package app.englishpron.data

import android.database.sqlite.SQLiteDatabase
import app.englishpron.engine.LiveState
import app.englishpron.engine.LiveWord
import app.englishpron.engine.WordState
import java.text.SimpleDateFormat
import java.util.Calendar
import java.util.Date
import java.util.Locale
import kotlin.math.max
import kotlin.math.min

// SimpleDateFormat is not thread-safe and caches the time zone: build a fresh one per use
private val DAY get() = SimpleDateFormat("yyyy-MM-dd", Locale.US)

internal fun today(): String = DAY.format(Date())

/** v2 tables / columns: only CREATE TABLE IF NOT EXISTS and guarded ALTER (existing users keep their data). */
internal fun applyV2Schema(db: SQLiteDatabase) {
    fun cols(t: String): Set<String> = db.rawQuery("PRAGMA table_info($t)", null).use { c ->
        val s = HashSet<String>()
        val i = c.getColumnIndexOrThrow("name")
        while (c.moveToNext()) s += c.getString(i)
        s
    }
    fun addCol(t: String, col: String, type: String) { if (col !in cols(t)) db.execSQL("ALTER TABLE $t ADD COLUMN $col $type") }
    for ((c, t) in listOf("coverage_pct" to "REAL", "skipped_count" to "INTEGER", "duration_sec" to "REAL", "wpm" to "REAL",
        "paragraph_index" to "INTEGER", "words_read" to "INTEGER", "words_total" to "INTEGER")) addCol("recordings", c, t)
    addCol("progress", "best_coverage", "REAL")
    addCol("processing_jobs", "paragraph_index", "INTEGER")
    addCol("processing_jobs", "paragraph_count", "INTEGER NOT NULL DEFAULT 0")
    db.execSQL("CREATE TABLE IF NOT EXISTS reading_position (text_id TEXT PRIMARY KEY, word_index INTEGER NOT NULL DEFAULT 0, " +
        "paragraph_index INTEGER NOT NULL DEFAULT 0, updated_at INTEGER NOT NULL)")
    db.execSQL("CREATE TABLE IF NOT EXISTS xp_log (id INTEGER PRIMARY KEY AUTOINCREMENT, amount INTEGER NOT NULL, reason TEXT, created_at INTEGER NOT NULL)")
    db.execSQL("CREATE TABLE IF NOT EXISTS achievements (id TEXT PRIMARY KEY, unlocked_at INTEGER NOT NULL)")
    db.execSQL("CREATE TABLE IF NOT EXISTS daily_activity (date TEXT PRIMARY KEY, minutes REAL NOT NULL DEFAULT 0, " +
        "words INTEGER NOT NULL DEFAULT 0, xp INTEGER NOT NULL DEFAULT 0)")
}

internal fun AppDatabase.addXp(amount: Int, reason: String) {
    if (amount <= 0) return
    db.execSQL("INSERT INTO xp_log(amount, reason, created_at) VALUES(?,?,?)", arrayOf(amount, reason, now()))
    db.execSQL("INSERT INTO daily_activity(date, minutes, words, xp) VALUES(?,0,0,?) ON CONFLICT(date) DO UPDATE SET xp = xp + excluded.xp",
        arrayOf(today(), amount))
}

fun AppDatabase.xpTotal(): Int = list("SELECT COALESCE(SUM(amount),0) FROM xp_log") { it.getInt(0) }.first()

fun AppDatabase.readingPosition(textId: String): ReadingPos? =
    list("SELECT word_index, paragraph_index FROM reading_position WHERE text_id = ?", arrayOf(textId)) { ReadingPos(it.getInt(0), it.getInt(1)) }.firstOrNull()

/** Words read per paragraph index (latest attempt of each paragraph). */
fun AppDatabase.paragraphReads(textId: String): Map<Int, Int> = list(
    "SELECT paragraph_index, COALESCE(words_read,0) FROM recordings WHERE id IN " +
        "(SELECT MAX(id) FROM recordings WHERE text_id = ? AND kind = 'paragraph' GROUP BY paragraph_index)", arrayOf(textId)
) { it.getInt(0) to it.getInt(1) }.toMap()

internal fun AppDatabase.paragraphWordsRead(textId: String): Int = list(
    "SELECT COALESCE(SUM(words_read),0) FROM recordings WHERE id IN " +
        "(SELECT MAX(id) FROM recordings WHERE text_id = ? AND kind = 'paragraph' GROUP BY paragraph_index)", arrayOf(textId)
) { it.getInt(0) }.first()

internal fun AppDatabase.setPosition(textId: String, word: Int, paragraph: Int) =
    db.execSQL("INSERT OR REPLACE INTO reading_position(text_id, word_index, paragraph_index, updated_at) VALUES(?,?,?,?)",
        arrayOf(textId, word, paragraph, now()))

internal fun AppDatabase.clearPosition(textId: String) = db.execSQL("DELETE FROM reading_position WHERE text_id = ?", arrayOf(textId))

/** Unlocks achievements whose condition holds now; returns the newly unlocked ones. */
fun AppDatabase.checkAchievements(): List<AchievementDef> {
    val have = list("SELECT id FROM achievements") { it.getString(0) }.toSet()
    val done = list("SELECT COUNT(*) FROM progress WHERE status = 'done'") { it.getInt(0) }.first()
    val words = list("SELECT COALESCE(SUM(words),0) FROM daily_activity") { it.getInt(0) }.first()
    val ok = HashSet<String>()
    if (done >= 1) ok += "first_text"
    if (done >= 10) ok += "texts_10"
    if (words >= 1000) ok += "words_1000"
    if (words >= 10000) ok += "words_10000"
    if ("streak_7" !in have && progress().streakDays >= 7) ok += "streak_7"
    if ("all_a1" !in have) {
        val total = list("SELECT COUNT(*) FROM content.texts WHERE level = 'A1'") { it.getInt(0) }.first()
        val dn = list("SELECT COUNT(*) FROM content.texts t JOIN progress p ON p.text_id = t.id WHERE t.level = 'A1' AND p.status = 'done'") { it.getInt(0) }.first()
        if (total > 0 && dn >= total) ok += "all_a1"
    }
    if ("theta" !in have) {
        val th = list("SELECT attempts, COALESCE(avg_score,0) FROM phoneme_stats WHERE phoneme = 'θ'") { it.getInt(0) to it.getDouble(1) }.firstOrNull()
        if (th != null && th.first >= 5 && th.second >= 90) ok += "theta"
    }
    val out = ArrayList<AchievementDef>()
    for (a in ALL_ACHIEVEMENTS) if (a.id in ok && a.id !in have) {
        db.execSQL("INSERT OR IGNORE INTO achievements(id, unlocked_at) VALUES(?,?)", arrayOf(a.id, now()))
        out += a
    }
    return out
}

/** Weak words from a finished assessment go to the SRS dictionary automatically (max 5 per assessment). */
internal fun AppDatabase.autoAddWeakWords(textId: String, r: AssessmentUi) {
    val weak = r.words.filter { it.status != "omitted" && it.score < 60 && it.text.length >= 3 && it.text.all { ch -> ch.isLetter() || ch == '\'' } }
        .sortedBy { it.score }.take(5)
    for (w in weak) {
        val tr = list("SELECT translation_ru FROM content.text_vocabulary WHERE text_id = ? AND lower(word) = ?", arrayOf(textId, w.text.lowercase())) {
            it.getString(0)
        }.firstOrNull() ?: ""
        saveWord(w.text, w.expectedIpa, tr, textId)
    }
}

/** Latest reading made of paragraph recordings: one result aggregated over the latest attempt of each paragraph. */
internal fun AppDatabase.paragraphReading(textId: String): TextReading? {
    val body = list("SELECT body FROM content.texts WHERE id = ?", arrayOf(textId)) { it.getString(0) }.firstOrNull() ?: return null
    val spans = paragraphSpans(body)
    class Row(val id: Long, val wav: String, val json: String?, val pi: Int, val st: String?)
    val rows = list(
        "SELECT r.id, r.file_path, r.scores_json, COALESCE(r.paragraph_index,0), " +
            "(SELECT j.status FROM processing_jobs j WHERE j.wav_path = r.file_path ORDER BY j.id DESC LIMIT 1) FROM recordings r " +
            "WHERE r.id IN (SELECT MAX(id) FROM recordings WHERE text_id = ? AND kind = 'paragraph' GROUP BY paragraph_index) ORDER BY r.paragraph_index",
        arrayOf(textId)
    ) { Row(it.getLong(0), it.getString(1), if (it.isNull(2)) null else it.getString(2), it.getInt(3), if (it.isNull(4)) null else it.getString(4)) }
    if (rows.isEmpty()) return null
    val results = ArrayList<Triple<Int, AssessmentUi, String>>()
    val liveWords = ArrayList<LiveWord>()
    for (r in rows) {
        val span = spans.getOrNull(r.pi) ?: continue
        val js = r.json ?: continue
        if (js.contains("\"scores\"")) {
            val a = try { AssessmentUi.parse(js) } catch (_: Exception) { null } ?: continue
            results += Triple(r.pi, a, r.wav)
        } else LiveState.parse(js)?.let { ls ->
            for (w in ls.words) if (w.u16Begin >= 0) liveWords += LiveWord(r.pi * 100000 + w.index, w.state, w.u16Begin + span.first, w.u16End + span.first)
        }
    }
    var result: AssessmentUi? = null
    if (results.isNotEmpty()) {
        val allWords = ArrayList<WordResult>()
        var wSum = 0.0
        var acc = 0.0; var comp = 0.0; var flu = 0.0; var ov = 0.0; var wpm = 0.0; var pauses = 0
        val advice = LinkedHashMap<String, AdviceItem>()
        val warnings = LinkedHashSet<String>()
        for ((pi, a, wav) in results) {
            val off = spans[pi].first
            a.words.mapTo(allWords) { w -> w.copy(index = pi * 100000 + w.index, u16Begin = w.u16Begin + off, u16End = w.u16End + off, wavPath = wav) }
            val wt = a.words.size.toDouble().coerceAtLeast(1.0)
            wSum += wt; acc += a.accuracy * wt; comp += a.completeness * wt; flu += a.fluency * wt; ov += a.overall * wt; wpm += a.wpm * wt
            pauses += a.longPauses
            for (ad in a.advice) {
                val shifted = ad.words.map { pi * 100000 + it }
                val prev = advice[ad.id]
                advice[ad.id] = if (prev == null) ad.copy(words = shifted) else prev.copy(count = prev.count + ad.count, words = prev.words + shifted)
            }
            warnings += a.warnings
        }
        result = AssessmentUi(acc / wSum, comp / wSum, flu / wSum, ov / wSum, results.all { it.second.phonemeLevel }, wpm / wSum, pauses,
            allWords, advice.values.sortedByDescending { it.count }, warnings.toList(), "")
    }
    val live = if (liveWords.isEmpty()) null else LiveState(liveWords.size, liveWords.size, false, liveWords)
    val pending = rows.any { it.st == "queued" || it.st == "processing" }
    val failed = result == null && rows.any { it.st == "failed" }
    return TextReading(rows.last().id, rows.last().wav, live, result, pending, failed)
}

fun AppDatabase.continueInfo(): ContinueInfo? {
    val pos = list("SELECT text_id, paragraph_index FROM reading_position ORDER BY updated_at DESC LIMIT 1") { it.getString(0) to it.getInt(1) }.firstOrNull()
    val id = pos?.first ?: list("SELECT text_id FROM progress WHERE status = 'started' ORDER BY last_opened_at DESC LIMIT 1") { it.getString(0) }.firstOrNull() ?: return null
    val t = text(id) ?: return null
    return ContinueInfo(t.item, pos?.second ?: 0, paragraphSpans(t.body).size, t.item.bestCoverage ?: 0.0)
}

fun AppDatabase.weeklyMinutes(): List<Pair<String, Double>> {
    val names = listOf("Вс", "Пн", "Вт", "Ср", "Чт", "Пт", "Сб")
    val cal = Calendar.getInstance()
    cal.add(Calendar.DAY_OF_YEAR, -6)
    val out = ArrayList<Pair<String, Double>>()
    repeat(7) {
        val m = list("SELECT minutes FROM daily_streak WHERE day = ?", arrayOf(DAY.format(cal.time))) { it.getDouble(0) }.firstOrNull() ?: 0.0
        out += names[cal.get(Calendar.DAY_OF_WEEK) - 1] to m
        cal.add(Calendar.DAY_OF_YEAR, 1)
    }
    return out
}

fun AppDatabase.homeData(level: String, goal: Int): HomeData {
    val cand = texts(level, null, "").filter { it.status != "done" && (it.bestCoverage ?: 0.0) < 50 }
        .ifEmpty { texts(null, null, "").filter { it.status != "done" } }
    val day = Calendar.getInstance().get(Calendar.DAY_OF_YEAR)
    return HomeData(progress(), goal, xpTotal(), savedWords(true).size, continueInfo(),
        if (cand.isEmpty()) null else cand[day % cand.size], weeklyMinutes())
}

fun AppDatabase.progressV2(): ProgressV2 {
    val acc = list("SELECT score FROM recordings WHERE score IS NOT NULL ORDER BY created_at DESC, id DESC LIMIT 30") { it.getDouble(0) }.reversed()
    val cal = Calendar.getInstance()
    val weeks = ArrayList<Double>()
    repeat(8) {
        val end = DAY.format(cal.time)
        cal.add(Calendar.DAY_OF_YEAR, -6)
        val start = DAY.format(cal.time)
        weeks += list("SELECT COALESCE(SUM(minutes),0) FROM daily_streak WHERE day BETWEEN ? AND ?", arrayOf(start, end)) { it.getDouble(0) }.first()
        cal.add(Calendar.DAY_OF_YEAR, -1)
    }
    val levels = list("SELECT t.level, COALESCE(SUM(p.status = 'done'),0), COUNT(*) FROM content.texts t LEFT JOIN progress p ON p.text_id = t.id GROUP BY t.level ORDER BY t.level") {
        Triple(it.getString(0), it.getInt(1), it.getInt(2))
    }
    val words = list("SELECT COALESCE(SUM(words),0) FROM daily_activity") { it.getInt(0) }.first()
    return ProgressV2(acc, weeks.reversed(), levels, words, xpTotal(), list("SELECT id FROM achievements") { it.getString(0) }.toSet())
}

/** Sound id for an IPA symbol (null when there is no card). */
fun AppDatabase.soundIdForIpa(ipa: String): String? = sounds().firstOrNull { it.ipa == ipa || it.id == ipa }?.id

fun clampPct(v: Double) = max(0.0, min(100.0, v))
