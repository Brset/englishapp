package app.englishpron.data

import android.content.Context
import android.database.Cursor
import android.database.sqlite.SQLiteDatabase
import org.json.JSONObject
import java.io.File
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale
import kotlin.math.max

private const val CONTENT_ASSET = "content.db"
private const val USER_SCHEMA_ASSET = "user_schema.sql"

/**
 * user.db (read/write) with content.db ATTACHed as `content`. Non-WAL mode keeps a single
 * connection, so the ATTACH is visible to every query. All methods are blocking: call from IO.
 */
class AppDatabase private constructor(private val db: SQLiteDatabase) {

    // ---- library -------------------------------------------------------------------------------
    fun levels(): List<String> = list("SELECT DISTINCT level FROM content.texts ORDER BY level") { it.getString(0) }
    fun genres(): List<String> = list("SELECT DISTINCT genre FROM content.texts ORDER BY genre") { it.getString(0) }

    fun texts(level: String?, genre: String?, query: String): List<TextItem> {
        val where = ArrayList<String>()
        val args = ArrayList<String>()
        if (level != null) { where += "t.level = ?"; args += level }
        if (genre != null) { where += "t.genre = ?"; args += genre }
        val fts = ftsQuery(query)
        if (fts != null) {
            where += "t.rowid IN (SELECT rowid FROM content.texts_fts WHERE texts_fts MATCH ?)"; args += fts
        }
        val sql = "SELECT t.id, t.level, t.genre, t.title_en, t.title_ru, t.description_ru, t.word_count, " +
            "COALESCE(p.status,'new'), p.best_score FROM content.texts t LEFT JOIN progress p ON p.text_id = t.id " +
            (if (where.isEmpty()) "" else "WHERE " + where.joinToString(" AND ")) + " ORDER BY t.sort_order"
        return try { list(sql, args.toTypedArray(), ::textItem) } catch (e: android.database.sqlite.SQLiteException) { emptyList() }
    }

    private fun ftsQuery(q: String): String? {
        val toks = q.split(Regex("[^\\p{L}\\p{N}]+")).filter { it.isNotEmpty() }
        return if (toks.isEmpty()) null else toks.joinToString(" ") { "\"$it\"*" }
    }

    private fun textItem(c: Cursor) = TextItem(
        c.getString(0), c.getString(1), c.getString(2), c.getString(3), c.getString(4), c.getString(5),
        c.getInt(6), c.getString(7), if (c.isNull(8)) null else c.getDouble(8),
    )

    fun text(id: String): TextFull? = list(
        "SELECT t.id, t.level, t.genre, t.title_en, t.title_ru, t.description_ru, t.word_count, " +
            "COALESCE(p.status,'new'), p.best_score, t.body FROM content.texts t " +
            "LEFT JOIN progress p ON p.text_id = t.id WHERE t.id = ?", arrayOf(id)
    ) { TextFull(textItem(it), it.getString(9)) }.firstOrNull()

    fun vocabulary(textId: String): List<VocabItem> = list(
        "SELECT word, ipa_us, ipa_uk, translation_ru FROM content.text_vocabulary WHERE text_id = ? ORDER BY position",
        arrayOf(textId)
    ) { VocabItem(it.getString(0), it.getString(1), it.getString(2), it.getString(3)) }

    fun focusSounds(textId: String): List<FocusSound> = list(
        "SELECT id, sound, tip_ru FROM content.text_focus_sounds WHERE text_id = ? ORDER BY position", arrayOf(textId)
    ) { c ->
        val ex = list("SELECT example FROM content.text_focus_examples WHERE focus_id = ? ORDER BY position",
            arrayOf(c.getLong(0).toString())) { it.getString(0) }
        FocusSound(c.getString(1), c.getString(2), ex)
    }

    fun markOpened(textId: String) = db.execSQL(
        "INSERT INTO progress(text_id, status, last_opened_at) VALUES(?, 'started', ?) " +
            "ON CONFLICT(text_id) DO UPDATE SET last_opened_at = excluded.last_opened_at, " +
            "status = CASE WHEN status='new' THEN 'started' ELSE status END", arrayOf(textId, now()))

    // ---- sounds --------------------------------------------------------------------------------
    fun sounds(): List<SoundSummary> = list(
        "SELECT id, COALESCE(name_ru,id), COALESCE(ipa,id), CAST(COALESCE(difficulty,'1') AS INTEGER) FROM content.sounds ORDER BY sort_order"
    ) { SoundSummary(it.getString(0), it.getString(1), it.getString(2), it.getInt(3)) }

    fun soundCard(id: String): JSONObject? =
        list("SELECT json FROM content.sounds WHERE id = ?", arrayOf(id)) { JSONObject(it.getString(0)) }.firstOrNull()

    // ---- dictionary / SRS ----------------------------------------------------------------------
    fun saveWord(word: String, ipa: String, translation: String, sourceTextId: String?) = db.execSQL(
        "INSERT OR IGNORE INTO saved_words(word, ipa, translation_ru, source_text_id, due_at, created_at) VALUES(?,?,?,?,?,?)",
        arrayOf(word.lowercase(), ipa, translation, sourceTextId, now(), now()))

    fun isSaved(word: String): Boolean =
        list("SELECT 1 FROM saved_words WHERE word = ?", arrayOf(word.lowercase())) { 1 }.isNotEmpty()

    fun deleteWord(id: Long) = db.execSQL("DELETE FROM saved_words WHERE id = ?", arrayOf(id))

    fun savedWords(dueOnly: Boolean): List<SavedWord> = list(
        "SELECT id, word, COALESCE(ipa,''), COALESCE(translation_ru,''), due_at, ease, interval_days, reps, lapses " +
            "FROM saved_words " + (if (dueOnly) "WHERE due_at <= ? " else "WHERE ? > 0 ") + "ORDER BY due_at",
        arrayOf(now().toString())
    ) { SavedWord(it.getLong(0), it.getString(1), it.getString(2), it.getString(3), it.getLong(4), it.getDouble(5), it.getDouble(6), it.getInt(7), it.getInt(8)) }

    /** Simplified SM-2. grade: 0 = again, 1 = hard, 2 = good, 3 = easy. */
    fun review(w: SavedWord, grade: Int) {
        var ease = w.ease
        var interval: Double
        var reps = w.reps
        var lapses = w.lapses
        if (grade == 0) {
            reps = 0; lapses++; interval = 0.0; ease = max(1.3, ease - 0.2)
        } else {
            reps++
            ease = max(1.3, ease + when (grade) { 1 -> -0.15; 3 -> 0.15; else -> 0.0 })
            interval = when {
                reps == 1 -> 1.0
                reps == 2 -> 3.0
                else -> w.intervalDays * ease * (if (grade == 1) 0.8 else if (grade == 3) 1.3 else 1.0)
            }
        }
        val due = now() + if (grade == 0) 600L else (interval * 86400).toLong()
        db.execSQL("UPDATE saved_words SET ease=?, interval_days=?, reps=?, lapses=?, due_at=? WHERE id=?",
            arrayOf(ease, interval, reps, lapses, due, w.id))
    }

    // ---- results / progress --------------------------------------------------------------------
    fun saveResult(textId: String, kind: String, durationMs: Long, r: AssessmentUi) {
        db.beginTransaction()
        try {
            db.execSQL("INSERT INTO recordings(text_id, kind, file_path, duration_ms, score, scores_json, created_at) VALUES(?,?,?,?,?,?,?)",
                arrayOf(textId, kind, "", durationMs, r.overall, r.raw, now()))
            val done = r.overall >= 80
            db.execSQL(
                "INSERT INTO progress(text_id, status, best_score, attempts, last_opened_at, completed_at) VALUES(?,?,?,?,?,?) " +
                    "ON CONFLICT(text_id) DO UPDATE SET attempts = attempts + 1, " +
                    "best_score = MAX(COALESCE(best_score,0), excluded.best_score), last_opened_at = excluded.last_opened_at, " +
                    "status = CASE WHEN ? = 1 THEN 'done' ELSE status END, " +
                    "completed_at = CASE WHEN ? = 1 AND completed_at IS NULL THEN excluded.last_opened_at ELSE completed_at END",
                arrayOf(textId, if (done) "done" else "started", r.overall, 1, now(), if (done) now() else null, if (done) 1 else 0, if (done) 1 else 0))
            for (w in r.words) for (p in w.phonemes) {
                val s = p.score ?: continue
                val err = if (p.substituted || s < 60) 1 else 0
                db.execSQL(
                    "INSERT INTO phoneme_stats(phoneme, attempts, errors, avg_score, updated_at) VALUES(?,?,?,?,?) " +
                        "ON CONFLICT(phoneme) DO UPDATE SET avg_score = (COALESCE(avg_score,0) * attempts + ?) / (attempts + 1), " +
                        "attempts = attempts + 1, errors = errors + ?, updated_at = ?",
                    arrayOf(p.ipa, 1, err, s, now(), s, err, now()))
            }
            val day = SimpleDateFormat("yyyy-MM-dd", Locale.US).format(Date())
            db.execSQL(
                "INSERT INTO daily_streak(day, minutes, texts_done, goal_met) VALUES(?,?,?,0) " +
                    "ON CONFLICT(day) DO UPDATE SET minutes = minutes + excluded.minutes, texts_done = texts_done + excluded.texts_done",
                arrayOf(day, durationMs / 60000.0, if (done) 1 else 0))
            db.setTransactionSuccessful()
        } finally { db.endTransaction() }
    }

    fun progress(): ProgressSummary {
        val (done, started) = list("SELECT SUM(status='done'), SUM(status!='new') FROM progress") { it.getInt(0) to it.getInt(1) }.first()
        val (attempts, avg) = list("SELECT COUNT(*), AVG(score) FROM recordings") {
            it.getInt(0) to (if (it.isNull(1)) null else it.getDouble(1)) }.first()
        val days = list("SELECT day FROM daily_streak WHERE minutes > 0 ORDER BY day DESC") { it.getString(0) }
        val fmt = SimpleDateFormat("yyyy-MM-dd", Locale.US)
        val cal = java.util.Calendar.getInstance()
        if (days.firstOrNull() != fmt.format(cal.time)) cal.add(java.util.Calendar.DAY_OF_YEAR, -1)
        var streak = 0
        for (d in days) { if (d == fmt.format(cal.time)) { streak++; cal.add(java.util.Calendar.DAY_OF_YEAR, -1) } else break }
        val today = list("SELECT minutes FROM daily_streak WHERE day = ?", arrayOf(fmt.format(Date()))) { it.getDouble(0) }.firstOrNull() ?: 0.0
        val weak = list("SELECT phoneme, errors, COALESCE(avg_score,0) FROM phoneme_stats WHERE attempts >= 3 ORDER BY errors * 1.0 / attempts DESC, avg_score LIMIT 5") {
            Triple(it.getString(0), it.getInt(1), it.getDouble(2)) }
        val recent = list("SELECT COALESCE(t.title_en, r.text_id), r.score FROM recordings r LEFT JOIN content.texts t ON t.id = r.text_id ORDER BY r.created_at DESC LIMIT 8") {
            it.getString(0) to (if (it.isNull(1)) 0.0 else it.getDouble(1)) }
        return ProgressSummary(done, started, attempts, avg, streak, today, weak, recent)
    }

    // ---- settings ------------------------------------------------------------------------------
    fun setting(key: String, def: String): String =
        list("SELECT value FROM settings WHERE key = ?", arrayOf(key)) { it.getString(0) }.firstOrNull() ?: def

    fun putSetting(key: String, value: String) =
        db.execSQL("INSERT OR REPLACE INTO settings(key, value) VALUES(?,?)", arrayOf(key, value))

    // ---- helpers -------------------------------------------------------------------------------
    private fun now() = System.currentTimeMillis() / 1000

    private fun <T> list(sql: String, args: Array<String> = emptyArray(), map: (Cursor) -> T): List<T> {
        db.rawQuery(sql, args).use { c ->
            val out = ArrayList<T>(c.count)
            while (c.moveToNext()) out += map(c)
            return out
        }
    }

    companion object {
        fun open(context: Context): AppDatabase {
            val contentFile = installContent(context)
            val userFile = context.getDatabasePath("user.db").apply { parentFile?.mkdirs() }
            val db = SQLiteDatabase.openDatabase(userFile.path, null,
                SQLiteDatabase.CREATE_IF_NECESSARY or SQLiteDatabase.OPEN_READWRITE)
            db.disableWriteAheadLogging()
            db.setForeignKeyConstraintsEnabled(true)
            // ATTACH must run outside a transaction. The file itself is read-only (setReadOnly).
            db.execSQL("ATTACH DATABASE ? AS content", arrayOf(contentFile.path))
            applyUserSchema(context, db)
            return AppDatabase(db)
        }

        /** Copies assets/content.db to filesDir when missing or when app/asset version changed. */
        private fun installContent(context: Context): File {
            val target = File(context.filesDir, "content.db")
            val prefs = context.getSharedPreferences("content", Context.MODE_PRIVATE)
            val stamp = context.assets.openFd(CONTENT_ASSET).use { "${appVersion(context)}:${it.length}" }
            if (!target.exists() || prefs.getString("stamp", null) != stamp) {
                val tmp = File(context.filesDir, "content.db.tmp")
                context.assets.open(CONTENT_ASSET).use { i -> tmp.outputStream().use { o -> i.copyTo(o) } }
                target.delete()
                check(tmp.renameTo(target)) { "cannot install content.db" }
                target.setReadOnly()
                prefs.edit().putString("stamp", stamp).apply()
            }
            return target
        }

        @Suppress("DEPRECATION")
        private fun appVersion(c: Context): Long =
            c.packageManager.getPackageInfo(c.packageName, 0).let { if (android.os.Build.VERSION.SDK_INT >= 28) it.longVersionCode else it.versionCode.toLong() }

        private fun applyUserSchema(context: Context, db: SQLiteDatabase) {
            val exists = db.rawQuery("SELECT 1 FROM sqlite_master WHERE type='table' AND name='user_meta'", null).use { it.moveToFirst() }
            if (exists) return
            val sql = context.assets.open(USER_SCHEMA_ASSET).bufferedReader().readText()
                .lines().filterNot { it.trimStart().startsWith("--") }
                .joinToString("\n") { it.substringBefore(" -- ") }
            db.beginTransaction()
            try {
                for (stmt in sql.split(';').map { it.trim() }.filter { it.isNotEmpty() }) {
                    if (stmt.startsWith("PRAGMA", ignoreCase = true)) continue
                    db.execSQL(stmt)
                }
                db.execSQL("INSERT INTO user_meta(key, value) VALUES('schema_version','1')")
                db.setTransactionSuccessful()
            } finally { db.endTransaction() }
        }
    }
}
