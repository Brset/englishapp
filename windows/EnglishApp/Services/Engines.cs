namespace EnglishApp.Services;

/// <summary>Result of a phoneme model run: row-major [Frames x Classes] log-softmax.</summary>
public sealed record PhonemePosteriors(float[] LogPosteriors, int Frames, int Classes, double FrameSeconds,
    IReadOnlyList<string> Labels, int BlankIndex);

public interface IAsrEngine
{
    /// <summary>Recognizes words with timestamps from a 16 kHz mono WAV. Empty list when unavailable.</summary>
    Task<IReadOnlyList<Native.AsrWord>> TranscribeAsync(string wavPath, string? referenceText, CancellationToken ct = default);
}

public interface IPhonemeEngine
{
    /// <summary>Phoneme log-posteriors for the whole recording, or null when no model is available.</summary>
    Task<PhonemePosteriors?> ComputeAsync(string wavPath, CancellationToken ct = default);
}

public interface ITtsEngine
{
    /// <summary>Synthesizes speech and returns WAV bytes (null when unavailable). accent: "US" | "UK"; rate 0.5..1.25.</summary>
    Task<byte[]?> SynthesizeAsync(string text, string accent, double rate, CancellationToken ct = default);
}

// TODO: replace with whisper.cpp (ASR), onnxruntime wav2vec2 phoneme model, piper (TTS).
public sealed class StubAsrEngine : IAsrEngine
{
    public Task<IReadOnlyList<Native.AsrWord>> TranscribeAsync(string wavPath, string? referenceText, CancellationToken ct = default)
        => Task.FromResult<IReadOnlyList<Native.AsrWord>>(Array.Empty<Native.AsrWord>());
}

public sealed class StubPhonemeEngine : IPhonemeEngine
{
    public Task<PhonemePosteriors?> ComputeAsync(string wavPath, CancellationToken ct = default)
        => Task.FromResult<PhonemePosteriors?>(null);
}

public sealed class StubTtsEngine : ITtsEngine
{
    public Task<byte[]?> SynthesizeAsync(string text, string accent, double rate, CancellationToken ct = default)
        => Task.FromResult<byte[]?>(null);
}
