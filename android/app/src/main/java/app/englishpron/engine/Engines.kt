package app.englishpron.engine

import android.content.Context
import app.englishpron.audio.Player
import app.englishpron.audio.TtsSpeaker
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Job
import kotlinx.coroutines.asCoroutineDispatcher
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import org.json.JSONObject
import java.io.File
import java.util.concurrent.Executors

/** Raw JNI entry points (see src/main/cpp/pron_jni.cpp). */
object NativeEngine {
    init { System.loadLibrary("pron_jni") }

    @JvmStatic external fun engineCreate(modelsDir: String, threads: Int): Long
    @JvmStatic external fun engineDestroy(h: Long)
    @JvmStatic external fun engineStatus(h: Long): String?
    @JvmStatic external fun engineLastError(h: Long): String?
    @JvmStatic external fun engineSetStrictness(h: Long, s: Int)
    @JvmStatic external fun engineAssessPcm16(h: Long, pcm: ShortArray, sampleRate: Int, reference: String): String?
    /** Heavy assessment with progress callbacks on the calling thread; null on error/cancel. */
    @JvmStatic external fun engineAssessPcm16Progress(h: Long, pcm: ShortArray, sampleRate: Int, reference: String, listener: ProgressListener): String?
    /** Thread-safe: aborts the running assess call (it then returns null, last error "cancelled"). */
    @JvmStatic external fun engineCancel(h: Long)
    @JvmStatic external fun engineEstimateSeconds(h: Long, audioSeconds: Double): Double

    /** Called from native code (kept by proguard); stage = vad|asr|phoneme|assess|done, eta < 0 = unknown. */
    interface ProgressListener { fun onProgress(stage: String, fraction: Double, etaSec: Double) }

    /** Returns [sampleRate, samples...] or null. */
    @JvmStatic external fun engineTts(h: Long, text: String, voice: String, speed: Float): FloatArray?
    @JvmStatic external fun engineLookup(h: Long, word: String): String?

    // Live reading tracker; handle 0 = unavailable. Call only from the engine thread.
    @JvmStatic external fun liveStart(engine: Long, text: String): Long
    @JvmStatic external fun liveFeed(handle: Long, pcm: ShortArray, count: Int, rate: Int): String?
    @JvmStatic external fun liveFinish(handle: Long): String?
    @JvmStatic external fun liveSetCursor(handle: Long, index: Int)
    @JvmStatic external fun liveFree(handle: Long)
}

enum class EnginePhase { PREPARING, COPYING, LOADING, READY, FAILED }

data class EngineState(val phase: EnginePhase = EnginePhase.PREPARING, val progress: Float = 0f, val error: String? = null)

data class EngineStatus(
    val version: String = "", val asr: Boolean = false, val vad: Boolean = false, val phoneme: Boolean = false,
    val cmudict: Boolean = false, val ttsUs: Boolean = false, val ttsGb: Boolean = false, val errors: String = "",
    val known: Boolean = false,
) {
    /** Human-readable status in Russian. */
    fun describe(): String {
        if (!known) return "Движок: не загружен"
        fun m(b: Boolean) = if (b) "есть" else "нет"
        val sb = StringBuilder("Движок ").append(version).append('\n')
        sb.append("Распознавание речи (whisper): ").append(m(asr)).append('\n')
        sb.append("Детектор речи (VAD): ").append(m(vad)).append('\n')
        sb.append("Фонемная модель: ").append(m(phoneme)).append('\n')
        sb.append("Словарь CMUdict: ").append(m(cmudict)).append('\n')
        sb.append("Озвучка US: ").append(m(ttsUs)).append(", UK: ").append(m(ttsGb))
        if (errors.isNotBlank()) sb.append("\nОшибки: ").append(errors)
        return sb.toString()
    }
}

/** Owns the native engine: model copy, creation, and a single thread for every engine call. */
class EngineHost(private val context: Context) {
    private val executor = Executors.newSingleThreadExecutor { r -> Thread(r, "pron-engine") }
    private val dispatcher = executor.asCoroutineDispatcher()
    private val scope = CoroutineScope(dispatcher)
    // Background assessments get their own thread: live tracking / TTS / lookup stay responsive meanwhile.
    private val heavyExecutor = Executors.newSingleThreadExecutor { r -> Thread(r, "pron-heavy") }
    private val heavyDispatcher = heavyExecutor.asCoroutineDispatcher()
    @Volatile private var handle = 0L  // written/used on [dispatcher]; read by cancelHeavy() from any thread
    /** True while a background assessment is running (informational; it no longer blocks live/TTS). */
    @Volatile var heavyBusy = false
        private set
    @Volatile private var secPerAudioSec = 1.0
    private var strictness = 1

    private val _state = MutableStateFlow(EngineState())
    val state: StateFlow<EngineState> = _state
    private val _status = MutableStateFlow(EngineStatus())
    val status: StateFlow<EngineStatus> = _status

    @Volatile var lastError: String = ""
        private set

    fun start() {
        scope.launch {
            try {
                val dir = prepareModels()
                _state.value = EngineState(EnginePhase.LOADING, 1f)
                val h = NativeEngine.engineCreate(dir.absolutePath, 0)
                if (h == 0L) throw IllegalStateException("не удалось создать движок (${dir.absolutePath})")
                handle = h
                NativeEngine.engineSetStrictness(h, strictness)
                refreshStatus()
                _state.value = EngineState(EnginePhase.READY, 1f)
            } catch (e: Throwable) {
                _state.value = EngineState(EnginePhase.FAILED, 0f, e.message ?: e.toString())
            }
        }
    }

    private fun refreshStatus() {
        val js = NativeEngine.engineStatus(handle) ?: return
        val o = JSONObject(js)
        val tts = o.optJSONObject("tts")
        val err = o.optJSONObject("errors")
        val errText = err?.keys()?.asSequence()?.joinToString("; ") { "$it: ${err?.optString(it)}" }.orEmpty()
        _status.value = EngineStatus(
            o.optString("version"), o.optBoolean("asr"), o.optBoolean("vad"), o.optBoolean("phoneme"),
            o.optBoolean("cmudict"), tts?.optBoolean("us") ?: false, tts?.optBoolean("gb") ?: false, errText, true,
        )
    }

    /** Copies assets/models to filesDir/models when the bundled version changes. */
    private fun prepareModels(): File {
        val target = File(context.filesDir, "models")
        val marker = File(target, ".version")
        val assets = context.assets
        val version = try { assets.open("models_version.txt").bufferedReader().use { it.readText().trim() } } catch (_: Exception) { "" }
        if (version.isEmpty()) { target.mkdirs(); return target }  // no bundled models
        if (marker.exists() && marker.readText().trim() == version) return target

        _state.value = EngineState(EnginePhase.COPYING, 0f)
        val tmp = File(context.filesDir, "models.tmp")
        tmp.deleteRecursively()
        tmp.mkdirs()
        val files = ArrayList<String>()
        fun walk(path: String) {
            val kids = assets.list(path) ?: emptyArray()
            if (kids.isEmpty()) files += path else kids.forEach { walk("$path/$it") }
        }
        walk("models")
        val total = files.size.coerceAtLeast(1)
        // Free-space check up front: a full disk must give a clear message, not a half-copied model set.
        val need = files.sumOf { p ->
            try { assets.openFd(p).use { it.length } } catch (_: Exception) {
                try { assets.open(p).use { it.available().toLong() } } catch (_: Exception) { 0L }
            }
        } + (32L shl 20)
        fun free() = try { android.os.StatFs(context.filesDir.path).availableBytes } catch (_: Exception) { Long.MAX_VALUE }
        if (free() < need && target.exists()) target.deleteRecursively()  // old models are stale anyway
        if (free() < need) {
            tmp.deleteRecursively()
            throw IllegalStateException("Недостаточно места для распаковки моделей: нужно около ${need shr 20} МБ, " +
                "свободно ${free() shr 20} МБ. Освободите место на устройстве и запустите приложение снова.")
        }
        try {
            files.forEachIndexed { i, p ->
                val out = File(tmp, p.removePrefix("models/"))
                out.parentFile?.mkdirs()
                try {
                    assets.open(p).use { inp -> out.outputStream().use { inp.copyTo(it, 1 shl 16) } }
                } catch (_: java.io.FileNotFoundException) { out.delete() }  // empty asset directory
                if (i % 8 == 0 || i == files.lastIndex) _state.value = EngineState(EnginePhase.COPYING, (i + 1f) / total)
            }
        } catch (e: java.io.IOException) {
            tmp.deleteRecursively()
            throw IllegalStateException("Не удалось распаковать модели (возможно, не хватает места): ${e.message}")
        }
        File(tmp, ".version").writeText(version)
        target.deleteRecursively()
        if (!tmp.renameTo(target)) throw IllegalStateException("не удалось сохранить модели")
        return target
    }

    fun setStrictness(s: Int) {
        strictness = s
        scope.launch { if (handle != 0L) NativeEngine.engineSetStrictness(handle, s) }
    }

    /** Returns assessment JSON, or throws with the engine error. */
    suspend fun assess(pcm: ShortArray, sampleRate: Int, reference: String): String = withContext(dispatcher) {
        if (handle == 0L) throw IllegalStateException("движок ещё не готов")
        NativeEngine.engineAssessPcm16(handle, pcm, sampleRate, reference)
            ?: throw IllegalStateException(NativeEngine.engineLastError(handle).orEmpty().ifEmpty { "ошибка движка" })
    }

    /**
     * Background assessment with progress (stage, fraction 0..1, eta sec or < 0). Runs on the dedicated
     * heavy-job thread (the engine is thread-safe); throws [AssessCancelled] when [cancelHeavy] aborted it.
     */
    suspend fun assessProgress(pcm: ShortArray, sampleRate: Int, reference: String,
                               onProgress: (String, Double, Double) -> Unit): String = withContext(heavyDispatcher) {
        if (handle == 0L) throw IllegalStateException("движок ещё не готов")
        heavyBusy = true
        try {
            val l = object : NativeEngine.ProgressListener {
                override fun onProgress(stage: String, fraction: Double, etaSec: Double) {
                    try { onProgress(stage, fraction, etaSec) } catch (_: Throwable) {}
                }
            }
            NativeEngine.engineAssessPcm16Progress(handle, pcm, sampleRate, reference, l) ?: run {
                val err = NativeEngine.engineLastError(handle).orEmpty()
                if (err.contains("cancel", ignoreCase = true)) throw AssessCancelled()
                throw IllegalStateException(err.ifEmpty { "ошибка движка" })
            }
        } finally { heavyBusy = false }
    }

    /** Any thread. Safe when nothing is running (the native flag is cleared at the next assess start). */
    fun cancelHeavy() {
        val h = handle
        if (h != 0L) try { NativeEngine.engineCancel(h) } catch (_: Throwable) {}
    }

    /** Estimated processing seconds for [audioSeconds] of audio (thread-safe in the engine; any thread). */
    fun estimateSeconds(audioSeconds: Double): Double {
        val h = handle
        if (h == 0L) return audioSeconds * secPerAudioSec
        val v = try { NativeEngine.engineEstimateSeconds(h, 60.0) } catch (_: Throwable) { -1.0 }
        if (v > 0) secPerAudioSec = v / 60.0
        return audioSeconds * secPerAudioSec
    }

    /** Starts a live tracker for [reference]; null if the live model is missing or the engine is not ready. */
    suspend fun liveStart(reference: String): LiveSession? = withContext(dispatcher) {
        if (handle == 0L) return@withContext null
        val h = try { NativeEngine.liveStart(handle, reference) } catch (_: Throwable) { 0L }
        if (h == 0L) null else LiveSession(h, dispatcher)
    }

    suspend fun lookup(word: String): JSONObject? = withContext(dispatcher) {
        if (handle == 0L) null else NativeEngine.engineLookup(handle, word)?.let { JSONObject(it) }
    }

    /** Synthesizes speech; returns PCM16 + sample rate, or null on failure. */
    suspend fun tts(text: String, british: Boolean, speed: Float): Pair<ShortArray, Int>? = withContext(dispatcher) {
        if (handle == 0L) return@withContext null
        val a = NativeEngine.engineTts(handle, text, if (british) "gb" else "us", speed) ?: return@withContext null
        if (a.size < 2) return@withContext null
        val rate = a[0].toInt()
        Pair(ShortArray(a.size - 1) { (a[it + 1].coerceIn(-1f, 1f) * 32767f).toInt().toShort() }, rate)
    }
}

class AssessCancelled : Exception("cancelled")

/** Speech output: engine TTS (Piper) via AudioTrack; system TTS only if the engine has no voice. */
class Speaker(private val host: EngineHost, private val player: Player, private val system: TtsSpeaker) {
    private val scope = CoroutineScope(kotlinx.coroutines.Dispatchers.Default)
    private var job: Job? = null
    private var british = false

    fun setBritish(uk: Boolean) { british = uk; system.setBritish(uk) }

    fun speak(text: String, rate: Float = 1f) {
        if (text.isBlank()) return
        stop()
        val uk = british
        job = scope.launch {
            val st = host.status.value
            val have = if (uk) st.ttsGb else st.ttsUs
            val audio = if (have) host.tts(text, uk, rate) else null
            if (audio != null) player.play(audio.first, audio.second)
            else system.speak(text, rate)
        }
    }

    fun stop() {
        job?.cancel(); job = null
        player.stop()
        system.stop()
    }
}
