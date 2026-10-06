using System.Collections.ObjectModel;
using CommunityToolkit.Mvvm.ComponentModel;
using EnglishApp.Models;
using EnglishApp.Services;

namespace EnglishApp.ViewModels;

public partial class ProgressViewModel : ObservableObject
{
    public ObservableCollection<string> Lines { get; } = new();
    public ObservableCollection<PhonemeStat> Weak { get; } = new();
    [ObservableProperty] private double donePercent;

    // ---- presentation-only values for the stat cards / bars (same data as Lines) ----
    [ObservableProperty] private string textsDoneValue = "0";
    [ObservableProperty] private string textsTotalCaption = "";
    [ObservableProperty] private string startedValue = "0";
    [ObservableProperty] private double startedPercent;
    [ObservableProperty] private string donePercentText = "0%";
    [ObservableProperty] private string startedPercentText = "0%";
    [ObservableProperty] private string attemptsValue = "0";
    [ObservableProperty] private string avgBestValue = "—";
    [ObservableProperty] private string streakValue = "0";
    [ObservableProperty] private string streakCaption = "";
    [ObservableProperty] private string minutesValue = "0";
    [ObservableProperty] private string wordsValue = "0";
    [ObservableProperty] private string wordsCaption = "";
    [ObservableProperty] private bool hasWeak;
    public bool NoWeak => !HasWeak;
    partial void OnHasWeakChanged(bool value) => OnPropertyChanged(nameof(NoWeak));

    public void Load()
    {
        var s = AppServices.Repo.GetStats();
        Lines.Clear();
        Lines.Add($"Пройдено текстов: {s.TextsDone} из {s.TextsTotal}");
        Lines.Add($"В процессе: {s.TextsStarted}");
        Lines.Add($"Попыток чтения: {s.Attempts}");
        Lines.Add($"Средний лучший результат: {s.AvgBest:0}%");
        Lines.Add($"Серия: {s.Streak} {HomeViewModel.Plural(s.Streak, "день", "дня", "дней")}");
        Lines.Add($"Время практики: {s.MinutesTotal:0} мин");
        Lines.Add($"Слов в словаре: {s.WordsSaved} (к повторению: {s.WordsDue})");
        DonePercent = s.TextsTotal == 0 ? 0 : 100.0 * s.TextsDone / s.TextsTotal;
        Weak.Clear();
        foreach (var p in AppServices.Repo.GetWorstPhonemes()) Weak.Add(p);

        TextsDoneValue = s.TextsDone.ToString();
        TextsTotalCaption = $"из {s.TextsTotal} {HomeViewModel.Plural(s.TextsTotal, "текста", "текстов", "текстов")}";
        StartedValue = s.TextsStarted.ToString();
        StartedPercent = s.TextsTotal == 0 ? 0 : 100.0 * s.TextsStarted / s.TextsTotal;
        DonePercentText = $"{DonePercent:0}%";
        StartedPercentText = $"{StartedPercent:0}%";
        AttemptsValue = s.Attempts.ToString();
        var hasScores = s.Attempts > 0 && s.AvgBest > 0;
        AvgBestValue = hasScores ? $"{s.AvgBest:0}%" : "—";
        StreakValue = s.Streak.ToString();
        StreakCaption = HomeViewModel.Plural(s.Streak, "день подряд", "дня подряд", "дней подряд");
        MinutesValue = $"{s.MinutesTotal:0}";
        WordsValue = s.WordsSaved.ToString();
        WordsCaption = $"к повторению: {s.WordsDue}";
        HasWeak = Weak.Count > 0;
    }

    // ---- x:Bind helpers for the "hardest sounds" bar chart ----
    public static double ErrorRate(int errors, int attempts) => attempts <= 0 ? 0 : 100.0 * errors / attempts;
    public static double ErrorFraction(int errors, int attempts) => Fraction(ErrorRate(errors, attempts));
    /// <summary>Percent (0..100) to a 0..1 bar scale.</summary>
    public static double Fraction(double percent) => double.IsNaN(percent) ? 0 : Math.Clamp(percent / 100.0, 0, 1);
    public static string ErrorRateText(int errors, int attempts) => $"{ErrorRate(errors, attempts):0}%";
    public static string PhonemeLabel(string phoneme) => $"/{phoneme}/";
    public static string PhonemeDetail(int errors, int attempts, double avgScore) =>
        $"ошибок {errors} из {attempts} · средний балл {avgScore:0}";
}
