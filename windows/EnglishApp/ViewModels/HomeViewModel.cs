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

    // ---- presentation-only (read-only data from ContentRepository) ----
    [ObservableProperty] private string greeting = "Добро пожаловать";
    [ObservableProperty] private string streakValue = "0";
    [ObservableProperty] private string streakCaption = "";
    [ObservableProperty] private string textsDoneValue = "0";
    [ObservableProperty] private string textsDoneCaption = "";
    [ObservableProperty] private string avgBestValue = "—";
    [ObservableProperty] private string wordsDueValue = "0";
    [ObservableProperty] private string wordsDueCaption = "";
    public string ContinueLevel => ContinueText?.Level ?? "";
    public string TodayLevel => TodayText?.Level ?? "";
    public bool NoContinue => ContinueText == null;
    public bool NoToday => TodayText == null;

    public bool HasWarning => !string.IsNullOrEmpty(Warning);
    partial void OnWarningChanged(string value) => OnPropertyChanged(nameof(HasWarning));
    public bool HasContinue => ContinueText != null;
    public bool HasToday => TodayText != null;
    partial void OnContinueTextChanged(TextSummary? value)
    {
        OnPropertyChanged(nameof(HasContinue));
        OnPropertyChanged(nameof(NoContinue));
        OnPropertyChanged(nameof(ContinueLevel));
    }
    partial void OnTodayTextChanged(TextSummary? value)
    {
        OnPropertyChanged(nameof(HasToday));
        OnPropertyChanged(nameof(NoToday));
        OnPropertyChanged(nameof(TodayLevel));
    }

    public void Load()
    {
        var repo = AppServices.Repo;
        ContinueText = repo.GetContinueText();
        TodayText = repo.GetTextOfTheDay();
        var s = repo.GetStreak();
        StreakText = s == 0 ? "Серия: начните сегодня" : $"Серия: {s} {Plural(s, "день", "дня", "дней")} подряд";
        Warning = !repo.ContentAvailable ? "База контента не найдена: " + repo.ContentError
                : AppServices.Engine.Error != null ? "Движок произношения не загружен: " + AppServices.Engine.Error : "";

        Greeting = GreetingFor(DateTime.Now.Hour);
        StreakValue = s.ToString();
        StreakCaption = s == 0 ? "начните серию сегодня" : Plural(s, "день подряд", "дня подряд", "дней подряд");
        try
        {
            var st = repo.GetStats();
            TextsDoneValue = st.TextsDone.ToString();
            TextsDoneCaption = $"из {st.TextsTotal} текстов пройдено";
            AvgBestValue = st.TextsDone + st.TextsStarted > 0 && st.AvgBest > 0 ? $"{st.AvgBest:0}%" : "—";
            WordsDueValue = st.WordsDue.ToString();
            WordsDueCaption = st.WordsSaved == 0 ? "слов в словаре пока нет"
                : $"{Plural(st.WordsDue, "слово", "слова", "слов")} к повторению из {st.WordsSaved}";
        }
        catch
        {
            // Stats are decorative on the home page; never block it.
        }
    }

    public static string GreetingFor(int hour) => hour switch
    {
        >= 5 and < 12 => "Доброе утро",
        >= 12 and < 18 => "Добрый день",
        >= 18 and < 23 => "Добрый вечер",
        _ => "Доброй ночи",
    };

    public static string Plural(int n, string one, string few, string many)
    {
        var m100 = n % 100; var m10 = n % 10;
        if (m100 is >= 11 and <= 14) return many;
        return m10 == 1 ? one : m10 is >= 2 and <= 4 ? few : many;
    }
}
