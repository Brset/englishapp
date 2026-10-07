using System.Collections.ObjectModel;
using CommunityToolkit.Mvvm.ComponentModel;
using EnglishApp.Models;
using EnglishApp.Services;

namespace EnglishApp.ViewModels;

public partial class HomeViewModel : ObservableObject
{
    [ObservableProperty] private TextSummary? continueText;
    [ObservableProperty] private TextSummary? todayText;
    [ObservableProperty] private string streakText = "";
    [ObservableProperty] private string warning = "";
    [ObservableProperty] private string greeting = "Здравствуйте";
    [ObservableProperty] private int streakDays;
    [ObservableProperty] private double goalPercent;
    [ObservableProperty] private string goalText = "";
    [ObservableProperty] private string goalCaption = "";
    [ObservableProperty] private string levelLine = "";
    [ObservableProperty] private double levelPercent;
    [ObservableProperty] private string continueLine = "";
    [ObservableProperty] private double continuePercent;
    [ObservableProperty] private string todayLine = "";

    public ObservableCollection<WeakSound> WeakSounds { get; } = new();
    public IReadOnlyList<DayMinutes> Week { get; private set; } = Array.Empty<DayMinutes>();
    /// <summary>Where the continue card leads: record page at the saved paragraph, or null to open the reading page.</summary>
    public RecordArgs? ContinueArgs { get; private set; }

    public bool HasWarning => !string.IsNullOrEmpty(Warning);
    partial void OnWarningChanged(string value) => OnPropertyChanged(nameof(HasWarning));
    public bool HasContinue => ContinueText != null;
    public bool NoContinue => ContinueText == null;
    public bool HasToday => TodayText != null;
    public bool HasWeak => WeakSounds.Count > 0;
    public bool NoWeak => WeakSounds.Count == 0;
    partial void OnContinueTextChanged(TextSummary? value) { OnPropertyChanged(nameof(HasContinue)); OnPropertyChanged(nameof(NoContinue)); }
    partial void OnTodayTextChanged(TextSummary? value) => OnPropertyChanged(nameof(HasToday));

    public void Load()
    {
        var repo = AppServices.Repo;
        var settings = AppServices.Settings;
        Greeting = DateTime.Now.Hour switch { < 5 => "Доброй ночи", < 12 => "Доброе утро", < 18 => "Добрый день", _ => "Добрый вечер" };

        ContinueText = repo.GetContinueText();
        ContinueArgs = null;
        ContinueLine = "";
        if (ContinueText is { } c)
        {
            var pos = repo.GetPosition(c.Id);
            int total = repo.GetParagraphCount(c.Id);
            ContinuePercent = c.CoverageValue;
            ContinueLine = $"Прочитано {c.CoverageValue:0}%";
            if (pos != null && total > 1)
            {
                ContinueLine += $" · Абзац {Math.Min(pos.ParagraphIndex + 1, total)}/{total}";
                ContinueArgs = new RecordArgs(c.Id, pos.ParagraphIndex, true);
            }
        }

        TodayText = repo.GetTextOfTheDayFor(settings.Level);
        TodayLine = TodayText == null ? "" : $"{TodayText.Level} · {TodayText.Genre} · {TodayText.WordCount} сл.";

        var s = repo.GetStreak();
        StreakDays = s;
        StreakText = s == 0 ? "Начните серию сегодня" : $"{s} {Plural(s, "день", "дня", "дней")} подряд";

        int goal = settings.DailyGoal;
        double mins = repo.MinutesToday();
        GoalPercent = Math.Min(100, mins * 100.0 / Math.Max(1, goal));
        GoalText = $"{mins:0}/{goal}";
        GoalCaption = mins >= goal ? "Цель дня выполнена!" : $"Ещё {Math.Ceiling(goal - mins):0} мин до цели";

        var xp = repo.GetXpInfo();
        LevelLine = xp.Line;
        LevelPercent = xp.Fraction * 100;

        WeakSounds.Clear();
        foreach (var w in repo.GetWeakSounds(3)) WeakSounds.Add(w);
        OnPropertyChanged(nameof(HasWeak));
        OnPropertyChanged(nameof(NoWeak));
        Week = repo.GetWeekMinutes();

        Warning = !repo.ContentAvailable ? "База контента не найдена: " + repo.ContentError
                : AppServices.Engine.Error != null ? "Движок произношения не загружен: " + AppServices.Engine.Error : "";
    }

    public static string Plural(int n, string one, string few, string many)
    {
        var m100 = n % 100; var m10 = n % 10;
        if (m100 is >= 11 and <= 14) return many;
        return m10 == 1 ? one : m10 is >= 2 and <= 4 ? few : many;
    }
}
