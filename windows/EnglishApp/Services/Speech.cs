namespace EnglishApp.Services;

public static class Speech
{
    private static readonly WinRtTtsEngine Fallback = new();

    /// <summary>Speaks text with the configured accent. Returns false when no TTS is available.</summary>
    public static async Task<bool> SpeakAsync(string text, double rate)
    {
        var accent = AppServices.Settings.Accent;
        var engine = PronEngineService.Instance;
        await engine.Ready;
        byte[]? wav = null;
        if (engine.IsAvailable && engine.HasTts(accent))
        {
            try { wav = await engine.SynthesizeWavAsync(text, accent, rate); } catch { wav = null; }
        }
        // Windows voices only when the engine reports the TTS model missing (not on synthesis errors).
        else wav = await Fallback.SynthesizeAsync(text, accent, rate);
        if (wav == null) return false;
        AppServices.Player.PlayWavBytes(wav);
        return true;
    }
}
