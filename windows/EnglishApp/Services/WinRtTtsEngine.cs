using Windows.Media.SpeechSynthesis;
using Windows.Storage.Streams;

namespace EnglishApp.Services;

/// <summary>Temporary TTS on Windows.Media.SpeechSynthesis (installed system voices).</summary>
public sealed class WinRtTtsEngine : ITtsEngine
{
    public async Task<byte[]?> SynthesizeAsync(string text, string accent, double rate, CancellationToken ct = default)
    {
        if (string.IsNullOrWhiteSpace(text)) return null;
        try
        {
            using var synth = new SpeechSynthesizer();
            var lang = accent == "UK" ? "en-GB" : "en-US";
            var voice = SpeechSynthesizer.AllVoices.FirstOrDefault(v => string.Equals(v.Language, lang, StringComparison.OrdinalIgnoreCase))
                        ?? SpeechSynthesizer.AllVoices.FirstOrDefault(v => v.Language.StartsWith("en", StringComparison.OrdinalIgnoreCase));
            if (voice != null) synth.Voice = voice;
            synth.Options.SpeakingRate = Math.Clamp(rate, 0.5, 2.0);
            using var stream = await synth.SynthesizeTextToStreamAsync(text);
            using var reader = new DataReader(stream.GetInputStreamAt(0));
            var size = (uint)stream.Size;
            await reader.LoadAsync(size);
            var bytes = new byte[size];
            reader.ReadBytes(bytes);
            return bytes;
        }
        catch
        {
            return null;
        }
    }
}
