using System.Collections.ObjectModel;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using EnglishApp.Models;
using EnglishApp.Native;
using EnglishApp.Services;
using Microsoft.UI.Xaml;

namespace EnglishApp.ViewModels;

public partial class ReadingViewModel : ObservableObject
{
    private Dictionary<string, VocabEntry> _vocab = new(StringComparer.OrdinalIgnoreCase);

    [ObservableProperty] private TextDetail? text;
    [ObservableProperty] private double speed = AppServices.Settings.Speed;
    [ObservableProperty] private string status = "";
    [ObservableProperty] private string badge = "";
    [ObservableProperty] private Visibility badgeVisibility = Visibility.Collapsed;
    [ObservableProperty] private bool isPending;
    [ObservableProperty] private string scoreText = "";
    [ObservableProperty] private Visibility scoreVisibility = Visibility.Collapsed;
    [ObservableProperty] private string historyText = "";
    [ObservableProperty] private Visibility historyVisibility = Visibility.Collapsed;
    [ObservableProperty] private Visibility reviewVisibility = Visibility.Collapsed;
    [ObservableProperty] private double coveragePct;
    [ObservableProperty] private string coverageLine = "";
    [ObservableProperty] private bool hasCoverage;
    [ObservableProperty] private string resumeLabel = "";
    [ObservableProperty] private bool canResume;

    public ReadingTypography Typo { get; } = new();
    public bool NoText => Text == null;
    public bool HasText => Text != null;
    partial void OnTextChanged(TextDetail? value) { OnPropertyChanged(nameof(NoText)); OnPropertyChanged(nameof(HasText)); }
    public ReadingPosition? Position { get; private set; }
    /// <summary>Word marks of paragraphs that have no assessment yet (absolute offsets in the body).</summary>
    public List<(int Begin, int End, char Mark)> ExtraMarks { get; } = new();
    private readonly List<(int From, int To, string Wav)> _wavRanges = new();

    /// <summary>Full assessment of the latest reading (null while pending / never read).</summary>
    public AssessmentResult? Result { get; private set; }
    /// <summary>Quick live marks ('r' read, 's' skipped, '-') of a reading that is still being assessed.</summary>
    public string? LiveMarks { get; private set; }
    public string? WavPath { get; private set; }
    public ReviewArgs? ReviewArgs { get; private set; }

    public ObservableCollection<FocusSound> Focus { get; } = new();

    public string Accent => AppServices.Settings.Accent;
    public string SpeedLabel => $"Скорость: {Speed:0.00}×";
    partial void OnSpeedChanged(double value)
    {
        OnPropertyChanged(nameof(SpeedLabel));
        AppServices.Settings.Speed = value;
    }

    public bool Load(string? textId)
    {
        if (string.IsNullOrEmpty(textId)) return false;
        Text = AppServices.Repo.GetText(textId);
        if (Text == null) return false;
        AppServices.Repo.MarkOpened(textId);
        _vocab = AppServices.Repo.GetVocabulary(textId).GroupBy(v => v.Word, StringComparer.OrdinalIgnoreCase)
            .ToDictionary(g => g.Key, g => g.First(), StringComparer.OrdinalIgnoreCase);
        Focus.Clear();
        foreach (var f in AppServices.Repo.GetFocusSounds(textId)) Focus.Add(f);
        LoadReadState();
        return true;
    }

    /// <summary>Reloads the latest reading: result, live marks, badge, score and attempt history.</summary>
    public void LoadReadState()
    {
        Result = null; LiveMarks = null; WavPath = null; ReviewArgs = null;
        Badge = ""; IsPending = false; ScoreText = ""; HistoryText = "";
        CoverageLine = ""; HasCoverage = false; CoveragePct = 0; CanResume = false; ResumeLabel = "";
        ExtraMarks.Clear(); _wavRanges.Clear(); Position = null;
        if (Text != null)
        {
            var attempts = AppServices.Repo.GetAttempts(Text.Id);   // newest first
            if (attempts.Count > 0)
            {
                if (attempts[0].ParagraphIndex != null) LoadParagraphState(attempts);
                else LoadWholeState(attempts);
                var scored = attempts.Where(a => a.Score != null).Take(8).Reverse().ToList();
                if (scored.Count > 1)
                    HistoryText = "Попытки: " + string.Join(" → ", scored.Select(a => $"{a.Score:0}"));
            }
            LoadResume();
        }
        BadgeVisibility = Badge.Length > 0 ? Visibility.Visible : Visibility.Collapsed;
        ScoreVisibility = ScoreText.Length > 0 ? Visibility.Visible : Visibility.Collapsed;
        HistoryVisibility = HistoryText.Length > 0 ? Visibility.Visible : Visibility.Collapsed;
        ReviewVisibility = ReviewArgs != null ? Visibility.Visible : Visibility.Collapsed;
    }

    private void LoadResume()
    {
        if (Text == null) return;
        var pos = AppServices.Repo.GetPosition(Text.Id);
        Position = pos;
        if (pos == null) return;
        int total = AppServices.Repo.GetParagraphCount(Text.Id);
        CanResume = true;
        ResumeLabel = total > 1 ? $"Продолжить с места (абзац {Math.Min(pos.ParagraphIndex + 1, total)}/{total})" : "Продолжить с места";
    }

    private void SetCoverage(double? best, double? current, double? wpm, int? skipped)
    {
        var pct = current ?? best;
        if (pct is not double v) return;
        HasCoverage = true;
        CoveragePct = Math.Round(v);
        var parts = new List<string> { $"Прочитано {v:0}%" };
        if (skipped is int sk && sk > 0) parts.Add($"пропущено слов: {sk}");
        if (wpm is double w && w > 1) parts.Add($"{w:0} сл/мин");
        CoverageLine = string.Join(" · ", parts);
    }

    private void LoadWholeState(IReadOnlyList<AttemptInfo> attempts)
    {
        var latest = attempts[0];
        WavPath = latest.WavPath;
        IsPending = latest.Pending;
        Badge = latest.Badge ?? "";
        int? skipped = null;
        if (latest.Score != null && latest.ScoresJson != null)
        {
            try
            {
                var r = PronAssessor.ParseResult(latest.ScoresJson);
                if (r.Words.Count > 0)
                {
                    Result = r;
                    ReviewArgs = new ReviewArgs(Text!.Id, Text.Body, latest.ScoresJson, r, null);
                    ScoreText = $"Оценка: {r.Scores.Overall:0}/100 · точность {r.Scores.Accuracy:0} · полнота {r.Scores.Completeness:0} · беглость {r.Scores.Fluency:0}";
                }
            }
            catch (Exception ex) { Diagnostics.LogException("parse result", ex); }
            if (Result == null) ScoreText = $"Оценка: {latest.Score:0}/100";
        }
        else
        {
            LiveMarks = ContentRepository.ParseLiveMarks(latest.ScoresJson);
            if (LiveMarks != null)
            {
                skipped = LiveMarks.Count(c => c == 's');
                ScoreText = skipped > 0 ? $"Прочитано, пропущено слов: {skipped}" : "Прочитано";
            }
            else ScoreText = "Прочитано";
        }
        SetCoverage(null, latest.Coverage, null, skipped);
    }

    /// <summary>Paragraph mode: the newest attempt of every paragraph since the last whole-text attempt, aggregated.</summary>
    private void LoadParagraphState(IReadOnlyList<AttemptInfo> attempts)
    {
        var text = Text!;
        var newest = new Dictionary<int, AttemptInfo>();
        foreach (var a in attempts)
        {
            if (a.ParagraphIndex is not int pi) break;
            if (!newest.ContainsKey(pi)) newest[pi] = a;
        }
        var paras = TextParagraphs.Split(text.Body);
        var words = new List<WordResult>();
        var advice = new Dictionary<string, AdviceItem>();
        var pauses = new List<LongPause>();
        var warnings = new List<string>();
        double wAcc = 0, wCom = 0, wFlu = 0, wOver = 0, wSum = 0, speech = 0, art = 0;
        int reps = 0, hes = 0, scoredParas = 0, pendingParas = 0, wordOffset = 0, version = 0;
        bool phoneme = true;
        double? cov = AppServices.Repo.ComputeTextCoverage(text.Id);
        foreach (var para in paras)
        {
            int est = TextParagraphs.WordCount(para.Text);
            int count = est;
            if (newest.TryGetValue(para.Index, out var a))
            {
                WavPath ??= a.WavPath;
                if (a.Pending) pendingParas++;
                AssessmentResult? r = null;
                if (a.Score != null && a.ScoresJson != null)
                {
                    try { r = PronAssessor.ParseResult(a.ScoresJson); }
                    catch (Exception ex) { Diagnostics.LogException("parse paragraph", ex); }
                }
                if (r != null && r.Words.Count > 0)
                {
                    scoredParas++;
                    count = r.Words.Count;
                    double w = count;
                    wAcc += r.Scores.Accuracy * w; wCom += r.Scores.Completeness * w; wFlu += r.Scores.Fluency * w;
                    wOver += r.Scores.Overall * w; wSum += w;
                    speech += r.Fluency.SpeechSeconds; art += r.Fluency.ArticulationSeconds;
                    reps += r.Fluency.Repetitions; hes += r.Fluency.Hesitations;
                    phoneme &= r.PhonemeLevel;
                    version = Math.Max(version, r.Version);
                    foreach (var pw in r.Words)
                        words.Add(pw with { Index = pw.Index + wordOffset, U16Begin = pw.U16Begin + para.Start, U16End = pw.U16End + para.Start });
                    _wavRanges.Add((wordOffset, wordOffset + count, a.WavPath));
                    foreach (var lp in r.Fluency.LongPauses) pauses.Add(lp with { AfterWord = lp.AfterWord + wordOffset });
                    foreach (var ad in r.Advice)
                    {
                        var shifted = ad with { Words = ad.Words.Select(x => x + wordOffset).ToList() };
                        if (advice.TryGetValue(ad.Id, out var prevAd))
                            advice[ad.Id] = prevAd with { Count = prevAd.Count + ad.Count, Words = prevAd.Words.Concat(shifted.Words).ToList() };
                        else advice[ad.Id] = shifted;
                    }
                    foreach (var wn in r.Warnings) if (!warnings.Contains(wn)) warnings.Add(wn);
                }
                else
                {
                    var marks = ContentRepository.ParseLiveMarks(a.ScoresJson);
                    if (marks != null)
                    {
                        IReadOnlyList<TokenInfo> tokens;
                        try { tokens = PronAssessor.Tokenize(para.Text); } catch (Exception) { tokens = Array.Empty<TokenInfo>(); }
                        count = tokens.Count > 0 ? tokens.Count : est;
                        for (int k = 0; k < tokens.Count && k < marks.Length; k++)
                            if (marks[k] is 'r' or 's') ExtraMarks.Add((tokens[k].U16Begin + para.Start, tokens[k].U16End + para.Start, marks[k]));
                    }
                }
            }
            wordOffset += count;
        }
        int totalParas = paras.Count;
        Badge = pendingParas > 0 ? $"оценка готовится… ({pendingParas} из {newest.Count} абз.)" : "";
        IsPending = pendingParas > 0;
        if (wSum > 0)
        {
            var scores = new Scores { Accuracy = wAcc / wSum, Completeness = wCom / wSum, Fluency = wFlu / wSum, Overall = wOver / wSum };
            var agg = new AssessmentResult
            {
                Version = version, Scores = scores, PhonemeLevel = phoneme, Words = words,
                Fluency = new FluencyInfo
                {
                    WordCount = words.Count, SpeechSeconds = speech, ArticulationSeconds = art,
                    WordsPerMinute = speech > 0 ? words.Count * 60.0 / speech : 0, LongPauses = pauses,
                    Repetitions = reps, Hesitations = hes,
                },
                Advice = advice.Values.ToList(), Warnings = warnings,
            };
            Result = agg;
            ReviewArgs = new ReviewArgs(text.Id, text.Body, "", agg, scoredParas < newest.Count ? "Показаны только оценённые абзацы." : null);
            ScoreText = $"Оценка: {scores.Overall:0}/100 · абзацев оценено {scoredParas} из {totalParas} · точность {scores.Accuracy:0} · беглость {scores.Fluency:0}";
        }
        else ScoreText = $"Прочитано абзацев: {newest.Count} из {totalParas}";
        int skipped = ExtraMarks.Count(m => m.Mark == 's');
        SetCoverage(null, cov, null, skipped > 0 ? skipped : null);
    }

    /// <summary>Plays the word's time span from the saved recording; false when unavailable.</summary>
    public bool PlayMyWord(WordResult w)
    {
        var wav = WavForWord(w);
        if (wav == null || !File.Exists(wav) || w.Start is not double s || w.End is not double e || e <= s) return false;
        try { return AppServices.Player.PlayFileSegment(wav, s, e); }
        catch (Exception ex) { Diagnostics.LogException("play word", ex); return false; }
    }

    /// <summary>Recording that contains the word (per-paragraph recordings in paragraph mode).</summary>
    public string? WavForWord(WordResult w)
    {
        if (_wavRanges.Count == 0) return WavPath;
        foreach (var r in _wavRanges) if (w.Index >= r.From && w.Index < r.To) return r.Wav;
        return null;
    }

    public string AdviceTip(string? adviceId)
    {
        if (adviceId == null || Result == null) return "";
        return Result.Advice.FirstOrDefault(a => a.Id == adviceId)?.TipRu ?? "";
    }

    public VocabEntry? FindVocab(string word) => _vocab.TryGetValue(word, out var v) ? v : null;

    /// <summary>IPA for the current accent: vocabulary first, then the native lexicon.</summary>
    public string IpaFor(string word)
    {
        var v = FindVocab(word);
        var ipa = v == null ? "" : (Accent == "UK" ? v.IpaUk : v.IpaUs);
        if (string.IsNullOrWhiteSpace(ipa)) ipa = AppServices.Engine.Lookup(word.ToLowerInvariant())?.Ipa ?? "";
        return ipa;
    }

    [RelayCommand]
    public async Task SpeakAllAsync()
    {
        if (Text == null) return;
        Status = "";
        if (!await Speech.SpeakAsync(Text.Body.Replace("\n\n", "\n"), Speed)) Status = "Синтез речи недоступен.";
    }

    [RelayCommand] public void Stop() => AppServices.Player.Stop();

    public async Task SpeakWordAsync(string word)
    {
        Status = "";
        if (!await Speech.SpeakAsync(word, Math.Min(Speed, 1.0))) Status = "Синтез речи недоступен.";
    }

    public void SaveWord(string word)
    {
        var v = FindVocab(word);
        AppServices.Repo.SaveWord(word, IpaFor(word), v?.TranslationRu, Text?.Id);
    }
}
