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
    }
}
