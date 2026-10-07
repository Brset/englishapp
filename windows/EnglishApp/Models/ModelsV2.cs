using System.Text.RegularExpressions;

namespace EnglishApp.Models;

public sealed record ParagraphInfo(int Index, string Text, int Start);

public sealed record Achievement(string Id, string Title, string Description, string Glyph);

public sealed record XpInfo(int Total, int LevelIndex, string LevelName, int LevelFloor, int NextAt)
{
    public bool IsMax => NextAt < 0;
    public double Fraction => IsMax ? 1 : Math.Clamp((double)(Total - LevelFloor) / Math.Max(1, NextAt - LevelFloor), 0, 1);
    public string Line => IsMax ? $"{LevelName} · {Total} XP" : $"{LevelName} · {Total} из {NextAt} XP";
}

public sealed record ReadingPosition(string TextId, int WordIndex, int ParagraphIndex);

public sealed record DayMinutes(string Label, double Minutes, double BarHeight);

public sealed record LevelProgress(string Level, int Done, int Total)
{
    public double Percent => Total == 0 ? 0 : 100.0 * Done / Total;
    public string Label => $"{Done}/{Total}";
}

public sealed record WeakSound(string Phoneme, double AvgScore, string Trend)
{
    public string Chip => $"/{Phoneme}/";
    public string Detail => $"/{Phoneme}/  средний балл {AvgScore:0}  {Trend}";
}

public sealed record CelebrationInfo(string TextId, string Title, double Coverage, int XpGained,
    IReadOnlyList<Achievement> NewBadges, XpInfo Xp);

/// <summary>Navigation argument for the record page.</summary>
public sealed record RecordArgs(string TextId, int? StartParagraph = null, bool? ParagraphMode = null);

public sealed record SentenceItem(int Index, string Text);

public static class TextParagraphs
{
    private static readonly Regex WordRx = new(@"[A-Za-z0-9]+(?:['’][A-Za-z]+)*", RegexOptions.Compiled);

    public static int WordCount(string s) => WordRx.Matches(s).Count;

    /// <summary>Paragraphs separated by blank lines; Start is the offset of the paragraph inside the body.</summary>
    public static List<ParagraphInfo> Split(string body)
    {
        var list = new List<ParagraphInfo>();
        int pos = 0;
        foreach (var raw in body.Split("\n\n", StringSplitOptions.RemoveEmptyEntries))
        {
            var t = raw.Trim();
            if (t.Length == 0) continue;
            int idx = body.IndexOf(t, pos, StringComparison.Ordinal);
            if (idx < 0) idx = pos;
            list.Add(new ParagraphInfo(list.Count, t, idx));
            pos = Math.Min(body.Length, idx + t.Length);
        }
        if (list.Count == 0 && body.Trim().Length > 0) list.Add(new ParagraphInfo(0, body.Trim(), 0));
        return list;
    }

    public static int ParagraphAt(IReadOnlyList<ParagraphInfo> paras, int offset)
    {
        int r = 0;
        foreach (var p in paras) { if (p.Start <= offset) r = p.Index; else break; }
        return r;
    }

    private static readonly Regex SentenceRx = new(@"[^.!?\n]+[.!?]*", RegexOptions.Compiled);

    public static List<SentenceItem> Sentences(string body)
    {
        var list = new List<SentenceItem>();
        foreach (Match m in SentenceRx.Matches(body))
        {
            var t = m.Value.Trim();
            if (t.Length > 0 && WordCount(t) >= 2) list.Add(new SentenceItem(list.Count, t));
        }
        return list;
    }
}
