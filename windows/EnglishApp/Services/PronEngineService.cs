using System.Runtime.InteropServices;
using System.Text.Json;
using EnglishApp.Native;
using NAudio.Wave;

namespace EnglishApp.Services;

/// <summary>Parsed pron_engine_status JSON.</summary>
public sealed record EngineStatus(string Version, bool Asr, bool Vad, bool Phoneme, bool Cmudict,
    bool TtsUs, bool TtsGb, IReadOnlyDictionary<string, string> Errors)
{
    public static EngineStatus Parse(string json)
    {
        using var doc = JsonDocument.Parse(json);
        var r = doc.RootElement;
        bool B(JsonElement e, string n) => e.ValueKind == JsonValueKind.Object && e.TryGetProperty(n, out var v) && v.ValueKind == JsonValueKind.True;
        var tts = r.TryGetProperty("tts", out var t) ? t : default;
        var errors = new Dictionary<string, string>();
        if (r.TryGetProperty("errors", out var er) && er.ValueKind == JsonValueKind.Object)
            foreach (var p in er.EnumerateObject()) errors[p.Name] = p.Value.ToString();
        return new EngineStatus(
            r.TryGetProperty("version", out var ver) ? ver.ToString() : "",
            B(r, "asr"), B(r, "vad"), B(r, "phoneme"), B(r, "cmudict"), B(tts, "us"), B(tts, "gb"), errors);
    }

    public string ToRussian()
    {
        static string L(string name, bool ok) => $"{name}: {(ok ? "загружен" : "не загружен")}";
        var lines = new List<string>
        {
            $"Движок произношения {Version}",
            L("Распознавание речи (whisper)", Asr),
            L("Детектор речи (VAD)", Vad),
            L("Фонемная модель (wav2vec2)", Phoneme),
            L("Словарь CMUdict", Cmudict),
            L("Озвучка US", TtsUs),
            L("Озвучка UK", TtsGb),
        };
        foreach (var kv in Errors) lines.Add($"Ошибка ({kv.Key}): {kv.Value}");
        return string.Join(Environment.NewLine, lines);
    }
}

/// <summary>Singleton wrapper over pron_engine. The native handle is not thread-safe: all calls go through a semaphore.</summary>
public sealed class PronEngineService
{
    private readonly SemaphoreSlim _gate = new(1, 1);
    private EngineHandle? _h;
    private readonly TaskCompletionSource _ready = new(TaskCreationOptions.RunContinuationsAsynchronously);

    public static PronEngineService Instance { get; } = new();

    public EngineStatus? Status { get; private set; }
    public string? Error { get; private set; }
    public bool IsAvailable => _h is { IsInvalid: false };
    public Task Ready => _ready.Task;

    /// <summary>Creates the engine on a background thread; never throws.</summary>
    public void StartInit(string modelsDir, int strictness)
    {
        Task.Run(() =>
        {
            try
            {
                var h = NativeMethods.pron_engine_create(modelsDir, 0);
                if (h.IsInvalid) throw new PronException("pron_engine_create failed (папка models: " + modelsDir + ")");
                NativeMethods.pron_engine_set_strictness(h, strictness);
                var js = Take(NativeMethods.pron_engine_status(h));
                Status = js == null ? null : EngineStatus.Parse(js);
                _h = h;
            }
            catch (Exception ex)
            {
                Error = ex is DllNotFoundException or BadImageFormatException or EntryPointNotFoundException
                    ? "Не удалось загрузить pron_engine.dll: " + ex.Message : ex.Message;
            }
            finally { _ready.TrySetResult(); }
        });
    }

    private static string? Take(IntPtr p)
    {
        if (p == IntPtr.Zero) return null;
        try { return Marshal.PtrToStringUTF8(p); }
        finally { NativeMethods.pron_free_string(p); }
    }

    private string LastError() => _h == null ? "" : Marshal.PtrToStringUTF8(NativeMethods.pron_engine_last_error(_h)) ?? "";

    public bool HasTts(string accent) => Status != null && (accent == "UK" ? Status.TtsGb : Status.TtsUs);

    public async Task SetStrictnessAsync(int strictness)
    {
        await Ready;
        if (_h == null) return;
        await _gate.WaitAsync();
        try { NativeMethods.pron_engine_set_strictness(_h, strictness); }
        finally { _gate.Release(); }
    }

    /// <summary>Assesses a 16 kHz mono 16-bit WAV recording; returns the result JSON.</summary>
    public async Task<string> AssessWavAsync(string wavPath, string reference, int strictness)
    {
        await Ready;
        if (_h == null) throw new InvalidOperationException("Движок не загружен: " + Error);
        var h = _h;
        return await Task.Run(async () =>
        {
            short[] samples;
            int rate;
            using (var r = new WaveFileReader(wavPath))
            {
                rate = r.WaveFormat.SampleRate;
                var bytes = new byte[r.Length];
                int got = 0, n;
                while (got < bytes.Length && (n = r.Read(bytes, got, bytes.Length - got)) > 0) got += n;
                samples = new short[got / 2];
                Buffer.BlockCopy(bytes, 0, samples, 0, samples.Length * 2);
            }
            await _gate.WaitAsync();
            try
            {
                NativeMethods.pron_engine_set_strictness(h, strictness);
                var p = NativeMethods.pron_engine_assess_pcm16(h, samples, (UIntPtr)samples.Length, rate, reference);
                var s = Take(p);
                if (s == null) throw new PronException(LastError());
                return s;
            }
            finally { _gate.Release(); }
        });
    }

    /// <summary>Synthesizes speech; returns 16-bit mono WAV bytes or null (engine/voice missing or error).</summary>
    public async Task<byte[]?> SynthesizeWavAsync(string text, string accent, double speed)
    {
        await Ready;
        if (_h == null || string.IsNullOrWhiteSpace(text) || !HasTts(accent)) return null;
        var h = _h;
        var voice = accent == "UK" ? "gb" : "us";
        return await Task.Run(async () =>
        {
            float[] pcm; int rate;
            await _gate.WaitAsync();
            try
            {
                var p = NativeMethods.pron_engine_tts(h, text, voice, (float)Math.Clamp(speed, 0.5, 1.5), out var count, out rate);
                if (p == IntPtr.Zero) return null;
                try
                {
                    pcm = new float[(int)count];
                    if (pcm.Length > 0) Marshal.Copy(p, pcm, 0, pcm.Length);
                }
                finally { NativeMethods.pron_engine_free_audio(p); }
            }
            finally { _gate.Release(); }
            using var ms = new MemoryStream();
            using (var w = new WaveFileWriter(ms, new WaveFormat(rate, 16, 1)))
            {
                var buf = new byte[pcm.Length * 2];
                for (int i = 0; i < pcm.Length; i++)
                {
                    var v = (short)Math.Clamp((int)Math.Round(pcm[i] * 32767f), short.MinValue, short.MaxValue);
                    buf[2 * i] = (byte)(v & 0xFF); buf[2 * i + 1] = (byte)((v >> 8) & 0xFF);
                }
                w.Write(buf, 0, buf.Length);
            }
            return ms.ToArray();
        });
    }

    /// <summary>Dictionary lookup (IPA); null when unavailable. Non-blocking: returns null while the engine is still loading.</summary>
    public LookupResult? Lookup(string word)
    {
        if (!_ready.Task.IsCompleted || _h == null) return null;
        if (!_gate.Wait(0)) return null;   // busy (assess/tts running): do not block the UI thread
        try
        {
            var s = Take(NativeMethods.pron_engine_lookup(_h, word));
            return s == null ? null : JsonSerializer.Deserialize<LookupResult>(s, PronAssessor.Json);
        }
        catch (Exception) { return null; }
        finally { _gate.Release(); }
    }
}
