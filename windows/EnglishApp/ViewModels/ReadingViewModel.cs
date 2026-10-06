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
        if (Text != null)
        {
            var attempts = AppServices.Repo.GetAttempts(Text.Id);   // newest first
            if (attempts.Count > 0)
            {
                var latest = attempts[0];
                WavPath = latest.WavPath;
                IsPending = latest.Pending;
                Badge = latest.Badge ?? "";
                if (latest.Score != null && latest.ScoresJson != null)
                {
                    try
                    {
                        var r = PronAssessor.ParseResult(latest.ScoresJson);
                        if (r.Words.Count > 0)
                        {
                            Result = r;
                            ReviewArgs = new ReviewArgs(Text.Id, Text.Body, latest.ScoresJson, r, null);
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
                        int skipped = LiveMarks.Count(c => c == 's');
                        ScoreText = skipped > 0 ? $"Прочитано, пропущено слов: {skipped}" : "Прочитано";
                    }
                    else ScoreText = "Прочитано";
                }
                var scored = attempts.Where(a => a.Score != null).Take(8).Reverse().ToList();
                if (scored.Count > 1)
                    HistoryText = "Попытки: " + string.Join(" → ", scored.Select(a => $"{a.Score:0}"));
            }
        }
        BadgeVisibility = Badge.Length > 0 ? Visibility.Visible : Visibility.Collapsed;
        ScoreVisibility = ScoreText.Length > 0 ? Visibility.Visible : Visibility.Collapsed;
        HistoryVisibility = HistoryText.Length > 0 ? Visibility.Visible : Visibility.Collapsed;
        ReviewVisibility = ReviewArgs != null ? Visibility.Visible : Visibility.Collapsed;
    }

    /// <summary>Plays the word's time span from the saved recording; false when unavailable.</summary>
    public bool PlayMyWord(WordResult w)
    {
        if (WavPath == null || !File.Exists(WavPath) || w.Start is not double s || w.End is not double e || e <= s) return false;
        try { return AppServices.Player.PlayFileSegment(WavPath, s, e); }
        catch (Exception ex) { Diagnostics.LogException("play word", ex); return false; }
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
