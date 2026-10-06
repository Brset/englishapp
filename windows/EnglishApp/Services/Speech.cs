namespace EnglishApp.Services;

public static class Speech
{
    /// <summary>Speaks text with the configured accent. Returns false when no TTS is available.</summary>
    public static async Task<bool> SpeakAsync(string text, double rate)
    {
        var wav = await AppServices.Tts.SynthesizeAsync(text, AppServices.Settings.Accent, rate);
        if (wav == null) return false;
        AppServices.Player.PlayWavBytes(wav);
        return true;
    }
}
