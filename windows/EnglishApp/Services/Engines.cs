namespace EnglishApp.Services;

public interface ITtsEngine
{
    /// <summary>Synthesizes speech and returns WAV bytes (null when unavailable). accent: "US" | "UK"; rate 0.5..1.25.</summary>
    Task<byte[]?> SynthesizeAsync(string text, string accent, double rate, CancellationToken ct = default);
}
