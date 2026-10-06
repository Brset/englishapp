package app.englishpron

import org.json.JSONArray
import org.json.JSONObject

/** JNI bridge to pron_core (see core/include/pron/pron_c.h). All results are JSON strings. */
object PronCore {
    init { System.loadLibrary("pron_jni") }

    @JvmStatic private external fun nativeCreate(): Long
    @JvmStatic private external fun nativeDestroy(h: Long)
    @JvmStatic private external fun nativeVersion(): String
    @JvmStatic private external fun nativeLastError(h: Long): String
    @JvmStatic private external fun nativeLoadCmudict(h: Long, path: String): Int
    @JvmStatic private external fun nativeLoadCmudictText(h: Long, data: ByteArray): Int
    @JvmStatic private external fun nativeSetVocab(h: Long, labels: Array<String>, blank: Int): Int
    @JvmStatic private external fun nativeSetStrictness(h: Long, s: Int)
    @JvmStatic private external fun nativeTokenize(text: String): String?
    @JvmStatic private external fun nativeLookup(h: Long, word: String): String?
    @JvmStatic private external fun nativeAssess(
        h: Long, reference: String, words: Array<String>, starts: DoubleArray, ends: DoubleArray,
        probs: FloatArray, logPost: FloatArray?, nFrames: Int, nClasses: Int, frameSeconds: Double,
    ): String?

    private val lock = Any()
    private var handle = 0L

    private fun h(): Long = synchronized(lock) {
        if (handle == 0L) handle = nativeCreate()
        handle
    }

    val version: String get() = nativeVersion()
    fun lastError(): String = nativeLastError(h())

    /** Returns number of pronunciations loaded or -1. */
    fun loadCmudict(path: String): Int = synchronized(lock) { nativeLoadCmudict(h(), path) }
    fun loadCmudict(data: ByteArray): Int = synchronized(lock) { nativeLoadCmudictText(h(), data) }
    fun setVocab(labels: List<String>, blankIndex: Int): Int =
        synchronized(lock) { nativeSetVocab(h(), labels.toTypedArray(), blankIndex) }
    fun setStrictness(s: Int) = synchronized(lock) { nativeSetStrictness(h(), s) }

    fun tokenize(text: String): JSONArray? = nativeTokenize(text)?.let { JSONArray(it) }
    fun lookup(word: String): JSONObject? = synchronized(lock) { nativeLookup(h(), word) }?.let { JSONObject(it) }

    /** logPosteriors: row-major [nFrames x nClasses] or null (no phoneme scoring). */
    fun assess(
        reference: String,
        words: List<AsrWord>,
        logPosteriors: FloatArray? = null,
        nFrames: Int = 0,
        nClasses: Int = 0,
        frameSeconds: Double = 0.02,
    ): String? = synchronized(lock) {
        nativeAssess(
            h(), reference,
            Array(words.size) { words[it].text },
            DoubleArray(words.size) { words[it].start },
            DoubleArray(words.size) { words[it].end },
            FloatArray(words.size) { words[it].probability },
            logPosteriors, nFrames, nClasses, frameSeconds,
        )
    }

    fun release() = synchronized(lock) { if (handle != 0L) { nativeDestroy(handle); handle = 0L } }
}

data class AsrWord(val text: String, val start: Double, val end: Double, val probability: Float = 1f)
