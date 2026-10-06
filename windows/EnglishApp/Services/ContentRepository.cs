using System.Globalization;
using System.Text;
using EnglishApp.Models;
using EnglishApp.Native;
using Microsoft.Data.Sqlite;

namespace EnglishApp.Services;

/// <summary>
/// Opens user.db (read/write), ATTACHes content.db read-only as "content" and applies user_schema.sql
/// on first run. All access is synchronous on one connection and serialized by a lock.
/// </summary>
public sealed class ContentRepository : IDisposable
{
    private readonly SqliteConnection _c;
    private readonly object _lock = new();

    public bool ContentAvailable { get; }
    public string? ContentError { get; }

    public ContentRepository(string userDbPath, string contentDbPath, string userSchemaPath)
    {
        Directory.CreateDirectory(Path.GetDirectoryName(userDbPath)!);
        // "file:" URI enables SQLITE_OPEN_URI so the ATTACH below may use ?mode=ro.
        _c = new SqliteConnection("Data Source=" + new Uri(userDbPath).AbsoluteUri);
        _c.Open();
        Exec("PRAGMA foreign_keys = ON");

        if (ScalarLong("SELECT count(*) FROM sqlite_master WHERE type='table' AND name='user_meta'") == 0)
        {
            if (!File.Exists(userSchemaPath)) throw new FileNotFoundException("user_schema.sql not found", userSchemaPath);
            Exec(File.ReadAllText(userSchemaPath, Encoding.UTF8));
            Exec("INSERT OR REPLACE INTO user_meta(key,value) VALUES('schema_version','1')");
        }

        Exec(JobsSchema);

        try
        {
            if (!File.Exists(contentDbPath)) throw new FileNotFoundException("content.db not found", contentDbPath);
            using var cmd = _c.CreateCommand();
            cmd.CommandText = "ATTACH DATABASE $p AS content";
            cmd.Parameters.AddWithValue("$p", new Uri(contentDbPath).AbsoluteUri + "?mode=ro");
            cmd.ExecuteNonQuery();
            ContentAvailable = true;
        }
        catch (Exception ex)
        {
            ContentAvailable = false;
            ContentError = ex.Message;
        }
    }

    private const string JobsSchema =
        "CREATE TABLE IF NOT EXISTS processing_jobs (" +
        "id INTEGER PRIMARY KEY AUTOINCREMENT, text_id TEXT NOT NULL, wav_path TEXT NOT NULL, " +
        "audio_seconds REAL NOT NULL DEFAULT 0, " +
        "status TEXT NOT NULL DEFAULT 'queued' CHECK (status IN ('queued','processing','done','failed','cancelled')), " +
        "progress REAL NOT NULL DEFAULT 0, eta_sec REAL, result_json TEXT, error TEXT, " +
        "created_at INTEGER NOT NULL, finished_at INTEGER); " +
        "CREATE INDEX IF NOT EXISTS idx_jobs_status ON processing_jobs(status, id); " +
        "CREATE INDEX IF NOT EXISTS idx_jobs_wav ON processing_jobs(wav_path);";

    // ---------- helpers ----------

    private int Exec(string sql, params (string, object?)[] args)
    {
        lock (_lock)
        {
            using var cmd = _c.CreateCommand();
            cmd.CommandText = sql;
            foreach (var (k, v) in args) cmd.Parameters.AddWithValue(k, v ?? DBNull.Value);
            return cmd.ExecuteNonQuery();
        }
    }

    private long ScalarLong(string sql, params (string, object?)[] args)
    {
        lock (_lock)
        {
            using var cmd = _c.CreateCommand();
            cmd.CommandText = sql;
            foreach (var (k, v) in args) cmd.Parameters.AddWithValue(k, v ?? DBNull.Value);
            var o = cmd.ExecuteScalar();
            return o is null or DBNull ? 0 : Convert.ToInt64(o, CultureInfo.InvariantCulture);
        }
    }

    private List<T> Query<T>(string sql, Func<SqliteDataReader, T> map, params (string, object?)[] args)
    {
        var list = new List<T>();
        if (sql.Contains("content.") && !ContentAvailable) return list;
        lock (_lock)
        {
            using var cmd = _c.CreateCommand();
            cmd.CommandText = sql;
            foreach (var (k, v) in args) cmd.Parameters.AddWithValue(k, v ?? DBNull.Value);
            using var r = cmd.ExecuteReader();
            while (r.Read()) list.Add(map(r));
        }
        return list;
    }

    private static string S(SqliteDataReader r, int i) => r.IsDBNull(i) ? "" : r.GetString(i);
    private static long Now => DateTimeOffset.UtcNow.ToUnixTimeSeconds();
    private static string Today => DateTime.Now.ToString("yyyy-MM-dd", CultureInfo.InvariantCulture);

    // ---------- settings ----------

    public string? GetSetting(string key)
    {
        var l = Query("SELECT value FROM settings WHERE key=$k", r => r.GetString(0), ("$k", key));
        return l.Count == 0 ? null : l[0];
    }

    public void SetSetting(string key, string value) =>
        Exec("INSERT INTO settings(key,value) VALUES($k,$v) ON CONFLICT(key) DO UPDATE SET value=excluded.value",
            ("$k", key), ("$v", value));

    // ---------- library ----------

    private const string TextSelect =
        "SELECT t.id,t.level,t.genre,t.title_en,t.title_ru,t.description_ru,t.word_count," +
        "COALESCE(p.status,'new'),p.best_score FROM content.texts t LEFT JOIN progress p ON p.text_id=t.id ";

    private static TextSummary MapText(SqliteDataReader r) => new(S(r, 0), S(r, 1), S(r, 2), S(r, 3), S(r, 4), S(r, 5),
        r.GetInt32(6), S(r, 7), r.IsDBNull(8) ? null : r.GetDouble(8));

    public IReadOnlyList<string> GetGenres() =>
        Query("SELECT DISTINCT genre FROM content.texts ORDER BY genre", r => r.GetString(0));

    /// <summary>Builds a safe FTS5 query: every token quoted, prefix match, AND semantics.</summary>
    public static string? BuildFtsQuery(string? input)
    {
        if (string.IsNullOrWhiteSpace(input)) return null;
        var parts = new List<string>();
        var sb = new StringBuilder();
        void Flush() { if (sb.Length > 0) { parts.Add("\"" + sb + "\"*"); sb.Clear(); } }
        foreach (var ch in input) { if (char.IsLetterOrDigit(ch)) sb.Append(ch); else Flush(); }
        Flush();
        return parts.Count == 0 ? null : string.Join(" ", parts);
    }

    public IReadOnlyList<TextSummary> GetTexts(string? level, string? genre, string? status, string? search)
    {
        var sql = new StringBuilder(TextSelect + "WHERE 1=1 ");
        var args = new List<(string, object?)>();
        if (!string.IsNullOrEmpty(level)) { sql.Append("AND t.level=$level "); args.Add(("$level", level)); }
        if (!string.IsNullOrEmpty(genre)) { sql.Append("AND t.genre=$genre "); args.Add(("$genre", genre)); }
        if (!string.IsNullOrEmpty(status)) { sql.Append("AND COALESCE(p.status,'new')=$status "); args.Add(("$status", status)); }
        var fts = BuildFtsQuery(search);
        if (fts != null)
        {
            sql.Append("AND t.rowid IN (SELECT rowid FROM content.texts_fts WHERE texts_fts MATCH $fts) ");
            args.Add(("$fts", fts));
        }
        sql.Append("ORDER BY t.sort_order");
        return Query(sql.ToString(), MapText, args.ToArray());
    }

    public TextSummary? GetTextSummary(string id)
    {
        var l = Query(TextSelect + "WHERE t.id=$id", MapText, ("$id", id));
        return l.Count == 0 ? null : l[0];
    }

    public TextDetail? GetText(string id)
    {
        var l = Query("SELECT id,level,genre,title_en,title_ru,description_ru,body,word_count FROM content.texts WHERE id=$id",
            r => new TextDetail(S(r, 0), S(r, 1), S(r, 2), S(r, 3), S(r, 4), S(r, 5), S(r, 6), r.GetInt32(7)), ("$id", id));
        return l.Count == 0 ? null : l[0];
    }

    public IReadOnlyList<VocabEntry> GetVocabulary(string textId) =>
        Query("SELECT word,ipa_us,ipa_uk,translation_ru FROM content.text_vocabulary WHERE text_id=$id ORDER BY position",
            r => new VocabEntry(S(r, 0), S(r, 1), S(r, 2), S(r, 3)), ("$id", textId));

    public IReadOnlyList<FocusSound> GetFocusSounds(string textId)
    {
        var rows = Query("SELECT id,sound,tip_ru FROM content.text_focus_sounds WHERE text_id=$id ORDER BY position",
            r => (Id: r.GetInt64(0), Sound: S(r, 1), Tip: S(r, 2)), ("$id", textId));
        return rows.Select(x => new FocusSound(x.Sound, x.Tip,
            Query("SELECT example FROM content.text_focus_examples WHERE focus_id=$f ORDER BY position",
                r => r.GetString(0), ("$f", x.Id)))).ToList();
    }

    /// <summary>Last opened, not finished text.</summary>
    public TextSummary? GetContinueText()
    {
        var l = Query(TextSelect + "WHERE p.status='started' ORDER BY p.last_opened_at DESC LIMIT 1", MapText);
        return l.Count == 0 ? null : l[0];
    }

    /// <summary>Deterministic "text of the day" (rotates through the library by date).</summary>
    public TextSummary? GetTextOfTheDay()
    {
        var total = (int)ScalarLongSafe("SELECT count(*) FROM content.texts");
        if (total == 0) return null;
        var idx = (DateTime.Now.Year * 366 + DateTime.Now.DayOfYear) % total;
        var l = Query(TextSelect + "ORDER BY t.sort_order LIMIT 1 OFFSET $o", MapText, ("$o", idx));
        return l.Count == 0 ? null : l[0];
    }

    private long ScalarLongSafe(string sql) => sql.Contains("content.") && !ContentAvailable ? 0 : ScalarLong(sql);

    // ---------- sounds ----------

    public IReadOnlyList<SoundCard> GetSounds() =>
        Query("SELECT id,slug,name_ru,ipa,difficulty,json FROM content.sounds ORDER BY sort_order",
            r => new SoundCard(S(r, 0), S(r, 1), S(r, 2), S(r, 3), S(r, 4), S(r, 5)));

    // ---------- progress ----------

    public void MarkOpened(string textId) =>
        Exec("INSERT INTO progress(text_id,status,last_opened_at) VALUES($id,'started',$t) " +
             "ON CONFLICT(text_id) DO UPDATE SET last_opened_at=$t, status=CASE WHEN status='new' THEN 'started' ELSE status END",
            ("$id", textId), ("$t", Now));

    public long SaveAttempt(string textId, string wavPath, int durationMs, double score, string resultJson, double minutes)
    {
        long id;
        lock (_lock)
        {
            using var tx = _c.BeginTransaction();
            using (var cmd = _c.CreateCommand())
            {
                cmd.Transaction = tx;
                cmd.CommandText = "INSERT INTO recordings(text_id,kind,file_path,duration_ms,score,scores_json,created_at) " +
                                  "VALUES($t,'reading',$f,$d,$s,$j,$c); SELECT last_insert_rowid()";
                cmd.Parameters.AddWithValue("$t", textId);
                cmd.Parameters.AddWithValue("$f", wavPath);
                cmd.Parameters.AddWithValue("$d", durationMs);
                cmd.Parameters.AddWithValue("$s", score);
                cmd.Parameters.AddWithValue("$j", resultJson);
                cmd.Parameters.AddWithValue("$c", Now);
                id = Convert.ToInt64(cmd.ExecuteScalar(), CultureInfo.InvariantCulture);
            }
            using (var cmd = _c.CreateCommand())
            {
                cmd.Transaction = tx;
                cmd.CommandText =
                    "INSERT INTO progress(text_id,status,best_score,attempts,last_opened_at,completed_at) " +
                    "VALUES($t, CASE WHEN $s>=70 THEN 'done' ELSE 'started' END, $s, 1, $n, CASE WHEN $s>=70 THEN $n END) " +
                    "ON CONFLICT(text_id) DO UPDATE SET attempts=attempts+1, last_opened_at=$n, " +
                    "best_score=MAX(COALESCE(best_score,0),$s), " +
                    "status=CASE WHEN status='done' OR $s>=70 THEN 'done' ELSE 'started' END, " +
                    "completed_at=CASE WHEN completed_at IS NULL AND $s>=70 THEN $n ELSE completed_at END";
                cmd.Parameters.AddWithValue("$t", textId);
                cmd.Parameters.AddWithValue("$s", score);
                cmd.Parameters.AddWithValue("$n", Now);
                cmd.ExecuteNonQuery();
            }
            using (var cmd = _c.CreateCommand())
            {
                cmd.Transaction = tx;
                cmd.CommandText =
                    "INSERT INTO daily_streak(day,minutes,texts_done,goal_met) VALUES($d,$m,$td,0) " +
                    "ON CONFLICT(day) DO UPDATE SET minutes=minutes+$m, texts_done=texts_done+$td";
                cmd.Parameters.AddWithValue("$d", Today);
                cmd.Parameters.AddWithValue("$m", minutes);
                cmd.Parameters.AddWithValue("$td", score >= 70 ? 1 : 0);
                cmd.ExecuteNonQuery();
            }
            tx.Commit();
        }
        return id;
    }

    public void RecordPhonemes(AssessmentResult result)
    {
        var acc = new Dictionary<string, (int n, int err, double sum)>();
        foreach (var w in result.Words)
            foreach (var p in w.Phonemes)
            {
                if (p.Score is not double s || string.IsNullOrEmpty(p.Ipa)) continue;
                acc.TryGetValue(p.Ipa, out var a);
                acc[p.Ipa] = (a.n + 1, a.err + (s < 60 ? 1 : 0), a.sum + s);
            }
        foreach (var (ipa, a) in acc)
            Exec("INSERT INTO phoneme_stats(phoneme,attempts,errors,avg_score,updated_at) VALUES($p,$n,$e,$avg,$t) " +
                 "ON CONFLICT(phoneme) DO UPDATE SET " +
                 "avg_score=(COALESCE(avg_score,0)*attempts+$sum)/(attempts+$n), attempts=attempts+$n, errors=errors+$e, updated_at=$t",
                ("$p", ipa), ("$n", a.n), ("$e", a.err), ("$avg", a.sum / a.n), ("$sum", a.sum), ("$t", Now));
    }

    public IReadOnlyList<PhonemeStat> GetWorstPhonemes(int limit = 8) =>
        Query("SELECT phoneme,attempts,errors,COALESCE(avg_score,0) FROM phoneme_stats WHERE attempts>=3 " +
              "ORDER BY (errors*1.0/attempts) DESC, attempts DESC LIMIT $l",
            r => new PhonemeStat(S(r, 0), r.GetInt32(1), r.GetInt32(2), r.GetDouble(3)), ("$l", limit));

    public int GetStreak()
    {
        var days = Query("SELECT day FROM daily_streak WHERE minutes>0 OR texts_done>0 ORDER BY day DESC",
            r => DateTime.ParseExact(r.GetString(0), "yyyy-MM-dd", CultureInfo.InvariantCulture));
        var expect = DateTime.Now.Date;
        if (days.Count > 0 && days[0] < expect) expect = expect.AddDays(-1);   // today not yet practised
        int streak = 0;
        foreach (var d in days)
        {
            if (d.Date == expect) { streak++; expect = expect.AddDays(-1); }
            else if (d.Date < expect) break;
        }
        return streak;
    }

    public ProgressStats GetStats()
    {
        var total = (int)ScalarLongSafe("SELECT count(*) FROM content.texts");
        var done = (int)ScalarLong("SELECT count(*) FROM progress WHERE status='done'");
        var started = (int)ScalarLong("SELECT count(*) FROM progress WHERE status='started'");
        var attempts = (int)ScalarLong("SELECT COALESCE(sum(attempts),0) FROM progress");
        var avg = Query("SELECT COALESCE(avg(best_score),0) FROM progress WHERE best_score IS NOT NULL", r => r.GetDouble(0))[0];
        var minutes = Query("SELECT COALESCE(sum(minutes),0) FROM daily_streak", r => r.GetDouble(0))[0];
        var words = (int)ScalarLong("SELECT count(*) FROM saved_words");
        var due = (int)ScalarLong("SELECT count(*) FROM saved_words WHERE due_at<=$n", ("$n", Now));
        return new ProgressStats(total, done, started, attempts, avg, GetStreak(), minutes, words, due);
    }

    // ---------- vocabulary / SRS ----------

    private static SavedWord MapWord(SqliteDataReader r) => new(r.GetInt64(0), S(r, 1), S(r, 2), S(r, 3), S(r, 4),
        r.GetDouble(5), r.GetDouble(6), r.GetInt64(7), r.GetInt32(8), r.GetInt32(9));

    private const string WordSelect =
        "SELECT id,word,ipa,translation_ru,source_text_id,ease,interval_days,due_at,reps,lapses FROM saved_words ";

    public void SaveWord(string word, string? ipa, string? translation, string? sourceTextId) =>
        Exec("INSERT OR IGNORE INTO saved_words(word,ipa,translation_ru,source_text_id,due_at,created_at) VALUES($w,$i,$t,$s,$n,$n)",
            ("$w", word.ToLowerInvariant()), ("$i", ipa), ("$t", translation), ("$s", sourceTextId), ("$n", Now));

    public bool IsWordSaved(string word) =>
        ScalarLong("SELECT count(*) FROM saved_words WHERE word=$w", ("$w", word.ToLowerInvariant())) > 0;

    public void DeleteWord(long id) => Exec("DELETE FROM saved_words WHERE id=$id", ("$id", id));

    public IReadOnlyList<SavedWord> GetSavedWords() => Query(WordSelect + "ORDER BY created_at DESC", MapWord);

    public IReadOnlyList<SavedWord> GetDueWords() =>
        Query(WordSelect + "WHERE due_at<=$n ORDER BY due_at", MapWord, ("$n", Now));

    /// <summary>SM-2 style update. quality: 0..5 (&lt;3 = lapse).</summary>
    public void ReviewWord(SavedWord w, int quality)
    {
        double ease = w.Ease, interval; int reps = w.Reps, lapses = w.Lapses;
        if (quality < 3) { reps = 0; interval = 1; lapses++; }
        else
        {
            reps++;
            interval = reps == 1 ? 1 : reps == 2 ? 6 : Math.Round(Math.Max(1, w.IntervalDays) * ease);
        }
        ease = Math.Max(1.3, ease + 0.1 - (5 - quality) * (0.08 + (5 - quality) * 0.02));
        Exec("UPDATE saved_words SET ease=$e,interval_days=$i,due_at=$d,reps=$r,lapses=$l WHERE id=$id",
            ("$e", ease), ("$i", interval), ("$d", Now + (long)(interval * 86400)), ("$r", reps), ("$l", lapses), ("$id", w.Id));
    }

    // ---------- background processing queue ----------

    /// <summary>
    /// Called when a reading ends: stores the recording (live marks only, no score yet), marks the text read
    /// (progress/streak) and enqueues the heavy assessment. Returns the job id.
    /// </summary>
    public long AddPendingReading(string textId, string wavPath, double audioSeconds, string? liveMarks)
    {
        long jobId;
        string? liveJson = liveMarks == null ? null : System.Text.Json.JsonSerializer.Serialize(new { live = liveMarks });
        lock (_lock)
        {
            using var tx = _c.BeginTransaction();
            using (var cmd = _c.CreateCommand())
            {
                cmd.Transaction = tx;
                cmd.CommandText = "INSERT INTO recordings(text_id,kind,file_path,duration_ms,score,scores_json,created_at) " +
                                  "VALUES($t,'reading',$f,$d,NULL,$j,$c)";
                cmd.Parameters.AddWithValue("$t", textId);
                cmd.Parameters.AddWithValue("$f", wavPath);
                cmd.Parameters.AddWithValue("$d", (int)(audioSeconds * 1000));
                cmd.Parameters.AddWithValue("$j", (object?)liveJson ?? DBNull.Value);
                cmd.Parameters.AddWithValue("$c", Now);
                cmd.ExecuteNonQuery();
            }
            using (var cmd = _c.CreateCommand())
            {
                cmd.Transaction = tx;
                cmd.CommandText =
                    "INSERT INTO progress(text_id,status,attempts,last_opened_at,completed_at) VALUES($t,'done',1,$n,$n) " +
                    "ON CONFLICT(text_id) DO UPDATE SET attempts=attempts+1, last_opened_at=$n, status='done', " +
                    "completed_at=COALESCE(completed_at,$n)";
                cmd.Parameters.AddWithValue("$t", textId);
                cmd.Parameters.AddWithValue("$n", Now);
                cmd.ExecuteNonQuery();
            }
            using (var cmd = _c.CreateCommand())
            {
                cmd.Transaction = tx;
                cmd.CommandText =
                    "INSERT INTO daily_streak(day,minutes,texts_done,goal_met) VALUES($d,$m,1,0) " +
                    "ON CONFLICT(day) DO UPDATE SET minutes=minutes+$m, texts_done=texts_done+1";
                cmd.Parameters.AddWithValue("$d", Today);
                cmd.Parameters.AddWithValue("$m", audioSeconds / 60.0);
                cmd.ExecuteNonQuery();
            }
            using (var cmd = _c.CreateCommand())
            {
                cmd.Transaction = tx;
                cmd.CommandText = "INSERT INTO processing_jobs(text_id,wav_path,audio_seconds,status,created_at) " +
                                  "VALUES($t,$f,$a,'queued',$c); SELECT last_insert_rowid()";
                cmd.Parameters.AddWithValue("$t", textId);
                cmd.Parameters.AddWithValue("$f", wavPath);
                cmd.Parameters.AddWithValue("$a", audioSeconds);
                cmd.Parameters.AddWithValue("$c", Now);
                jobId = Convert.ToInt64(cmd.ExecuteScalar(), CultureInfo.InvariantCulture);
            }
            tx.Commit();
        }
        return jobId;
    }

    private static JobRow MapJob(SqliteDataReader r) =>
        new(r.GetInt64(0), S(r, 1), S(r, 2), r.GetDouble(3), S(r, 4));

    private const string JobSelect = "SELECT id,text_id,wav_path,audio_seconds,status FROM processing_jobs ";

    /// <summary>Jobs interrupted by an app exit go back to the queue.</summary>
    public void RequeueInterruptedJobs() =>
        Exec("UPDATE processing_jobs SET status='queued', progress=0, eta_sec=NULL WHERE status='processing'");

    public IReadOnlyList<JobRow> GetActiveJobs() =>
        Query(JobSelect + "WHERE status IN ('queued','processing') ORDER BY id", MapJob);

    public JobRow? NextQueuedJob()
    {
        var l = Query(JobSelect + "WHERE status='queued' ORDER BY id LIMIT 1", MapJob);
        return l.Count == 0 ? null : l[0];
    }

    /// <summary>queued -> processing; false when the job was cancelled in the meantime.</summary>
    public bool StartJob(long id) =>
        Exec("UPDATE processing_jobs SET status='processing', progress=0, eta_sec=NULL WHERE id=$id AND status='queued'", ("$id", id)) > 0;

    public void UpdateJobProgress(long id, double fraction, double etaSec) =>
        Exec("UPDATE processing_jobs SET progress=$p, eta_sec=$e WHERE id=$id AND status='processing'",
            ("$p", fraction), ("$e", etaSec < 0 ? (object?)null : etaSec), ("$id", id));

    /// <summary>Stores the result: job done, recording scored, best score updated.</summary>
    public void CompleteJob(long id, string textId, string wavPath, string resultJson, double score)
    {
        lock (_lock)
        {
            using var tx = _c.BeginTransaction();
            void Run(string sql, params (string, object?)[] args)
            {
                using var cmd = _c.CreateCommand();
                cmd.Transaction = tx;
                cmd.CommandText = sql;
                foreach (var (k, v) in args) cmd.Parameters.AddWithValue(k, v ?? DBNull.Value);
                cmd.ExecuteNonQuery();
            }
            Run("UPDATE processing_jobs SET status='done', progress=1, eta_sec=0, result_json=$j, error=NULL, finished_at=$n WHERE id=$id",
                ("$j", resultJson), ("$n", Now), ("$id", id));
            Run("UPDATE recordings SET score=$s, scores_json=$j WHERE file_path=$f AND kind='reading'",
                ("$s", score), ("$j", resultJson), ("$f", wavPath));
            Run("UPDATE progress SET best_score=MAX(COALESCE(best_score,0),$s) WHERE text_id=$t",
                ("$s", score), ("$t", textId));
            tx.Commit();
        }
    }

    /// <summary>status: failed | cancelled. Only queued/processing jobs are touched; returns false otherwise.</summary>
    public bool EndJob(long id, string status, string? error) =>
        Exec("UPDATE processing_jobs SET status=$s, error=$e, finished_at=$n WHERE id=$id AND status IN ('queued','processing')",
            ("$s", status), ("$e", error), ("$n", Now), ("$id", id)) > 0;

    private List<AttemptInfo> QueryAttempts(string? textId) =>
        Query("SELECT r.id,r.text_id,r.created_at,r.score,r.file_path,r.scores_json," +
              "(SELECT j.status FROM processing_jobs j WHERE j.wav_path=r.file_path ORDER BY j.id DESC LIMIT 1) " +
              "FROM recordings r WHERE r.kind='reading' " + (textId == null ? "" : "AND r.text_id=$t ") +
              "ORDER BY r.created_at, r.id",
            r => new AttemptInfo(r.GetInt64(0), S(r, 1), r.GetInt64(2), r.IsDBNull(3) ? null : r.GetDouble(3), S(r, 4),
                r.IsDBNull(5) ? null : r.GetString(5), r.IsDBNull(6) ? null : r.GetString(6)),
            textId == null ? Array.Empty<(string, object?)>() : new (string, object?)[] { ("$t", textId) });

    /// <summary>Reading attempts of a text, newest first.</summary>
    public IReadOnlyList<AttemptInfo> GetAttempts(string textId)
    {
        var l = QueryAttempts(textId);
        l.Reverse();
        return l;
    }

    /// <summary>Live marks string ('r' read, 's' skipped, '-' other per word) from a pending attempt's scores_json.</summary>
    public static string? ParseLiveMarks(string? json)
    {
        if (string.IsNullOrEmpty(json) || !json.Contains("\"live\"")) return null;
        try
        {
            using var doc = System.Text.Json.JsonDocument.Parse(json);
            return doc.RootElement.ValueKind == System.Text.Json.JsonValueKind.Object &&
                   doc.RootElement.TryGetProperty("live", out var v) && v.ValueKind == System.Text.Json.JsonValueKind.String
                ? v.GetString() : null;
        }
        catch (System.Text.Json.JsonException) { return null; }
    }

    /// <summary>Per-text reading summary for library cards.</summary>
    public IReadOnlyDictionary<string, ReadSummary> GetReadSummaries()
    {
        var result = new Dictionary<string, ReadSummary>();
        foreach (var g in QueryAttempts(null).GroupBy(a => a.TextId))
        {
            var list = g.ToList();   // oldest first
            var last = list[^1];
            var scores = list.Where(a => a.Score != null).Select(a => a.Score!.Value).ToList();
            var marks = ParseLiveMarks(last.ScoresJson);
            result[g.Key] = new ReadSummary(list.Count, scores.Count > 0 ? scores[^1] : null,
                scores.Skip(Math.Max(0, scores.Count - 6)).ToList(), last.Badge, marks?.Count(ch => ch == 's') ?? 0);
        }
        return result;
    }

    public void Dispose() { lock (_lock) _c.Dispose(); }
}
