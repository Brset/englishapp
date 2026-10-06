package app.englishpron.engine

import kotlinx.coroutines.CoroutineDispatcher
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import org.json.JSONObject
import java.util.concurrent.atomic.AtomicBoolean

enum class WordState { READ, SKIPPED, CURRENT, PENDING }

data class LiveWord(val index: Int, val state: WordState, val u16Begin: Int, val u16End: Int)

data class LiveState(val cursor: Int, val scrollTo: Int, val done: Boolean, val words: List<LiveWord>) {
    companion object {
        fun parse(json: String): LiveState? = try {
            val o = JSONObject(json)
            val arr = o.optJSONArray("words")
            val words = ArrayList<LiveWord>(arr?.length() ?: 0)
            if (arr != null) for (k in 0 until arr.length()) {
                val w = arr.optJSONObject(k) ?: continue
                val st = when (w.optString("state")) {
                    "read" -> WordState.READ
                    "skipped" -> WordState.SKIPPED
                    "current" -> WordState.CURRENT
                    else -> WordState.PENDING
                }
                words += LiveWord(w.optInt("i", k), st, w.optInt("u16_begin", -1), w.optInt("u16_end", -1))
            }
            LiveState(o.optInt("cursor"), o.optInt("scroll_to", o.optInt("cursor")), o.optBoolean("done"), words)
        } catch (_: Exception) { null }
    }
}

/**
 * One live tracker. [offer] is called from the audio thread and never blocks on the engine: audio is
 * merged into a pending buffer and fed (all pending samples at once) on the engine dispatcher whenever
 * the previous feed has completed. Every native call happens on [dispatcher].
 */
class LiveSession internal constructor(private val handle: Long, private val dispatcher: CoroutineDispatcher) {
    private val scope = CoroutineScope(SupervisorJob() + dispatcher)
    private val lock = Any()
    private var pending = ShortArray(RATE)
    private var pendingLen = 0
    private val inflight = AtomicBoolean(false)
    @Volatile private var closed = false
    private val freed = AtomicBoolean(false)

    private val _state = MutableStateFlow<LiveState?>(null)
    val state: StateFlow<LiveState?> = _state

    /** Audio thread. Cheap: a short lock and a copy. */
    fun offer(samples: ShortArray) {
        if (closed || samples.isEmpty()) return
        var take: ShortArray? = null
        synchronized(lock) {
            if (pendingLen + samples.size > pending.size) pending = pending.copyOf(maxOf(pending.size * 2, pendingLen + samples.size))
            System.arraycopy(samples, 0, pending, pendingLen, samples.size)
            pendingLen += samples.size
            if (pendingLen >= MIN_SAMPLES && inflight.compareAndSet(false, true)) take = takePending()
        }
        take?.let { first -> scope.launch { feedLoop(first) } }
    }

    private fun takePending(): ShortArray { // holds lock
        val r = pending.copyOf(pendingLen)
        pendingLen = 0
        return r
    }

    private fun feedLoop(first: ShortArray) {
        var chunk: ShortArray? = first
        while (chunk != null) {
            if (!freed.get()) {
                try { NativeEngine.liveFeed(handle, chunk, chunk.size, RATE)?.let { LiveState.parse(it) }?.let { _state.value = it } }
                catch (_: Throwable) {}
            }
            chunk = synchronized(lock) {
                if (pendingLen >= MIN_SAMPLES) takePending() else { inflight.set(false); null }
            }
        }
    }

    /** User tapped a word: restart reading from there. */
    fun setCursor(index: Int) {
        _state.value?.let { st ->
            _state.value = st.copy(cursor = index, scrollTo = index, done = false, words = st.words.map {
                it.copy(state = when {
                    it.index < index -> if (it.state == WordState.SKIPPED) WordState.SKIPPED else WordState.READ
                    it.index == index -> WordState.CURRENT
                    else -> WordState.PENDING
                })
            })
        }
        scope.launch { if (!freed.get()) try { NativeEngine.liveSetCursor(handle, index) } catch (_: Throwable) {} }
    }

    /** Stops accepting audio, feeds the tail, flushes the recognizer, publishes the final state and frees the handle. */
    suspend fun finish(): LiveState? = withContext(dispatcher) {
        closed = true
        if (freed.get()) return@withContext _state.value
        try {
            val tail = synchronized(lock) { if (pendingLen > 0) takePending() else null }
            if (tail != null) NativeEngine.liveFeed(handle, tail, tail.size, RATE)?.let { LiveState.parse(it) }?.let { _state.value = it }
            NativeEngine.liveFinish(handle)?.let { LiveState.parse(it) }?.let { _state.value = it }
        } catch (_: Throwable) {
        } finally { free() }
        _state.value
    }

    /** Discards without a final flush (screen left, error). Safe to call repeatedly. */
    fun release() {
        closed = true
        scope.launch { free() }
    }

    private fun free() { if (freed.compareAndSet(false, true)) try { NativeEngine.liveFree(handle) } catch (_: Throwable) {} }

    private companion object {
        const val RATE = 16000
        const val MIN_SAMPLES = RATE * 160 / 1000  // ~160 ms
    }
}
