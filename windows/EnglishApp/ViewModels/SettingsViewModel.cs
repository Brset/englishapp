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

    [ObservableProperty] private int accentIndex;
    [ObservableProperty] private int strictnessIndex;
    [ObservableProperty] private int themeIndex;
    [ObservableProperty] private int micIndex;
    public string Info => $"pron_core: {(AppServices.Assessor != null ? Native.PronAssessor.Version : "не загружена")}";

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
        _loading = false;
    }

    partial void OnAccentIndexChanged(int value) { if (!_loading) AppServices.Settings.Accent = value == 1 ? "UK" : "US"; }
    partial void OnStrictnessIndexChanged(int value)
    {
        if (_loading) return;
        AppServices.Settings.Strictness = value;
        AppServices.Assessor?.SetStrictness((Native.Strictness)value);
    }
    partial void OnMicIndexChanged(int value) { if (!_loading && value >= 0) AppServices.Settings.MicDevice = value; }
    partial void OnThemeIndexChanged(int value)
    {
        if (_loading) return;
        var t = value switch { 1 => "Light", 2 => "Dark", _ => "Default" };
        AppServices.Settings.Theme = t;
        App.MainWindow.ApplyTheme(t);
    }
}
