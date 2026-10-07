using System.Globalization;
using EnglishApp.Models;
using EnglishApp.Native;
using Microsoft.Data.Sqlite;

namespace EnglishApp.Services;

/// <summary>Product v2 data: coverage, reading position, XP, achievements, daily activity, dashboard queries.</summary>
public sealed partial class ContentRepository
{
    private const string V2Tables =
        "CREATE TABLE IF NOT EXISTS reading_position (text_id TEXT PRIMARY KEY, word_index INTEGER NOT NULL DEFAULT 0, " +
        "paragraph_index INTEGER NOT NULL DEFAULT 0, updated_at INTEGER NOT NULL) WITHOUT ROWID; " +
        "CREATE TABLE IF NOT EXISTS xp_log (id INTEGER PRIMARY KEY AUTOINCREMENT, amount INTEGER NOT NULL, " +
        "reason TEXT NOT NULL, created_at INTEGER NOT NULL); " +
        "CREATE TABLE IF NOT EXISTS achievements (id TEXT PRIMARY KEY, unlocked_at INTEGER NOT NULL) WITHOUT ROWID; " +
        "CREATE TABLE IF NOT EXISTS daily_activity (date TEXT PRIMARY KEY, minutes REAL NOT NULL DEFAULT 0, " +
        "words INTEGER NOT NULL DEFAULT 0, xp INTEGER NOT NULL DEFAULT 0) WITHOUT ROWID;";

    private void EnsureV2Schema()
    {
        AddColumn("recordings", "coverage_pct", "REAL");
        AddColumn("recordings", "skipped_count", "INTEGER");
        AddColumn("recordings", "duration_sec", "REAL");
        AddColumn("recordings", "wpm", "REAL");
        AddColumn("recordings", "paragraph_index", "INTEGER");
        AddColumn("recordings", "words_read", "INTEGER");
        AddColumn("recordings", "words_total", "INTEGER");
        AddColumn("processing_jobs", "paragraph_index", "INTEGER");
        AddColumn("processing_jobs", "paragraph_total", "INTEGER");
        AddColumn("progress", "best_coverage", "REAL");
        AddColumn("phoneme_stats", "recent_avg", "REAL");
        Exec(V2Tables);
    }

    private void AddColumn(string table, string column, string type)
    {
        var cols = Query("PRAGMA table_info(" + table + ")", r => r.GetString(1));
        if (!cols.Contains(column, StringComparer.OrdinalIgnoreCase))
            Exec("ALTER TABLE " + table + " ADD COLUMN " + column + " " + type);
    }

    // ---------- coverage ----------

    private sealed record CovRow(int? Para, double? Cov, int? Read, int? Total);

    /// <summary>Coverage 0..100 of the latest reading of a text (whole attempt, or newest attempt per paragraph); null when unknown.</summary>
    public double? ComputeTextCoverage(string textId)
    {
        var rows = Query("SELECT paragraph_index,coverage_pct,words_read,words_total FROM recordings " +
                         "WHERE text_id=$t AND kind='reading' ORDER BY created_at, id",
            r => new CovRow(r.IsDBNull(0) ? null : r.GetInt32(0), r.IsDBNull(1) ? null : r.GetDouble(1),
                r.IsDBNull(2) ? null : r.GetInt32(2), r.IsDBNull(3) ? null : r.GetInt32(3)),
            ("$t", textId));
        if (rows.Count == 0) return null;
        int lastWhole = rows.FindLastIndex(x => x.Para == null);
        var after = rows.Skip(lastWhole + 1).ToList();
        if (after.Count == 0) return rows[lastWhole].Cov;
        if (after.All(x => x.Read == null)) return null;
        var paras = TextParagraphs.Split(GetText(textId)?.Body ?? "");
        var map = new Dictionary<int, CovRow>();
        foreach (var a in after) map[a.Para ?? 0] = a;
        double read = 0, total = 0;
        int n = Math.Max(paras.Count, map.Keys.Max() + 1);
        for (int i = 0; i < n; i++)
        {
            int est = i < paras.Count ? TextParagraphs.WordCount(paras[i].Text) : 0;
            if (map.TryGetValue(i, out var a)) { total += a.Total ?? est; read += a.Read ?? 0; }
            else total += est;
        }
        return total <= 0 ? null : Math.Min(100, 100.0 * read / total);
    }

    /// <summary>Recomputes the text coverage, raises best_coverage and marks the text done at 90%+.</summary>
    public double? UpdateTextCoverage(string textId)
    {
        var cov = ComputeTextCoverage(textId);
        if (cov is not double c) return null;
        Exec("UPDATE progress SET " +
             "status=CASE WHEN status='done' OR MAX(COALESCE(best_coverage,0),$c)>=90 THEN 'done' ELSE 'started' END, " +
             "completed_at=CASE WHEN completed_at IS NULL AND MAX(COALESCE(best_coverage,0),$c)>=90 THEN $n ELSE completed_at END, " +
             "best_coverage=MAX(COALESCE(best_coverage,0),$c) WHERE text_id=$t",
            ("$c", c), ("$n", Now), ("$t", textId));
        return c;
    }

    // ---------- reading position ----------

    public ReadingPosition? GetPosition(string textId)
    {
        var l = Query("SELECT text_id,word_index,paragraph_index FROM reading_position WHERE text_id=$t",
            r => new ReadingPosition(S(r, 0), r.GetInt32(1), r.GetInt32(2)), ("$t", textId));
        return l.Count == 0 ? null : l[0];
    }

    public void SavePosition(string textId, int wordIndex, int paragraphIndex) =>
        Exec("INSERT INTO reading_position(text_id,word_index,paragraph_index,updated_at) VALUES($t,$w,$p,$n) " +
             "ON CONFLICT(text_id) DO UPDATE SET word_index=$w, paragraph_index=$p, updated_at=$n",
            ("$t", textId), ("$w", wordIndex), ("$p", paragraphIndex), ("$n", Now));

    public void ClearPosition(string textId) => Exec("DELETE FROM reading_position WHERE text_id=$t", ("$t", textId));

    /// <summary>Paragraph count of a text (for "Абзац N/M").</summary>
    public int GetParagraphCount(string textId) => TextParagraphs.Split(GetText(textId)?.Body ?? "").Count;

    // ---------- activity / XP / achievements ----------

    public void AddActivity(double minutes, int words, bool textDone = false)
    {
        Exec("INSERT INTO daily_streak(day,minutes,texts_done,goal_met) VALUES($d,$m,$td,0) " +
             "ON CONFLICT(day) DO UPDATE SET minutes=minutes+$m, texts_done=texts_done+$td",
            ("$d", Today), ("$m", minutes), ("$td", textDone ? 1 : 0));
        Exec("INSERT INTO daily_activity(date,minutes,words,xp) VALUES($d,$m,$w,0) " +
             "ON CONFLICT(date) DO UPDATE SET minutes=minutes+$m, words=words+$w",
            ("$d", Today), ("$m", minutes), ("$w", words));
    }

    public int AwardXp(int amount, string reason)
    {
        if (amount <= 0) return 0;
        Exec("INSERT INTO xp_log(amount,reason,created_at) VALUES($a,$r,$t)", ("$a", amount), ("$r", reason), ("$t", Now));
        Exec("INSERT INTO daily_activity(date,minutes,words,xp) VALUES($d,0,0,$a) ON CONFLICT(date) DO UPDATE SET xp=xp+$a",
            ("$d", Today), ("$a", amount));
        return amount;
    }

    public int TotalXp() => (int)ScalarLong("SELECT COALESCE(sum(amount),0) FROM xp_log");

    public XpInfo GetXpInfo() => Gamification.Info(TotalXp());

    public long TotalWordsRead() => ScalarLong("SELECT COALESCE(sum(words),0) FROM daily_activity");

    public double MinutesToday()
    {
        var l = Query("SELECT minutes FROM daily_streak WHERE day=$d", r => r.GetDouble(0), ("$d", Today));
        return l.Count == 0 ? 0 : l[0];
    }

    private static readonly string[] DayNames = { "Вс", "Пн", "Вт", "Ср", "Чт", "Пт", "Сб" };

    public IReadOnlyList<DayMinutes> GetWeekMinutes(double maxBar = 90)
    {
        var from = DateTime.Now.Date.AddDays(-6);
        var rows = Query("SELECT day,minutes FROM daily_streak WHERE day>=$f", r => (Day: r.GetString(0), Min: r.GetDouble(1)),
            ("$f", from.ToString("yyyy-MM-dd", CultureInfo.InvariantCulture)));
        var map = new Dictionary<string, double>();
        foreach (var r in rows) map[r.Day] = r.Min;
        var vals = new List<(string Label, double Min)>();
        for (int i = 0; i < 7; i++)
        {
            var d = from.AddDays(i);
            map.TryGetValue(d.ToString("yyyy-MM-dd", CultureInfo.InvariantCulture), out var m);
            vals.Add((DayNames[(int)d.DayOfWeek], m));
        }
        double max = Math.Max(10, vals.Max(v => v.Min));
        return vals.Select(v => new DayMinutes(v.Label, v.Min, Math.Max(3, maxBar * v.Min / max))).ToList();
    }

    /// <summary>Minutes per ISO-ish week for the last N weeks, oldest first.</summary>
    public IReadOnlyList<DayMinutes> GetWeeklyTotals(int weeks = 8, double maxBar = 110)
    {
        var start = DateTime.Now.Date.AddDays(-7 * weeks + 1);
        var rows = Query("SELECT day,minutes FROM daily_streak WHERE day>=$f", r => (Day: r.GetString(0), Min: r.GetDouble(1)),
            ("$f", start.ToString("yyyy-MM-dd", CultureInfo.InvariantCulture)));
        var sums = new double[weeks];
        foreach (var r in rows)
        {
            if (!DateTime.TryParseExact(r.Day, "yyyy-MM-dd", CultureInfo.InvariantCulture, DateTimeStyles.None, out var d)) continue;
            int w = (int)((d.Date - start).TotalDays / 7);
            if (w >= 0 && w < weeks) sums[w] += r.Min;
        }
        double max = Math.Max(10, sums.Max());
        var list = new List<DayMinutes>();
        for (int i = 0; i < weeks; i++)
            list.Add(new DayMinutes(start.AddDays(i * 7).ToString("dd.MM", CultureInfo.InvariantCulture), sums[i], Math.Max(3, maxBar * sums[i] / max)));
        return list;
    }

    public IReadOnlyList<LevelProgress> GetLevelProgress()
    {
        var totals = Query("SELECT level,count(*) FROM content.texts GROUP BY level", r => (Lv: S(r, 0), N: r.GetInt32(1)));
        var dones = Query("SELECT t.level,count(*) FROM content.texts t JOIN progress p ON p.text_id=t.id " +
                          "WHERE p.status='done' GROUP BY t.level", r => (Lv: S(r, 0), N: r.GetInt32(1)));
        var result = new List<LevelProgress>();
        foreach (var lv in new[] { "A1", "A2", "B1", "B2", "C1", "C2" })
        {
            int t = totals.Where(x => x.Lv == lv).Select(x => x.N).FirstOrDefault();
            int d = dones.Where(x => x.Lv == lv).Select(x => x.N).FirstOrDefault();
            result.Add(new LevelProgress(lv, d, t));
        }
        return result;
    }

    public IReadOnlyList<(long At, double Score)> GetScoreHistory(int limit = 30)
    {
        var l = Query("SELECT created_at,score FROM recordings WHERE kind='reading' AND score IS NOT NULL ORDER BY created_at DESC, id DESC LIMIT $l",
            r => (At: r.GetInt64(0), Score: r.GetDouble(1)), ("$l", limit));
        l.Reverse();
        return l.Select(x => (x.At, x.Score)).ToList();
    }

    public IReadOnlyList<WeakSound> GetWeakSounds(int limit = 3)
    {
        var rows = Query("SELECT phoneme,COALESCE(avg_score,0),COALESCE(recent_avg,avg_score,0) FROM phoneme_stats WHERE attempts>=3 " +
                         "ORDER BY (errors*1.0/attempts) DESC, attempts DESC LIMIT $l",
            r => (P: S(r, 0), Avg: r.GetDouble(1), Rec: r.GetDouble(2)), ("$l", limit));
        return rows.Select(x => new WeakSound(x.P, x.Avg, x.Rec - x.Avg > 2 ? "▲ лучше" : x.Rec - x.Avg < -2 ? "▼ хуже" : "● без изменений")).ToList();
    }

    public HashSet<string> GetUnlockedIds() => new(Query("SELECT id FROM achievements", r => r.GetString(0)));

    /// <summary>Evaluates all achievements; returns the ones unlocked by this call.</summary>
    public IReadOnlyList<Achievement> CheckAchievements()
    {
        var have = GetUnlockedIds();
        var fresh = new List<Achievement>();
        void Try(string id, Func<bool> cond)
        {
            if (have.Contains(id)) return;
            bool ok;
            try { ok = cond(); } catch (Exception) { ok = false; }
            if (!ok) return;
            Exec("INSERT OR IGNORE INTO achievements(id,unlocked_at) VALUES($i,$t)", ("$i", id), ("$t", Now));
            var a = Gamification.Find(id);
            if (a != null) fresh.Add(a);
        }
        long doneTexts = ScalarLong("SELECT count(*) FROM progress WHERE status='done'");
        Try("first_text", () => doneTexts >= 1);
        Try("texts_10", () => doneTexts >= 10);
        Try("streak_3", () => GetStreak() >= 3);
        Try("streak_7", () => GetStreak() >= 7);
        long words = TotalWordsRead();
        Try("words_1000", () => words >= 1000);
        Try("words_10000", () => words >= 10000);
        Try("all_a1", () =>
        {
            if (!ContentAvailable) return false;
            long total = ScalarLong("SELECT count(*) FROM content.texts WHERE level='A1'");
            long done = ScalarLong("SELECT count(*) FROM content.texts t JOIN progress p ON p.text_id=t.id WHERE t.level='A1' AND p.status='done'");
            return total > 0 && done >= total;
        });
        Try("perfect_theta", () => ScalarLong("SELECT count(*) FROM phoneme_stats WHERE phoneme='θ' AND attempts>=5 AND avg_score>=90") > 0);
        Try("score_90", () => ScalarLong("SELECT count(*) FROM recordings WHERE score>=90") > 0);
        Try("drill_20", () => ScalarLong("SELECT count(*) FROM xp_log WHERE reason LIKE 'drill%'") >= 20);
        Try("shadow_5", () => ScalarLong("SELECT count(*) FROM xp_log WHERE reason='shadow'") >= 5);
        return fresh;
    }

    // ---------- after the heavy assessment ----------

    /// <summary>Weak words go to the SRS list, bonus XP for a good score. Returns newly unlocked badges.</summary>
    public IReadOnlyList<Achievement> AfterScored(string textId, AssessmentResult result, double score, int wordsRead, bool awardReadXp)
    {
        try
        {
            var vocab = GetVocabulary(textId).GroupBy(v => v.Word, StringComparer.OrdinalIgnoreCase)
                .ToDictionary(g => g.Key, g => g.First(), StringComparer.OrdinalIgnoreCase);
            int added = 0;
            foreach (var w in result.Words.Where(w => w.Score < 60 && w.Text.Length >= 3).OrderBy(w => w.Score))
            {
                if (added >= 6) break;
                var word = w.Text.Trim('.', ',', '!', '?', ';', ':', '"', '“', '”').Replace('’', '\'');
                if (word.Length < 3 || !word.All(ch => char.IsLetter(ch) || ch == '\'')) continue;
                vocab.TryGetValue(word, out var v);
                SaveWord(word, string.IsNullOrEmpty(w.ExpectedIpa) ? v?.IpaUs : w.ExpectedIpa, v?.TranslationRu, textId);
                added++;
            }
        }
        catch (Exception ex) { Diagnostics.LogException("auto words", ex); }
        if (awardReadXp) AwardXp(wordsRead, "reading");
        int bonus = score >= 90 ? wordsRead / 2 : score >= 80 ? wordsRead / 4 : 0;
        AwardXp(bonus, "score");
        return CheckAchievements();
    }

    // ---------- home helpers ----------

    public TextSummary? GetTextOfTheDayFor(string level)
    {
        int day = DateTime.Now.Year * 366 + DateTime.Now.DayOfYear;
        var l = Query(TextSelect + "WHERE t.level=$lv AND COALESCE(p.status,'new')='new' ORDER BY t.sort_order", MapText, ("$lv", level));
        if (l.Count > 0) return l[day % l.Count];
        l = Query(TextSelect + "WHERE COALESCE(p.status,'new')='new' ORDER BY t.sort_order", MapText);
        if (l.Count > 0) return l[day % l.Count];
        return GetTextOfTheDay();
    }

    /// <summary>Words due for SRS drill, oldest due first (limit).</summary>
    public IReadOnlyList<SavedWord> GetDrillWords(int limit = 12) =>
        Query(WordSelect + "WHERE due_at<=$n ORDER BY due_at LIMIT $l", MapWord, ("$n", Now), ("$l", limit));
}
