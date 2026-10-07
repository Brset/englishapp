using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using EnglishApp.Models;
using EnglishApp.Services;
using Microsoft.UI.Xaml.Media;

namespace EnglishApp.ViewModels;

/// <summary>Shadowing: for every sentence play the reference, then the user repeats; per-sentence score and coverage.</summary>
public partial class ShadowingViewModel : ObservableObject
{
    private List<SentenceItem> _sentences = new();
    private readonly Dictionary<int, SpeakResult> _results = new();
    private int _idx;
    private TextDetail? _text;

    [ObservableProperty] private string title = "Шэдоуинг";
    [ObservableProperty] private string counter = "";
    [ObservableProperty] private string status = "";
    [ObservableProperty] private bool isBusy;
    [ObservableProperty] private bool hasResult;
    [ObservableProperty] private bool finished;
    [ObservableProperty] private string resultText = "";
    [ObservableProperty] private string coverageText = "";
    [ObservableProperty] private string summary = "";
    [ObservableProperty] private double progress;
    [ObservableProperty] private Brush resultBrush = Ui.Gray;

    public bool HasText => _text != null && _sentences.Count > 0;
    public bool NoText => !HasText;
    public bool Active => HasText && !Finished;
    public bool NotBusy => !IsBusy;
    partial void OnIsBusyChanged(bool value) => OnPropertyChanged(nameof(NotBusy));
    partial void OnFinishedChanged(bool value) => OnPropertyChanged(nameof(Active));

    public string TextId => _text?.Id ?? "";
    public string CurrentSentence => HasText ? _sentences[Math.Clamp(_idx, 0, _sentences.Count - 1)].Text : "";
    public SpeakResult? LastResult => _results.TryGetValue(_idx, out var r) ? r : null;

    /// <summary>UI thread: sentence or result changed; the page re-renders the coloured sentence.</summary>
    public event Action? Changed;

    public void Load(string? textId)
    {
        _text = string.IsNullOrEmpty(textId) ? null : AppServices.Repo.GetText(textId);
        _sentences = _text == null ? new List<SentenceItem>() : TextParagraphs.Sentences(_text.Body);
        _results.Clear();
        _idx = 0;
        Finished = false;
        Title = _text == null ? "Шэдоуинг" : "Шэдоуинг · " + _text.TitleEn;
        OnPropertyChanged(nameof(HasText));
        OnPropertyChanged(nameof(NoText));
        OnPropertyChanged(nameof(Active));
        Show();
    }

    private void Show()
    {
        Status = "";
        var r = LastResult;
        HasResult = r is { Error: null };
        if (r is { Error: null } rr)
        {
            ResultText = $"{rr.Score:0}";
            ResultBrush = Ui.ScoreBrush(rr.Score);
            CoverageText = $"Охват: {rr.Coverage:0}%";
        }
        Counter = HasText ? $"Предложение {_idx + 1} из {_sentences.Count}" : "";
        Progress = _sentences.Count == 0 ? 0 : 100.0 * _results.Count / _sentences.Count;
        OnPropertyChanged(nameof(CurrentSentence));
        OnPropertyChanged(nameof(LastResult));
        Changed?.Invoke();
    }

    private static async Task WaitPlaybackAsync()
    {
        await Task.Delay(150);
        for (int i = 0; i < 300 && AppServices.Player.IsPlaying; i++) await Task.Delay(100);
    }

    [RelayCommand]
    private async Task ListenAsync()
    {
        if (!HasText || IsBusy) return;
        Status = "";
        if (!await Speech.SpeakAsync(CurrentSentence, AppServices.Settings.Speed)) Status = "Синтез речи недоступен.";
    }

    [RelayCommand]
    private async Task ShadowAsync()
    {
        if (!HasText || IsBusy) return;
        IsBusy = true;
        try
        {
            Status = "Слушайте…";
            if (!await Speech.SpeakAsync(CurrentSentence, AppServices.Settings.Speed)) { Status = "Синтез речи недоступен."; return; }
            await WaitPlaybackAsync();
            await RecordAndScoreAsync();
        }
        catch (Exception ex) { Status = "Ошибка: " + ex.Message; }
        finally { IsBusy = false; }
    }

    [RelayCommand]
    private async Task RepeatAsync()
    {
        if (!HasText || IsBusy) return;
        IsBusy = true;
        try { await RecordAndScoreAsync(); }
        catch (Exception ex) { Status = "Ошибка записи: " + ex.Message; }
        finally { IsBusy = false; }
    }

    private async Task RecordAndScoreAsync()
    {
        Status = "Повторите предложение…";
        var wav = await QuickSpeech.RecordAsync(15000, 1200);
        if (wav == null) { Status = "Не удалось записать звук."; return; }
        Status = "Оцениваем…";
        var res = await QuickSpeech.ScoreAsync(wav, CurrentSentence);
        if (res.Error != null) { Status = "Не удалось оценить: " + res.Error; return; }
        _results[_idx] = res;
        AppServices.Repo.AwardXp(3, "shadow");
        AppServices.Repo.AddActivity(Math.Max(0.1, TextParagraphs.WordCount(CurrentSentence) / 100.0 * 2), TextParagraphs.WordCount(CurrentSentence));
        var badges = AppServices.Repo.CheckAchievements();
        if (badges.Count > 0) App.MainWindow.ShowBadges(badges);
        Show();
    }

    [RelayCommand]
    private void Next()
    {
        if (!HasText || IsBusy) return;
        if (_idx < _sentences.Count - 1) { _idx++; Show(); }
        else Finish();
    }

    [RelayCommand]
    private void Prev()
    {
        if (!HasText || IsBusy || _idx == 0) return;
        _idx--;
        Show();
    }

    [RelayCommand]
    private void Finish()
    {
        if (!HasText) return;
        var done = _results.Values.Where(r => r.Error == null).ToList();
        Summary = done.Count == 0
            ? "Вы ещё не повторили ни одного предложения."
            : $"Повторено предложений: {done.Count} из {_sentences.Count}. Средняя оценка: {done.Average(r => r.Score):0}, средний охват: {done.Average(r => r.Coverage):0}%.";
        Finished = true;
    }

    [RelayCommand]
    private void Restart()
    {
        _results.Clear();
        _idx = 0;
        Finished = false;
        Show();
    }
}
