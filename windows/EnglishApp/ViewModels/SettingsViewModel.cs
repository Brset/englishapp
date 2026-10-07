using System.Collections.ObjectModel;
using CommunityToolkit.Mvvm.ComponentModel;
using EnglishApp.Services;

namespace EnglishApp.ViewModels;

public partial class SettingsViewModel : ObservableObject
{
    public IReadOnlyList<string> Accents { get; } = new[] { "US", "UK" };
    public IReadOnlyList<string> Strictnesses { get; } = new[] { "Мягкая", "Обычная", "Строгая" };
    public IReadOnlyList<string> Themes { get; } = new[] { "Как в системе", "Светлая", "Тёмная" };
    public ObservableCollection<string> Mics { get; } = new();
    private static readonly int[] GoalValues = { 5, 10, 15, 20, 30, 45, 60 };
    public IReadOnlyList<string> Goals { get; } = GoalValues.Select(g => $"{g} мин в день").ToList();
    public IReadOnlyList<string> LevelNames { get; } = new[] { "A1", "A2", "B1", "B2", "C1", "C2" };
    [ObservableProperty] private int goalIndex;
    [ObservableProperty] private int levelIndex;

    [ObservableProperty] private int accentIndex;
    [ObservableProperty] private int strictnessIndex;
    [ObservableProperty] private int themeIndex;
    [ObservableProperty] private int micIndex;
    [ObservableProperty] private string engineStatus = "Движок произношения: загрузка…";

    private bool _loading;

    public SettingsViewModel()
    {
        _loading = true;
        var s = AppServices.Settings;
        AccentIndex = s.Accent == "UK" ? 1 : 0;
        StrictnessIndex = Math.Clamp(s.Strictness, 0, 2);
        ThemeIndex = s.Theme switch { "Light" => 1, "Dark" => 2, _ => 0 };
        foreach (var m in AudioRecorder.GetDevices()) Mics.Add(m);
        if (Mics.Count == 0) Mics.Add("Микрофон не найден");
        MicIndex = Math.Clamp(s.MicDevice, 0, Mics.Count - 1);
        int gi = Array.IndexOf(GoalValues, s.DailyGoal);
        GoalIndex = gi >= 0 ? gi : 1;
        LevelIndex = Math.Max(0, Array.IndexOf(LevelNames.ToArray(), s.Level));
        _loading = false;
        _ = LoadEngineStatusAsync();
    }

    private async Task LoadEngineStatusAsync()
    {
        await AppServices.Engine.Ready;
        var e = AppServices.Engine;
        EngineStatus = e.Status != null ? e.Status.ToRussian()
            : "Движок произношения не загружен" + (e.Error != null ? ": " + e.Error : "");
    }

    partial void OnAccentIndexChanged(int value) { if (!_loading) AppServices.Settings.Accent = value == 1 ? "UK" : "US"; }
    partial void OnStrictnessIndexChanged(int value)
    {
        if (_loading) return;
        AppServices.Settings.Strictness = value;
        _ = AppServices.Engine.SetStrictnessAsync(value);
    }
    partial void OnGoalIndexChanged(int value) { if (!_loading && value >= 0) AppServices.Settings.DailyGoal = GoalValues[value]; }
    partial void OnLevelIndexChanged(int value) { if (!_loading && value >= 0) AppServices.Settings.Level = LevelNames[value]; }
    partial void OnMicIndexChanged(int value) { if (!_loading && value >= 0) AppServices.Settings.MicDevice = value; }
    partial void OnThemeIndexChanged(int value)
    {
        if (_loading) return;
        var t = value switch { 1 => "Light", 2 => "Dark", _ => "Default" };
        AppServices.Settings.Theme = t;
        App.MainWindow.ApplyTheme(t);
    }
}
