using System.Collections.ObjectModel;
using CommunityToolkit.Mvvm.ComponentModel;
using EnglishApp.Models;
using EnglishApp.Services;

namespace EnglishApp.ViewModels;

public sealed record BadgeItem(string Glyph, string Title, string Description, double Opacity);

public partial class ProgressViewModel : ObservableObject
{
    public ObservableCollection<WeakSound> Weak { get; } = new();
    public ObservableCollection<LevelProgress> Levels { get; } = new();
    public ObservableCollection<BadgeItem> Badges { get; } = new();
    public IReadOnlyList<double> Scores { get; private set; } = Array.Empty<double>();
    public IReadOnlyList<DayMinutes> Weeks { get; private set; } = Array.Empty<DayMinutes>();

    [ObservableProperty] private string totalWords = "0";
    [ObservableProperty] private string textsDone = "0";
    [ObservableProperty] private string streak = "0";
    [ObservableProperty] private string minutes = "0";
    [ObservableProperty] private string avgBest = "–";
    [ObservableProperty] private string xpLine = "";
    [ObservableProperty] private double xpPercent;
    [ObservableProperty] private bool hasScores;
    [ObservableProperty] private bool noScores = true;
    [ObservableProperty] private bool hasWeak;
    [ObservableProperty] private bool noWeak = true;
    [ObservableProperty] private string badgesLine = "";

    public void Load()
    {
        var repo = AppServices.Repo;
        var s = repo.GetStats();
        TotalWords = repo.TotalWordsRead().ToString("N0");
        TextsDone = $"{s.TextsDone}/{s.TextsTotal}";
        Streak = $"{s.Streak} {HomeViewModel.Plural(s.Streak, "день", "дня", "дней")}";
        Minutes = $"{s.MinutesTotal:0}";
        AvgBest = s.TextsDone + s.TextsStarted > 0 ? $"{s.AvgBest:0}" : "–";
        var xp = repo.GetXpInfo();
        XpLine = xp.Line;
        XpPercent = xp.Fraction * 100;

        Scores = repo.GetScoreHistory(30).Select(x => x.Score).ToList();
        HasScores = Scores.Count > 0;
        NoScores = !HasScores;
        Weeks = repo.GetWeeklyTotals(8);

        Weak.Clear();
        foreach (var w in repo.GetWeakSounds(8)) Weak.Add(w);
        HasWeak = Weak.Count > 0;
        NoWeak = !HasWeak;

        Levels.Clear();
        foreach (var l in repo.GetLevelProgress()) Levels.Add(l);

        var have = repo.GetUnlockedIds();
        Badges.Clear();
        foreach (var a in Gamification.All)
            Badges.Add(new BadgeItem(a.Glyph, a.Title, a.Description, have.Contains(a.Id) ? 1.0 : 0.35));
        BadgesLine = $"Достижения: {have.Count} из {Gamification.All.Count}";
    }
}
