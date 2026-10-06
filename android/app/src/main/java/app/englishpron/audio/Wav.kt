package app.englishpron.audio

import java.io.File
import java.io.RandomAccessFile
import java.nio.ByteBuffer
import java.nio.ByteOrder

/** Minimal PCM16 mono WAV (canonical 44-byte header) reader/writer. Blocking: call from IO. */
object Wav {
    private const val HEADER = 44

    fun write(file: File, pcm: ShortArray, rate: Int = SAMPLE_RATE) {
        file.parentFile?.mkdirs()
        val tmp = File(file.path + ".tmp")
        val dataLen = pcm.size * 2
        val bb = ByteBuffer.allocate(HEADER).order(ByteOrder.LITTLE_ENDIAN)
        bb.put("RIFF".toByteArray()).putInt(36 + dataLen).put("WAVE".toByteArray())
        bb.put("fmt ".toByteArray()).putInt(16).putShort(1).putShort(1).putInt(rate).putInt(rate * 2).putShort(2).putShort(16)
        bb.put("data".toByteArray()).putInt(dataLen)
        tmp.outputStream().buffered(1 shl 16).use { o ->
            o.write(bb.array())
            val chunk = ByteBuffer.allocate(1 shl 15).order(ByteOrder.LITTLE_ENDIAN)
            var i = 0
            while (i < pcm.size) {
                chunk.clear()
                val n = minOf(pcm.size - i, chunk.capacity() / 2)
                for (k in 0 until n) chunk.putShort(pcm[i + k])
                o.write(chunk.array(), 0, n * 2)
                i += n
            }
        }
        if (!tmp.renameTo(file)) { file.delete(); check(tmp.renameTo(file)) { "cannot save recording" } }
    }

    /** Whole file as samples (mono PCM16 assumed). */
    fun read(file: File): ShortArray = range(file, 0.0, Double.MAX_VALUE).first

    /** Samples between [fromSec, toSec] and the sample rate. Empty when the file is missing/invalid. */
    fun range(file: File, fromSec: Double, toSec: Double): Pair<ShortArray, Int> {
        if (!file.isFile || file.length() < HEADER) return ShortArray(0) to SAMPLE_RATE
        RandomAccessFile(file, "r").use { f ->
            val head = ByteArray(HEADER)
            f.readFully(head)
            val rate = ByteBuffer.wrap(head, 24, 4).order(ByteOrder.LITTLE_ENDIAN).int.takeIf { it in 8000..96000 } ?: SAMPLE_RATE
            val total = ((f.length() - HEADER) / 2).coerceAtLeast(0)
            val a = (fromSec.coerceAtLeast(0.0) * rate).toLong().coerceIn(0, total)
            val b = if (toSec >= Double.MAX_VALUE / 2) total else (toSec * rate).toLong().coerceIn(a, total)
            val n = (b - a).toInt()
            if (n <= 0) return ShortArray(0) to rate
            f.seek(HEADER + a * 2)
            val bytes = ByteArray(n * 2)
            f.readFully(bytes)
            val sb = ByteBuffer.wrap(bytes).order(ByteOrder.LITTLE_ENDIAN).asShortBuffer()
            val out = ShortArray(n)
            sb.get(out)
            return out to rate
        }
    }

    fun durationSec(file: File): Double = if (file.length() < HEADER) 0.0 else (file.length() - HEADER) / 2.0 / SAMPLE_RATE
}
