namespace EnglishApp.Services;

/// <summary>Poor man's service locator; created once at startup.</summary>
public static class AppServices
{
    public static ContentRepository Repo { get; private set; } = null!;
    public static AudioPlayer Player { get; } = new();
    public static AudioRecorder Recorder { get; } = new();
    public static SettingsService Settings { get; private set; } = null!;

    public static ProcessingQueue Jobs { get; private set; } = null!;

    public static PronEngineService Engine => PronEngineService.Instance;

    public static void Init()
    {
        Repo = new ContentRepository(AppPaths.UserDb, AppPaths.ContentDb, AppPaths.UserSchema);
        Settings = new SettingsService(Repo);
        Engine.StartInit(AppPaths.ModelsDir, Settings.Strictness);
        Jobs = new ProcessingQueue(Repo);   // must be created on the UI thread (captures its DispatcherQueue)
    }
}

public sealed class SettingsService
{
    private readonly ContentRepository _repo;
    public SettingsService(ContentRepository repo) => _repo = repo;

    public string Accent { get => _repo.GetSetting("accent") ?? "US"; set => _repo.SetSetting("accent", value); }
    public int Strictness { get => int.TryParse(_repo.GetSetting("strictness"), out var v) ? v : 1; set => _repo.SetSetting("strictness", value.ToString()); }
    public int MicDevice { get => int.TryParse(_repo.GetSetting("mic"), out var v) ? v : 0; set => _repo.SetSetting("mic", value.ToString()); }
    public string Theme { get => _repo.GetSetting("theme") ?? "Default"; set => _repo.SetSetting("theme", value); }
    public int DailyGoal { get => int.TryParse(_repo.GetSetting("daily_goal"), out var v) ? Math.Clamp(v, 1, 180) : 10; set => _repo.SetSetting("daily_goal", value.ToString()); }
    public string Level { get => _repo.GetSetting("level") ?? "A1"; set => _repo.SetSetting("level", value); }
    public bool Onboarded { get => _repo.GetSetting("onboarded") == "1"; set => _repo.SetSetting("onboarded", value ? "1" : "0"); }
    public int FontSizeIndex { get => int.TryParse(_repo.GetSetting("font_size"), out var v) ? Math.Clamp(v, 0, 3) : 1; set => _repo.SetSetting("font_size", value.ToString()); }
    public int LineSpacingIndex { get => int.TryParse(_repo.GetSetting("line_spacing"), out var v) ? Math.Clamp(v, 0, 2) : 1; set => _repo.SetSetting("line_spacing", value.ToString()); }
    public double Speed
    {
        get => double.TryParse(_repo.GetSetting("speed"), System.Globalization.NumberStyles.Float,
            System.Globalization.CultureInfo.InvariantCulture, out var v) ? Math.Clamp(v, 0.5, 1.25) : 1.0;
        set => _repo.SetSetting("speed", value.ToString(System.Globalization.CultureInfo.InvariantCulture));
    }
}
