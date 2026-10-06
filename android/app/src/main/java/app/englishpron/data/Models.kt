package app.englishpron.data

import androidx.compose.ui.graphics.Color
import org.json.JSONArray
import org.json.JSONObject

data class TextItem(
    val id: String, val level: String, val genre: String, val titleEn: String, val titleRu: String,
    val descriptionRu: String, val wordCount: Int, val status: String = "new", val bestScore: Double? = null,
)

data class TextFull(val item: TextItem, val body: String)

data class VocabItem(val word: String, val ipaUs: String, val ipaUk: String, val translationRu: String)

data class FocusSound(val sound: String, val tipRu: String, val examples: List<String>)

data class SoundSummary(val id: String, val nameRu: String, val ipa: String, val difficulty: Int)

data class SavedWord(
    val id: Long, val word: String, val ipa: String, val translationRu: String, val dueAt: Long,
    val ease: Double, val intervalDays: Double, val reps: Int, val lapses: Int,
)

data class ProgressSummary(
    val textsDone: Int, val textsStarted: Int, val attempts: Int, val avgScore: Double?, val streakDays: Int,
    val minutesToday: Double, val weakSounds: List<Triple<String, Int, Double>>, // ipa, errors, avg
    val recent: List<Pair<String, Double>>, // text title, score
)

// ---- Assessment result (JSON from pron_assess, see core/src/result_json.cpp) -------------------

data class PhonemeResult(val ipa: String, val score: Double?, val substituted: Boolean, val actualIpa: String, val adviceId: String?)

data class WordResult(
    val index: Int, val text: String, val u16Begin: Int, val u16End: Int, val status: String,
    val score: Double, val band: String, val colorHex: String, val expectedIpa: String,
    val phonemes: List<PhonemeResult>,
)

data class AdviceItem(val id: String, val soundId: String, val expectedIpa: String, val actualIpa: String,
                      val titleRu: String, val tipRu: String, val count: Int, val words: List<Int>)

data class AssessmentUi(
    val accuracy: Double, val completeness: Double, val fluency: Double, val overall: Double,
    val phonemeLevel: Boolean, val wpm: Double, val longPauses: Int,
    val words: List<WordResult>, val advice: List<AdviceItem>, val warnings: List<String>, val raw: String,
) {
    companion object {
        fun parse(json: String): AssessmentUi {
            val o = JSONObject(json)
            val sc = o.getJSONObject("scores")
            val fl = o.optJSONObject("fluency")
            fun JSONArray.objs() = (0 until length()).map { getJSONObject(it) }
            val words = o.getJSONArray("words").objs().map { w ->
                WordResult(
                    w.getInt("index"), w.getString("text"), w.optInt("u16_begin"), w.optInt("u16_end"),
                    w.optString("status"), w.optDouble("score", 0.0), w.optString("band"),
                    w.optString("color", "#9E9E9E"), w.optString("expected_ipa"),
                    w.optJSONArray("phonemes")?.objs()?.map { p ->
                        PhonemeResult(p.optString("ipa"), if (p.isNull("score")) null else p.optDouble("score"),
                            p.optBoolean("substituted"), p.optString("actual_ipa"),
                            if (p.isNull("advice_id")) null else p.optString("advice_id"))
                    } ?: emptyList(),
                )
            }
            val advice = o.optJSONArray("advice")?.objs()?.map { a ->
                AdviceItem(a.optString("id"), a.optString("sound_id"), a.optString("expected_ipa"),
                    a.optString("actual_ipa"), a.optString("title_ru"), a.optString("tip_ru"), a.optInt("count"),
                    a.optJSONArray("words")?.let { arr -> (0 until arr.length()).map { arr.getInt(it) } } ?: emptyList())
            } ?: emptyList()
            val warnings = o.optJSONArray("warnings")?.let { arr -> (0 until arr.length()).map { arr.getString(it) } } ?: emptyList()
            return AssessmentUi(
                sc.optDouble("accuracy"), sc.optDouble("completeness"), sc.optDouble("fluency"), sc.optDouble("overall"),
                o.optBoolean("phoneme_level"), fl?.optDouble("words_per_minute") ?: 0.0,
                fl?.optJSONArray("long_pauses")?.length() ?: 0, words, advice, warnings, json,
            )
        }
    }
}

fun parseColor(hex: String, fallback: Color = Color.Gray): Color = try {
    Color(android.graphics.Color.parseColor(hex))
} catch (_: IllegalArgumentException) { fallback }
