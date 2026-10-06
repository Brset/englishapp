package app.englishpron.audio

import android.annotation.SuppressLint
import android.content.Context
import android.media.AudioAttributes
import android.media.AudioFormat
import android.media.AudioRecord
import android.media.AudioTrack
import android.media.MediaRecorder
import android.speech.tts.TextToSpeech
import android.speech.tts.UtteranceProgressListener
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.isActive
import kotlinx.coroutines.launch
import java.io.ByteArrayOutputStream
import java.util.Locale
import kotlin.math.sqrt

const val SAMPLE_RATE = 16000

/** AudioRecord 16 kHz mono PCM16 with a 0..1 level meter. Needs RECORD_AUDIO granted. */
class Recorder {
    private val scope = CoroutineScope(Dispatchers.IO)
    private var job: Job? = null
    private var rec: AudioRecord? = null
    private val buf = ByteArrayOutputStream()

    private val _level = MutableStateFlow(0f)
    val level: StateFlow<Float> = _level
    private val _recording = MutableStateFlow(false)
    val recording: StateFlow<Boolean> = _recording

    @SuppressLint("MissingPermission")
    fun start(sink: ((ShortArray) -> Unit)? = null): Boolean {
        if (_recording.value) return true
        val minBuf = AudioRecord.getMinBufferSize(SAMPLE_RATE, AudioFormat.CHANNEL_IN_MONO, AudioFormat.ENCODING_PCM_16BIT)
        if (minBuf <= 0) return false
        val r = try {
            AudioRecord(MediaRecorder.AudioSource.MIC, SAMPLE_RATE, AudioFormat.CHANNEL_IN_MONO,
                AudioFormat.ENCODING_PCM_16BIT, minBuf * 2)
        } catch (e: SecurityException) { return false } catch (e: IllegalArgumentException) { return false }
        if (r.state != AudioRecord.STATE_INITIALIZED) { r.release(); return false }
        buf.reset()
        try { r.startRecording() } catch (e: IllegalStateException) { r.release(); return false }  // mic busy (call etc.)
        if (r.recordingState != AudioRecord.RECORDSTATE_RECORDING) { try { r.stop() } catch (_: IllegalStateException) {}; r.release(); return false }
        rec = r
        _recording.value = true
        job = scope.launch {
            val chunk = ByteArray(2048)
            while (isActive) {
                val n = r.read(chunk, 0, chunk.size)
                if (n <= 0) break
                buf.write(chunk, 0, n)
                if (sink != null) {
                    val m = n / 2
                    val frame = ShortArray(m) { ((chunk[2 * it + 1].toInt() shl 8) or (chunk[2 * it].toInt() and 0xFF)).toShort() }
                    try { sink(frame) } catch (_: Throwable) {}  // never let live tracking break recording
                }
                var sum = 0.0
                var i = 0
                while (i + 1 < n) {
                    val s = ((chunk[i + 1].toInt() shl 8) or (chunk[i].toInt() and 0xFF)).toShort().toInt()
                    sum += s.toDouble() * s
                    i += 2
                }
                val rms = sqrt(sum / (n / 2).coerceAtLeast(1)) / 32768.0
                _level.value = (rms * 6).toFloat().coerceIn(0f, 1f)  // simple gain for display
            }
        }
        return true
    }

    /** Stops and returns the recorded PCM16 samples. */
    suspend fun stop(): ShortArray {
        val r = rec ?: return ShortArray(0)
        try { r.stop() } catch (_: IllegalStateException) {}
        job?.join()
        r.release()
        rec = null
        _recording.value = false
        _level.value = 0f
        val bytes = buf.toByteArray()
        return ShortArray(bytes.size / 2) { ((bytes[2 * it + 1].toInt() shl 8) or (bytes[2 * it].toInt() and 0xFF)).toShort() }
    }
}

/** Plays PCM16 mono buffers via AudioTrack. */
class Player {
    private var track: AudioTrack? = null

    @Synchronized
    fun play(pcm: ShortArray, sampleRate: Int = SAMPLE_RATE, onDone: () -> Unit = {}) {
        stop()
        if (pcm.isEmpty()) { onDone(); return }
        val t = AudioTrack.Builder()
            .setAudioAttributes(AudioAttributes.Builder().setUsage(AudioAttributes.USAGE_MEDIA)
                .setContentType(AudioAttributes.CONTENT_TYPE_SPEECH).build())
            .setAudioFormat(AudioFormat.Builder().setSampleRate(sampleRate)
                .setEncoding(AudioFormat.ENCODING_PCM_16BIT).setChannelMask(AudioFormat.CHANNEL_OUT_MONO).build())
            .setBufferSizeInBytes(pcm.size * 2)
            .setTransferMode(AudioTrack.MODE_STATIC)
            .build()
        t.write(pcm, 0, pcm.size)
        t.setNotificationMarkerPosition(pcm.size)
        t.setPlaybackPositionUpdateListener(object : AudioTrack.OnPlaybackPositionUpdateListener {
            override fun onMarkerReached(track: AudioTrack?) { onDone() }
            override fun onPeriodicNotification(track: AudioTrack?) {}
        })
        track = t
        t.play()
    }

    @Synchronized
    fun stop() {
        track?.let { try { it.stop() } catch (_: IllegalStateException) {}; it.release() }
        track = null
    }
}

/** Temporary speech synthesis via the system TTS (to be replaced with Piper). */
class TtsSpeaker(context: Context) {
    private var ready = false
    private var pendingLocale: Locale = Locale.US
    private lateinit var tts: TextToSpeech

    private val _speaking = MutableStateFlow(false)
    val speaking: StateFlow<Boolean> = _speaking

    init {
        tts = TextToSpeech(context.applicationContext) { status ->
            ready = status == TextToSpeech.SUCCESS
            if (ready) tts.language = pendingLocale
        }
        tts.setOnUtteranceProgressListener(object : UtteranceProgressListener() {
            override fun onStart(utteranceId: String?) { _speaking.value = true }
            override fun onDone(utteranceId: String?) { _speaking.value = false }
            @Deprecated("Deprecated in Java")
            override fun onError(utteranceId: String?) { _speaking.value = false }
        })
    }

    fun setBritish(uk: Boolean) {
        pendingLocale = if (uk) Locale.UK else Locale.US
        if (ready) tts.language = pendingLocale
    }

    /** rate: 0.5..1.5 (1 = normal). */
    fun speak(text: String, rate: Float = 1f) {
        if (!ready || text.isBlank()) return
        tts.setSpeechRate(rate)
        tts.speak(text, TextToSpeech.QUEUE_FLUSH, null, "u${System.nanoTime()}")
    }

    fun stop() { tts.stop(); _speaking.value = false }
    fun shutdown() { tts.stop(); tts.shutdown() }
}
