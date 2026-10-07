using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using EnglishApp.Models;
using EnglishApp.Services;
using Microsoft.UI.Xaml.Media;

namespace EnglishApp.ViewModels;

/// <summary>Spaced-repetition word drill: listen, say, instant score, then again / hard / good / easy.</summary>
public partial class WordDrillViewModel : ObservableObject
{
    private List<SavedWord> _queue = new();
    private readonly System.Diagnostics.Stopwatch _clock = new();
    private int _total;
    private int _done;

    [ObservableProperty] private SavedWord? current;
    [ObservableProperty] private string status = "";
    [ObservableProperty] private bool isBusy;
    [ObservableProperty] private bool hasResult;
    [ObservableProperty] private string resultText = "";
    [ObservableProperty] private string heardText = "";
    [ObservableProperty] private string suggestion = "";
    [ObservableProperty] private Brush resultBrush = Ui.Gray;
    [ObservableProperty] private string progressText = "";
    [ObservableProperty] private double progressPercent;
    [ObservableProperty] private string summary = "";

    public bool HasCurrent => Current != null;
    public bool IsEmpty => Current == null;
    public bool NotBusy => !IsBusy;
    partial void OnIsBusyChanged(bool value) => OnPropertyChanged(nameof(NotBusy));
    partial void OnCurrentChanged(SavedWord? value) { OnPropertyChanged(nameof(HasCurrent)); OnPropertyChanged(nameof(IsEmpty)); }

    public void Load()
    {
        _queue = AppServices.Repo.GetDrillWords(12).ToList();
        _total = _queue.Count;
        _done = 0;
        Summary = "";
        NextCard();
    }

    private void NextCard()
    {
        HasResult = false; Status = ""; HeardText = ""; Suggestion = "";
        Current = _queue.Count > 0 ? _queue[0] : null;
        ProgressText = _total == 0 ? "" : $"Карточка {Math.Min(_done + 1, _total)} из {_total}";
        ProgressPercent = _total == 0 ? 0 : 100.0 * _done / _total;
        if (Current == null && _total > 0) Summary = $"Готово! Повторено слов: {_done}.";
        _clock.Restart();
    }

    [RelayCommand]
    private async Task ListenAsync()
    {
        if (Current == null) return;
        Status = "";
        if (!await Speech.SpeakAsync(Current.Word, 0.9)) Status = "Синтез речи недоступен.";
    }

    [RelayCommand]
    private async Task SayAsync()
    {
        if (Current == null || IsBusy) return;
        IsBusy = true; HasResult = false; Status = "Говорите…";
        try
        {
            var wav = await QuickSpeech.RecordAsync();
            if (wav == null) { Status = "Не удалось записать звук."; return; }
            Status = "Оцениваем…";
            var res = await QuickSpeech.ScoreAsync(wav, Current.Word);
            if (res.Error != null) { Status = "Не удалось оценить: " + res.Error; return; }
            ResultText = $"{res.Score:0}";
            ResultBrush = Ui.ScoreBrush(res.Score);
            HeardText = string.IsNullOrWhiteSpace(res.Recognized) ? "Ничего не расслышал, попробуйте ещё раз." : "Вы сказали: " + res.Recognized;
            Suggestion = res.Score >= 90 ? "Рекомендуем: Легко" : res.Score >= 80 ? "Рекомендуем: Хорошо" : res.Score >= 60 ? "Рекомендуем: Трудно" : "Рекомендуем: Снова";
            HasResult = true;
            Status = "";
            AppServices.Repo.AwardXp(2, "drill");
        }
        catch (Exception ex) { Status = "Ошибка записи: " + ex.Message; }
        finally { IsBusy = false; }
    }

    [RelayCommand]
    private void Grade(string quality)
    {
        if (Current == null) return;
        try
        {
            AppServices.Repo.ReviewWord(Current, int.Parse(quality));
            AppServices.Repo.AddActivity(Math.Min(3, _clock.Elapsed.TotalMinutes), 0);
            var badges = AppServices.Repo.CheckAchievements();
            if (badges.Count > 0) App.MainWindow.ShowBadges(badges);
        }
        catch (Exception ex) { Diagnostics.LogException("grade", ex); }
        _queue.RemoveAt(0);
        _done++;
        NextCard();
    }
}
