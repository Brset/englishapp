package app.englishpron.engine

import app.englishpron.AsrWord

/** Speech recognizer producing timestamped words. Real implementation: whisper.cpp (TODO). */
interface AsrEngine {
    val name: String
    suspend fun transcribe(pcm: ShortArray, sampleRate: Int, reference: String): List<AsrWord>
}

class PhonemePosteriors(val logPosteriors: FloatArray, val nFrames: Int, val nClasses: Int, val frameSeconds: Double)

/** Phoneme recognizer (wav2vec2 CTC via onnxruntime, TODO). null = phoneme scoring unavailable. */
interface PhonemeEngine {
    val name: String
    suspend fun posteriors(pcm: ShortArray, sampleRate: Int): PhonemePosteriors?
}

/**
 * Demo ASR stub: pretends the user read the reference text perfectly, spreading the words evenly
 * over the recording. Lets the whole UI pipeline be exercised before whisper is integrated.
 */
class StubAsrEngine : AsrEngine {
    override val name = "stub"
    override suspend fun transcribe(pcm: ShortArray, sampleRate: Int, reference: String): List<AsrWord> {
        val words = Regex("[\\p{L}\\p{N}'’-]+").findAll(reference).map { it.value }.toList()
        if (words.isEmpty()) return emptyList()
        val total = pcm.size.toDouble() / sampleRate
        val step = total / words.size
        return words.mapIndexed { i, w -> AsrWord(w, i * step, (i + 1) * step * 0.95, 0.9f) }
    }
}

class StubPhonemeEngine : PhonemeEngine {
    override val name = "stub"
    override suspend fun posteriors(pcm: ShortArray, sampleRate: Int): PhonemePosteriors? = null
}
