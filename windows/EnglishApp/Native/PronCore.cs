using System.Runtime.InteropServices;
using System.Text;
using System.Text.Json;
using System.Text.Json.Serialization;
using Microsoft.Win32.SafeHandles;

namespace EnglishApp.Native;

/// <summary>Owns a pron_assessor*; destroyed via pron_assessor_destroy.</summary>
public sealed class AssessorHandle : SafeHandleZeroOrMinusOneIsInvalid
{
    public AssessorHandle() : base(true) { }
    protected override bool ReleaseHandle()
    {
        NativeMethods.pron_assessor_destroy(handle);
        return true;
    }
}

[StructLayout(LayoutKind.Sequential)]
public struct PronWordNative
{
    public IntPtr text;     // const char* (UTF-8)
    public double start;
    public double end;
    public float probability;
}

/// <summary>Raw P/Invoke declarations, one per function in pron_c.h.</summary>
internal static class NativeMethods
{
    private const string Lib = "pron";   // pron.dll (core: OUTPUT_NAME pron)
    private const CallingConvention Cc = CallingConvention.Cdecl;

    [DllImport(Lib, CallingConvention = Cc)] public static extern IntPtr pron_version();
    [DllImport(Lib, CallingConvention = Cc)] public static extern AssessorHandle pron_assessor_create();
    [DllImport(Lib, CallingConvention = Cc)] public static extern void pron_assessor_destroy(IntPtr a);
    [DllImport(Lib, CallingConvention = Cc)] public static extern IntPtr pron_last_error(AssessorHandle a);

    [DllImport(Lib, CallingConvention = Cc)]
    public static extern int pron_assessor_load_cmudict_file(AssessorHandle a, [MarshalAs(UnmanagedType.LPUTF8Str)] string path);

    [DllImport(Lib, CallingConvention = Cc)]
    public static extern int pron_assessor_load_cmudict_text(AssessorHandle a, byte[] text, UIntPtr length);

    [DllImport(Lib, CallingConvention = Cc)]
    public static extern int pron_assessor_add_pronunciation(AssessorHandle a,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string word, [MarshalAs(UnmanagedType.LPUTF8Str)] string arpabet);

    [DllImport(Lib, CallingConvention = Cc)]
    public static extern int pron_assessor_set_phoneme_vocab(AssessorHandle a, IntPtr[] labels, int count, int blankIndex);

    [DllImport(Lib, CallingConvention = Cc)] public static extern void pron_assessor_set_strictness(AssessorHandle a, int strictness);
    [DllImport(Lib, CallingConvention = Cc)] public static extern void pron_assessor_set_long_pause(AssessorHandle a, double seconds);

    [DllImport(Lib, CallingConvention = Cc)]
    public static extern IntPtr pron_assess(AssessorHandle a, [MarshalAs(UnmanagedType.LPUTF8Str)] string reference,
        PronWordNative[]? words, int nWords, float[]? logPosteriors, int nFrames, int nClasses, double frameSeconds);

    [DllImport(Lib, CallingConvention = Cc)]
    public static extern IntPtr pron_tokenize([MarshalAs(UnmanagedType.LPUTF8Str)] string text);

    [DllImport(Lib, CallingConvention = Cc)]
    public static extern IntPtr pron_assessor_lookup(AssessorHandle a, [MarshalAs(UnmanagedType.LPUTF8Str)] string word);

    [DllImport(Lib, CallingConvention = Cc)] public static extern void pron_free_string(IntPtr s);
}

public enum Strictness { Lenient = 0, Normal = 1, Strict = 2 }

public sealed record AsrWord(string Text, double Start, double End, float Probability = 1f);

public sealed class PronException : Exception
{
    public PronException(string message) : base(message) { }
}

/// <summary>Managed wrapper over the C API. Not thread-safe per handle; calls are serialized by a lock.</summary>
public sealed class PronAssessor : IDisposable
{
    public static readonly JsonSerializerOptions Json = new()
    {
        PropertyNamingPolicy = JsonNamingPolicy.SnakeCaseLower,
        PropertyNameCaseInsensitive = true,
        NumberHandling = JsonNumberHandling.AllowReadingFromString,
    };

    private readonly AssessorHandle _h;
    private readonly object _lock = new();

    public PronAssessor()
    {
        _h = NativeMethods.pron_assessor_create();
        if (_h.IsInvalid) throw new PronException("pron_assessor_create failed");
    }

    public static string Version => Marshal.PtrToStringUTF8(NativeMethods.pron_version()) ?? "";

    public string LastError => Marshal.PtrToStringUTF8(NativeMethods.pron_last_error(_h)) ?? "";

    private static string? Take(IntPtr p)
    {
        if (p == IntPtr.Zero) return null;
        try { return Marshal.PtrToStringUTF8(p); }
        finally { NativeMethods.pron_free_string(p); }
    }

    private string TakeOrThrow(IntPtr p)
    {
        var s = Take(p);
        if (s == null) throw new PronException(LastError);
        return s;
    }

    public int LoadCmudictFile(string path) { lock (_lock) return NativeMethods.pron_assessor_load_cmudict_file(_h, path); }

    public int LoadCmudictText(string text)
    {
        var bytes = Encoding.UTF8.GetBytes(text);
        lock (_lock) return NativeMethods.pron_assessor_load_cmudict_text(_h, bytes, (UIntPtr)bytes.Length);
    }

    public bool AddPronunciation(string word, string arpabet)
    {
        lock (_lock) return NativeMethods.pron_assessor_add_pronunciation(_h, word, arpabet) == 0;
    }

    /// <summary>Returns the number of unmapped labels (special tokens expected), or -1 on error.</summary>
    public int SetPhonemeVocab(IReadOnlyList<string> labels, int blankIndex)
    {
        var ptrs = new IntPtr[labels.Count];
        try
        {
            for (int i = 0; i < ptrs.Length; i++) ptrs[i] = Marshal.StringToCoTaskMemUTF8(labels[i]);
            lock (_lock) return NativeMethods.pron_assessor_set_phoneme_vocab(_h, ptrs, ptrs.Length, blankIndex);
        }
        finally
        {
            foreach (var p in ptrs) if (p != IntPtr.Zero) Marshal.FreeCoTaskMem(p);
        }
    }

    public void SetStrictness(Strictness s) { lock (_lock) NativeMethods.pron_assessor_set_strictness(_h, (int)s); }
    public void SetLongPause(double seconds) { lock (_lock) NativeMethods.pron_assessor_set_long_pause(_h, seconds); }

    /// <summary>Raw JSON result of pron_assess.</summary>
    public string AssessJson(string reference, IReadOnlyList<AsrWord>? words,
        float[]? logPosteriors = null, int nFrames = 0, int nClasses = 0, double frameSeconds = 0.02)
    {
        var native = new PronWordNative[words?.Count ?? 0];
        try
        {
            for (int i = 0; i < native.Length; i++)
            {
                var w = words![i];
                native[i] = new PronWordNative
                {
                    text = Marshal.StringToCoTaskMemUTF8(w.Text),
                    start = w.Start, end = w.End, probability = w.Probability,
                };
            }
            lock (_lock)
            {
                var p = NativeMethods.pron_assess(_h, reference, native.Length == 0 ? null : native, native.Length,
                    logPosteriors, nFrames, nClasses, frameSeconds);
                return TakeOrThrow(p);
            }
        }
        finally
        {
            foreach (var n in native) if (n.text != IntPtr.Zero) Marshal.FreeCoTaskMem(n.text);
        }
    }

    public AssessmentResult Assess(string reference, IReadOnlyList<AsrWord>? words,
        float[]? logPosteriors = null, int nFrames = 0, int nClasses = 0, double frameSeconds = 0.02)
        => ParseResult(AssessJson(reference, words, logPosteriors, nFrames, nClasses, frameSeconds));

    public static AssessmentResult ParseResult(string json)
        => JsonSerializer.Deserialize<AssessmentResult>(json, Json) ?? throw new PronException("empty result");

    public static IReadOnlyList<TokenInfo> Tokenize(string text)
    {
        var s = Take(NativeMethods.pron_tokenize(text));
        return s == null ? Array.Empty<TokenInfo>() : JsonSerializer.Deserialize<List<TokenInfo>>(s, Json) ?? new();
    }

    public LookupResult? Lookup(string word)
    {
        string? s;
        lock (_lock) s = Take(NativeMethods.pron_assessor_lookup(_h, word));
        return s == null ? null : JsonSerializer.Deserialize<LookupResult>(s, Json);
    }

    public void Dispose() => _h.Dispose();
}

// ---------- JSON records (core/src/result_json.cpp) ----------

/// <summary>Accepts both JSON strings and numbers for id fields.</summary>
public sealed class FlexStringConverter : JsonConverter<string?>
{
    public override string? Read(ref Utf8JsonReader r, Type t, JsonSerializerOptions o) => r.TokenType switch
    {
        JsonTokenType.String => r.GetString(),
        JsonTokenType.Number => r.GetDouble().ToString(System.Globalization.CultureInfo.InvariantCulture),
        JsonTokenType.True => "true",
        JsonTokenType.False => "false",
        _ => null,
    };
    public override void Write(Utf8JsonWriter w, string? v, JsonSerializerOptions o) => w.WriteStringValue(v);
}

public sealed record TokenInfo
{
    public string Text { get; init; } = "";
    public string Norm { get; init; } = "";
    public int ByteBegin { get; init; }
    public int ByteEnd { get; init; }
    [JsonPropertyName("u16_begin")] public int U16Begin { get; init; }
    [JsonPropertyName("u16_end")] public int U16End { get; init; }
    public bool FromNumber { get; init; }
}

public sealed record LookupResult
{
    public string Word { get; init; } = "";
    public string Source { get; init; } = "";
    public string Arpabet { get; init; } = "";
    public string Ipa { get; init; } = "";
    public int StressSyllable { get; init; }
}

public sealed record Scores
{
    public double Accuracy { get; init; }
    public double Completeness { get; init; }
    public double Fluency { get; init; }
    public double Overall { get; init; }
}

public sealed record PhonemeResult
{
    public string Arpabet { get; init; } = "";
    public string Ipa { get; init; } = "";
    public int Stress { get; init; }
    public double? Score { get; init; }
    public double Gop { get; init; }
    public double? Start { get; init; }
    public double? End { get; init; }
    public bool Substituted { get; init; }
    public string ActualArpabet { get; init; } = "";
    public string ActualIpa { get; init; } = "";
    [JsonConverter(typeof(FlexStringConverter))] public string? AdviceId { get; init; }
}

public sealed record WordResult
{
    public int Index { get; init; }
    public string Text { get; init; } = "";
    public string Norm { get; init; } = "";
    public int ByteBegin { get; init; }
    public int ByteEnd { get; init; }
    [JsonPropertyName("u16_begin")] public int U16Begin { get; init; }
    [JsonPropertyName("u16_end")] public int U16End { get; init; }
    public string Status { get; init; } = "";
    public string Recognized { get; init; } = "";
    public double Similarity { get; init; }
    public double? Start { get; init; }
    public double? End { get; init; }
    public double Score { get; init; }
    public string Band { get; init; } = "";
    public string Color { get; init; } = "";
    public string PronSource { get; init; } = "";
    public string ExpectedIpa { get; init; } = "";
    public int StressSyllable { get; init; }
    public string ScoredBy { get; init; } = "";
    public List<PhonemeResult> Phonemes { get; init; } = new();
}

public sealed record InsertedWord
{
    public string Text { get; init; } = "";
    public double Start { get; init; }
    public double End { get; init; }
    public int AfterWord { get; init; }
}

public sealed record LongPause
{
    public int AfterWord { get; init; }
    public double Start { get; init; }
    public double End { get; init; }
    public double Duration { get; init; }
}

public sealed record FluencyInfo
{
    public int WordCount { get; init; }
    public double SpeechSeconds { get; init; }
    public double ArticulationSeconds { get; init; }
    public double WordsPerMinute { get; init; }
    public double ArticulationWpm { get; init; }
    public List<LongPause> LongPauses { get; init; } = new();
    public int Repetitions { get; init; }
    public int Hesitations { get; init; }
}

public sealed record AdviceItem
{
    [JsonConverter(typeof(FlexStringConverter))] public string Id { get; init; } = "";
    [JsonConverter(typeof(FlexStringConverter))] public string SoundId { get; init; } = "";
    public string ExpectedIpa { get; init; } = "";
    public string ActualIpa { get; init; } = "";
    public string TitleRu { get; init; } = "";
    public string TipRu { get; init; } = "";
    public int Count { get; init; }
    public List<int> Words { get; init; } = new();
}

public sealed record AssessmentResult
{
    public int Version { get; init; }
    public Scores Scores { get; init; } = new();
    public bool PhonemeLevel { get; init; }
    public List<WordResult> Words { get; init; } = new();
    public List<InsertedWord> Inserted { get; init; } = new();
    public FluencyInfo Fluency { get; init; } = new();
    public List<AdviceItem> Advice { get; init; } = new();
    public List<string> Warnings { get; init; } = new();
}
