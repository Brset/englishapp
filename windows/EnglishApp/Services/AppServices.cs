using EnglishApp.Native;

namespace EnglishApp.Services;

/// <summary>Poor man's service locator; created once at startup.</summary>
public static class AppServices
{
    public static ContentRepository Repo { get; private set; } = null!;
    public static AudioPlayer Player { get; } = new();
    public static AudioRecorder Recorder { get; } = new();
    public static IAsrEngine Asr { get; set; } = new StubAsrEngine();
    public static IPhonemeEngine Phoneme { get; set; } = new StubPhonemeEngine();
    public static ITtsEngine Tts { get; set; } = new StubTtsEngine();
    public static PronAssessor? Assessor { get; private set; }
    public static string? NativeError { get; private set; }
    public static SettingsService Settings { get; private set; } = null!;

    private static bool _vocabSet;

    public static void Init()
    {
        Repo = new ContentRepository(AppPaths.UserDb, AppPaths.ContentDb, AppPaths.UserSchema);
        Settings = new SettingsService(Repo);
        try
        {
            Assessor = new PronAssessor();
            if (File.Exists(AppPaths.CmuDict)) Assessor.LoadCmudictFile(AppPaths.CmuDict);
            Assessor.SetStrictness((Strictness)Settings.Strictness);
        }
        catch (Exception ex) when (ex is DllNotFoundException or BadImageFormatException or EntryPointNotFoundException or PronException)
        {
            NativeError = ex.Message;
            Assessor = null;
        }
    }

    /// <summary>Feeds the phoneme model's labels to the assessor once.</summary>
    public static void EnsurePhonemeVocab(PhonemePosteriors p)
    {
        if (_vocabSet || Assessor == null) return;
        Assessor.SetPhonemeVocab(p.Labels, p.BlankIndex);
        _vocabSet = true;
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
    public double Speed
    {
        get => double.TryParse(_repo.GetSetting("speed"), System.Globalization.NumberStyles.Float,
            System.Globalization.CultureInfo.InvariantCulture, out var v) ? Math.Clamp(v, 0.5, 1.25) : 1.0;
        set => _repo.SetSetting("speed", value.ToString(System.Globalization.CultureInfo.InvariantCulture));
    }
}
