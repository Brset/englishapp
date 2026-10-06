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

    public bool HasWarning => !string.IsNullOrEmpty(Warning);
    partial void OnWarningChanged(string value) => OnPropertyChanged(nameof(HasWarning));
    public bool HasContinue => ContinueText != null;
    public bool HasToday => TodayText != null;
    partial void OnContinueTextChanged(TextSummary? value) => OnPropertyChanged(nameof(HasContinue));
    partial void OnTodayTextChanged(TextSummary? value) => OnPropertyChanged(nameof(HasToday));

    public void Load()
    {
        var repo = AppServices.Repo;
        ContinueText = repo.GetContinueText();
        TodayText = repo.GetTextOfTheDay();
        var s = repo.GetStreak();
        StreakText = s == 0 ? "Серия: начните сегодня" : $"Серия: {s} {Plural(s, "день", "дня", "дней")} подряд";
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
