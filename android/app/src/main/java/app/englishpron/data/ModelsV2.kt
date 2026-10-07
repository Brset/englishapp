package app.englishpron.data

/** Paragraphs of a text body as (start, end) offsets (end exclusive); blank-line separated, trimmed. */
fun paragraphSpans(body: String): List<Pair<Int, Int>> {
    val out = ArrayList<Pair<Int, Int>>()
    fun add(a: Int, b: Int) {
        var s = a
        var e = b
        while (s < e && body[s].isWhitespace()) s++
        while (e > s && body[e - 1].isWhitespace()) e--
        if (e > s) out += s to e
    }
    var start = 0
    for (m in Regex("\\n\\s*\\n").findAll(body)) { add(start, m.range.first); start = m.range.last + 1 }
    add(start, body.length)
    return out
}

fun paragraphsOf(body: String): List<String> = paragraphSpans(body).map { body.substring(it.first, it.second) }

/** Everything the DB needs to know about one saved reading (whole text, paragraph or sentence). */
data class ReadingMeta(
    val coverage: Double, val skipped: Int, val wordsRead: Int, val wordsTotal: Int, val durationSec: Double, val wpm: Double,
    val paragraphIndex: Int?, val paragraphCount: Int, val cursor: Int, val nextParagraph: Int,
)

data class AchievementDef(val id: String, val title: String, val desc: String)

data class SaveOutcome(val jobId: Long, val textCoverage: Double, val newlyDone: Boolean, val xp: Int, val achievements: List<AchievementDef>)

data class ReadingPos(val wordIndex: Int, val paragraphIndex: Int)

data class ContinueInfo(val item: TextItem, val paragraphIndex: Int, val paragraphCount: Int, val coverage: Double)

data class LevelInfo(val index: Int, val name: String, val floor: Int, val next: Int?)

val ALL_ACHIEVEMENTS = listOf(
    AchievementDef("first_text", "Первый текст", "Пройден первый текст"),
    AchievementDef("texts_10", "Десять текстов", "Пройдено 10 текстов"),
    AchievementDef("streak_7", "Неделя подряд", "7 дней занятий подряд"),
    AchievementDef("words_1000", "1 000 слов", "Прочитано 1 000 слов"),
    AchievementDef("words_10000", "10 000 слов", "Прочитано 10 000 слов"),
    AchievementDef("all_a1", "Весь A1", "Пройдены все тексты уровня A1"),
    AchievementDef("theta", "Идеальный θ", "Средний балл звука θ не ниже 90"),
)

private val LEVELS = listOf(0 to "Новичок", 100 to "Ученик", 300 to "Практик", 700 to "Чтец", 1500 to "Знаток", 3000 to "Эксперт", 6000 to "Мастер")

fun levelFor(xp: Int): LevelInfo {
    var i = 0
    for (k in LEVELS.indices) if (xp >= LEVELS[k].first) i = k
    return LevelInfo(i, LEVELS[i].second, LEVELS[i].first, LEVELS.getOrNull(i + 1)?.first)
}

data class HomeData(
    val progress: ProgressSummary, val goalMinutes: Int, val xp: Int, val dueCount: Int,
    val cont: ContinueInfo?, val textOfDay: TextItem?, val weekly: List<Pair<String, Double>>,
)

data class ProgressV2(
    val accuracy: List<Double>, val weeks: List<Double>, val levels: List<Triple<String, Int, Int>>, // level, done, total
    val totalWords: Int, val xp: Int, val achievements: Set<String>,
)
