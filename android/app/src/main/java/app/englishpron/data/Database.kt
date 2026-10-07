package app.englishpron.data

import android.content.Context
import android.database.Cursor
import android.database.sqlite.SQLiteDatabase
import app.englishpron.engine.LiveState
import org.json.JSONObject
import java.io.File
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale
import kotlin.math.max
import kotlin.math.min

private const val CONTENT_ASSET = "content.db"
private const val USER_SCHEMA_ASSET = "user_schema.sql"
private const val PENDING_SQL = "EXISTS(SELECT 1 FROM processing_jobs j WHERE j.text_id = t.id AND j.status IN ('queued','processing'))"
private const val LAST_SCORE_SQL = "(SELECT CASE WHEN r.kind = 'reading' THEN r.score ELSE (SELECT AVG(r2.score) FROM recordings r2 WHERE r2.id IN " +
    "(SELECT MAX(id) FROM recordings WHERE text_id = t.id AND kind = 'paragraph' AND score IS NOT NULL GROUP BY paragraph_index)) END " +
    "FROM recordings r WHERE r.text_id = t.id AND r.kind IN ('reading','paragraph') ORDER BY r.created_at DESC, r.id DESC LIMIT 1)"

/**
 * user.db (read/write) with content.db ATTACHed as `content`. Non-WAL mode keeps a single
 * connection, so the ATTACH is visible to every query. All methods are blocking: call from IO.
 */
class AppDatabase private constructor(internal val db: SQLiteDatabase) {

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
            "COALESCE(p.status,'new'), p.best_score, $PENDING_SQL, $LAST_SCORE_SQL, p.best_coverage FROM content.texts t LEFT JOIN progress p ON p.text_id = t.id " +
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
        c.getInt(9) != 0, if (c.isNull(10)) null else c.getDouble(10), if (c.isNull(11)) null else c.getDouble(11),
    )

    fun text(id: String): TextFull? = list(
        "SELECT t.id, t.level, t.genre, t.title_en, t.title_ru, t.description_ru, t.word_count, " +
            "COALESCE(p.status,'new'), p.best_score, $PENDING_SQL, $LAST_SCORE_SQL, p.best_coverage, t.body FROM content.texts t " +
            "LEFT JOIN progress p ON p.text_id = t.id WHERE t.id = ?", arrayOf(id)
    ) { TextFull(textItem(it), it.getString(12)) }.firstOrNull()

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

    // ---- reading attempts + background processing queue ------------------------------------------
    /**
     * Saves a finished reading (WAV already written) with its quick live result and coverage, updates the text's
     * best coverage / status (done at >= 90 %), the resume position, XP and daily activity, and, if [enqueue],
     * queues the heavy assessment (one job per paragraph in paragraph mode).
     */
    fun saveReading(textId: String, kind: String, durationMs: Long, wavPath: String, audioSeconds: Double,
                    reference: String, liveJson: String?, enqueue: Boolean, m: ReadingMeta): SaveOutcome {
        db.beginTransaction()
        try {
            val t = now()
            val prev = list("SELECT status, best_coverage FROM progress WHERE text_id = ?", arrayOf(textId)) {
                it.getString(0) to (if (it.isNull(1)) null else it.getDouble(1))
            }.firstOrNull()
            db.execSQL("INSERT INTO recordings(text_id, kind, file_path, duration_ms, score, scores_json, created_at, coverage_pct, " +
                "skipped_count, duration_sec, wpm, paragraph_index, words_read, words_total) VALUES(?,?,?,?,NULL,?,?,?,?,?,?,?,?,?)",
                arrayOf(textId, kind, wavPath, durationMs, liveJson, t, m.coverage, m.skipped, m.durationSec, m.wpm, m.paragraphIndex, m.wordsRead, m.wordsTotal))
            val wc = list("SELECT word_count FROM content.texts WHERE id = ?", arrayOf(textId)) { it.getInt(0) }.firstOrNull() ?: m.wordsTotal
            val textCov = when (kind) {
                "reading" -> m.coverage
                "paragraph" -> min(100.0, paragraphWordsRead(textId) * 100.0 / max(1, wc))
                else -> 0.0
            }
            val best = max(prev?.second ?: 0.0, textCov)
            val doneNow = best >= 90.0
            val wasDone = prev?.first == "done"
            db.execSQL("INSERT OR IGNORE INTO progress(text_id, status, attempts) VALUES(?, 'started', 0)", arrayOf(textId))
            db.execSQL(
                "UPDATE progress SET attempts = attempts + 1, last_opened_at = ?, best_coverage = ?, " +
                    "status = CASE WHEN ? = 1 THEN 'done' WHEN status = 'new' THEN 'started' ELSE status END, " +
                    "completed_at = CASE WHEN ? = 1 AND completed_at IS NULL THEN ? ELSE completed_at END WHERE text_id = ?",
                arrayOf(t, if (kind == "sentence") prev?.second else best, if (doneNow) 1 else 0, if (doneNow) 1 else 0, t, textId))
            when (kind) {
                "reading" -> if (textCov >= 90.0) clearPosition(textId) else setPosition(textId, m.cursor, 0)
                "paragraph" -> if (m.nextParagraph >= m.paragraphCount) clearPosition(textId) else setPosition(textId, 0, m.nextParagraph)
            }
            val newlyDone = doneNow && !wasDone
            val minutes = durationMs / 60000.0
            db.execSQL(
                "INSERT INTO daily_streak(day, minutes, texts_done, goal_met) VALUES(?,?,?,0) " +
                    "ON CONFLICT(day) DO UPDATE SET minutes = minutes + excluded.minutes, texts_done = texts_done + excluded.texts_done",
                arrayOf(today(), minutes, if (newlyDone) 1 else 0))
            db.execSQL(
                "INSERT INTO daily_activity(date, minutes, words, xp) VALUES(?,?,?,0) " +
                    "ON CONFLICT(date) DO UPDATE SET minutes = minutes + excluded.minutes, words = words + excluded.words",
                arrayOf(today(), minutes, m.wordsRead))
            addXp(m.wordsRead, "words:$textId")
            var jobId = -1L
            if (enqueue) {
                db.execSQL("INSERT INTO processing_jobs(text_id, wav_path, audio_seconds, status, progress, created_at, reference, kind, paragraph_index, paragraph_count) " +
                    "VALUES(?,?,?,'queued',0,?,?,?,?,?)", arrayOf(textId, wavPath, audioSeconds, t, reference, kind, m.paragraphIndex, m.paragraphCount))
                jobId = list("SELECT last_insert_rowid()") { it.getLong(0) }.first()
            }
            val ach = checkAchievements()
            db.setTransactionSuccessful()
            return SaveOutcome(jobId, best, newlyDone, m.wordsRead, ach)
        } finally { db.endTransaction() }
    }

    fun activeJobs(): List<JobRow> = list(
        "SELECT j.id, j.text_id, COALESCE(t.title_en, j.text_id), j.status, j.progress, j.eta_sec, j.audio_seconds, " +
            "j.reference, j.wav_path, j.kind, j.paragraph_index, COALESCE(j.paragraph_count,0) FROM processing_jobs j LEFT JOIN content.texts t ON t.id = j.text_id " +
            "WHERE j.status IN ('queued','processing') ORDER BY j.id"
    ) { JobRow(it.getLong(0), it.getString(1), it.getString(2), it.getString(3), it.getDouble(4),
        if (it.isNull(5)) -1.0 else it.getDouble(5), it.getDouble(6), it.getString(7), it.getString(8), it.getString(9),
        if (it.isNull(10)) null else it.getInt(10), it.getInt(11)) }

    fun jobStatus(id: Long): String? = list("SELECT status FROM processing_jobs WHERE id = ?", arrayOf(id.toString())) { it.getString(0) }.firstOrNull()

    fun jobStart(id: Long) = db.execSQL("UPDATE processing_jobs SET status='processing', progress=0, eta_sec=NULL, error=NULL WHERE id=? AND status IN ('queued','processing')", arrayOf(id))

    fun jobProgress(id: Long, progress: Double, etaSec: Double) =
        db.execSQL("UPDATE processing_jobs SET progress=?, eta_sec=? WHERE id=? AND status='processing'", arrayOf(progress, etaSec, id))

    fun jobFail(id: Long, error: String) = db.execSQL(
        "UPDATE processing_jobs SET status='failed', error=?, finished_at=? WHERE id=? AND status IN ('queued','processing')", arrayOf(error, now(), id))

    /** Returns false when the job was not active any more. */
    fun jobCancel(id: Long): Boolean {
        db.execSQL("UPDATE processing_jobs SET status='cancelled', finished_at=? WHERE id=? AND status IN ('queued','processing')", arrayOf(now(), id))
        return list("SELECT changes()") { it.getInt(0) }.first() > 0
    }

    fun jobRequeue(id: Long) = db.execSQL("UPDATE processing_jobs SET status='queued', progress=0, eta_sec=NULL WHERE id=? AND status='processing'", arrayOf(id))

    /** Stores the assessment, updates the recording, best score and phoneme statistics. */
    fun jobDone(job: JobRow, resultJson: String, r: AssessmentUi) {
        db.beginTransaction()
        try {
            db.execSQL("UPDATE processing_jobs SET status='done', progress=1, eta_sec=0, result_json=?, error=NULL, finished_at=? WHERE id=? AND status='processing'",
                arrayOf(resultJson, now(), job.id))
            if (list("SELECT changes()") { it.getInt(0) }.first() == 0) return  // cancelled meanwhile
            db.execSQL("UPDATE recordings SET score=?, scores_json=? WHERE file_path=? AND text_id=?",
                arrayOf(r.overall, resultJson, job.wavPath, job.textId))
            if (job.kind == "reading") db.execSQL(
                "UPDATE progress SET best_score = MAX(COALESCE(best_score,0), ?) WHERE text_id = ?", arrayOf(r.overall, job.textId))
            else if (job.kind == "paragraph") db.execSQL(
                "UPDATE progress SET best_score = MAX(COALESCE(best_score,0), COALESCE((SELECT AVG(score) FROM recordings WHERE id IN " +
                    "(SELECT MAX(id) FROM recordings WHERE text_id = ? AND kind = 'paragraph' AND score IS NOT NULL GROUP BY paragraph_index)), 0)) WHERE text_id = ?",
                arrayOf(job.textId, job.textId))
            if (r.overall >= 80 && job.kind != "sentence") {
                val wr = list("SELECT COALESCE(words_read,0) FROM recordings WHERE file_path = ? AND text_id = ?", arrayOf(job.wavPath, job.textId)) { it.getInt(0) }.firstOrNull() ?: 0
                addXp(max(5, wr / 4), "bonus:${job.textId}")
            }
            autoAddWeakWords(job.textId, r)
            for (w in r.words) for (p in w.phonemes) {
                val s = p.score ?: continue
                val err = if (p.substituted || s < 60) 1 else 0
                db.execSQL(
                    "INSERT INTO phoneme_stats(phoneme, attempts, errors, avg_score, updated_at) VALUES(?,?,?,?,?) " +
                        "ON CONFLICT(phoneme) DO UPDATE SET avg_score = (COALESCE(avg_score,0) * attempts + ?) / (attempts + 1), " +
                        "attempts = attempts + 1, errors = errors + ?, updated_at = ?",
                    arrayOf(p.ipa, 1, err, s, now(), s, err, now()))
            }
            checkAchievements()
            db.setTransactionSuccessful()
        } finally { db.endTransaction() }
    }

    fun latestReading(textId: String): TextReading? {
        val newest = list("SELECT kind FROM recordings WHERE text_id = ? AND kind IN ('reading','paragraph') AND file_path != '' ORDER BY created_at DESC, id DESC LIMIT 1",
            arrayOf(textId)) { it.getString(0) }.firstOrNull() ?: return null
        return if (newest == "paragraph") paragraphReading(textId) else latestWhole(textId)
    }

    /** Latest whole-text reading: its WAV, the quick live marks and (when done) the full assessment. */
    private fun latestWhole(textId: String): TextReading? = list(
        "SELECT r.id, r.file_path, r.scores_json, (SELECT j.status FROM processing_jobs j WHERE j.wav_path = r.file_path ORDER BY j.id DESC LIMIT 1) " +
            "FROM recordings r WHERE r.text_id = ? AND r.kind = 'reading' AND r.file_path != '' ORDER BY r.created_at DESC, r.id DESC LIMIT 1",
        arrayOf(textId)
    ) { c ->
        val js = if (c.isNull(2)) null else c.getString(2)
        var result: AssessmentUi? = null
        var live: LiveState? = null
        if (js != null) {
            if (js.contains("\"scores\"")) result = try { AssessmentUi.parse(js) } catch (_: Exception) { null }
            else live = LiveState.parse(js)
        }
        val st = if (c.isNull(3)) null else c.getString(3)
        TextReading(c.getLong(0), c.getString(1), live, result, st == "queued" || st == "processing", st == "failed")
    }.firstOrNull()

    fun attempts(textId: String): List<AttemptItem> = list(
        "SELECT r.id, r.created_at, COALESCE(r.duration_ms,0), r.score, r.kind, r.file_path, " +
            "(SELECT j.status FROM processing_jobs j WHERE j.wav_path = r.file_path ORDER BY j.id DESC LIMIT 1) " +
            "FROM recordings r WHERE r.text_id = ? ORDER BY r.created_at DESC, r.id DESC LIMIT 30", arrayOf(textId)
    ) { AttemptItem(it.getLong(0), it.getLong(1), it.getLong(2), if (it.isNull(3)) null else it.getDouble(3),
        it.getString(4), it.getString(5), if (it.isNull(6)) null else it.getString(6)) }

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
        val recent = list("SELECT COALESCE(t.title_en, r.text_id), r.score FROM recordings r LEFT JOIN content.texts t ON t.id = r.text_id WHERE r.score IS NOT NULL ORDER BY r.created_at DESC LIMIT 8") {
            it.getString(0) to (if (it.isNull(1)) 0.0 else it.getDouble(1)) }
        return ProgressSummary(done, started, attempts, avg, streak, today, weak, recent)
    }

    // ---- settings ------------------------------------------------------------------------------
    fun setting(key: String, def: String): String =
        list("SELECT value FROM settings WHERE key = ?", arrayOf(key)) { it.getString(0) }.firstOrNull() ?: def

    fun putSetting(key: String, value: String) =
        db.execSQL("INSERT OR REPLACE INTO settings(key, value) VALUES(?,?)", arrayOf(key, value))

    // ---- helpers -------------------------------------------------------------------------------
    internal fun now() = System.currentTimeMillis() / 1000

    internal fun <T> list(sql: String, args: Array<String> = emptyArray(), map: (Cursor) -> T): List<T> {
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
            applyQueueSchema(db)
            applyV2Schema(db)
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

        /** The background queue table lives in app code (content/db/user_schema.sql is not changed). */
        private fun applyQueueSchema(db: SQLiteDatabase) {
            db.execSQL("CREATE TABLE IF NOT EXISTS processing_jobs (" +
                "id INTEGER PRIMARY KEY AUTOINCREMENT, text_id TEXT NOT NULL, wav_path TEXT NOT NULL, " +
                "audio_seconds REAL NOT NULL DEFAULT 0, " +
                "status TEXT NOT NULL DEFAULT 'queued' CHECK (status IN ('queued','processing','done','failed','cancelled')), " +
                "progress REAL NOT NULL DEFAULT 0, eta_sec REAL, result_json TEXT, error TEXT, " +
                "created_at INTEGER NOT NULL, finished_at INTEGER, " +
                "reference TEXT NOT NULL DEFAULT '', kind TEXT NOT NULL DEFAULT 'reading')")
            db.execSQL("CREATE INDEX IF NOT EXISTS idx_jobs_status ON processing_jobs(status, id)")
            db.execSQL("CREATE INDEX IF NOT EXISTS idx_jobs_text ON processing_jobs(text_id)")
            // Process death while a job was running: run it again.
            db.execSQL("UPDATE processing_jobs SET status='queued', progress=0, eta_sec=NULL WHERE status='processing'")
        }

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
