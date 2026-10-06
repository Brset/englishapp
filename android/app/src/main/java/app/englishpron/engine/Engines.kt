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
    /** Returns [sampleRate, samples...] or null. */
    @JvmStatic external fun engineTts(h: Long, text: String, voice: String, speed: Float): FloatArray?
    @JvmStatic external fun engineLookup(h: Long, word: String): String?
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
    private var handle = 0L  // only touched on [dispatcher]
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
        files.forEachIndexed { i, p ->
            val out = File(tmp, p.removePrefix("models/"))
            out.parentFile?.mkdirs()
            try {
                assets.open(p).use { inp -> out.outputStream().use { inp.copyTo(it, 1 shl 16) } }
            } catch (_: java.io.FileNotFoundException) { out.delete() }  // empty asset directory
            if (i % 8 == 0 || i == files.lastIndex) _state.value = EngineState(EnginePhase.COPYING, (i + 1f) / total)
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
