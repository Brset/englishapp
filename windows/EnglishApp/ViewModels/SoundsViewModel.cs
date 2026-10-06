using System.Collections.ObjectModel;
using System.Text.Json;
using CommunityToolkit.Mvvm.ComponentModel;
using EnglishApp.Models;
using EnglishApp.Services;

namespace EnglishApp.ViewModels;

public sealed record DetailLine(string Heading, string Text);

// Presentation records for the structured sound card.
public sealed record SoundStep(int Number, string Text) { public string NumberText => Number.ToString(); }
public sealed record SoundError(string Substitute, string HeardAs, string Tip)
{
    public string SubstituteText => $"Вместо звука получается: {Substitute}";
    public string HeardAsText => string.IsNullOrEmpty(HeardAs) ? "" : $"Слышится как: {HeardAs}";
}
public sealed record SoundPair(string Target, string Contrast, string ContrastSound)
{
    public string Label => $"{Target} · {Contrast}";
    public string SpeakText => $"{Target}. {Contrast}.";
}
public sealed record SoundExample(string Word, string Ipa, string Translation)
{
    public string IpaText => string.IsNullOrEmpty(Ipa) ? "" : $"/{Ipa}/";
}
public sealed record SoundLine(string Text);

public partial class SoundsViewModel : ObservableObject
{
    private static readonly Dictionary<string, string> Titles = new()
    {
        ["articulation"] = "Артикуляция", ["minimal_pairs"] = "Минимальные пары", ["examples"] = "Примеры",
        ["tips"] = "Советы", ["common_errors"] = "Типичные ошибки", ["description"] = "Описание",
        ["difficulty"] = "Сложность", ["name_ru"] = "Название", ["ipa"] = "Транскрипция", ["words"] = "Слова",
        ["stress_pairs"] = "Пары слов с разным ударением", ["linking_phrases"] = "Фразы со связкой",
        ["articulation_ru"] = "Артикуляция", ["typical_errors"] = "Типичные ошибки", ["example_words"] = "Слова-примеры",
        ["phrases"] = "Фразы", ["tongue_twisters"] = "Скороговорки", ["us_uk_note_ru"] = "США и Великобритания",
        ["difficulty_for_russians"] = "Сложность", ["arpabet"] = "ARPAbet",
    };
    private static readonly HashSet<string> Skip = new() { "id", "slug", "sort_order" };

    /// <summary>Keys shown by dedicated sections of the page (everything else goes to <see cref="ExtraDetails"/>).</summary>
    private static readonly HashSet<string> Structured = new()
    {
        "id", "slug", "sort_order", "name_ru", "ipa", "arpabet", "difficulty_for_russians", "difficulty", "typical_errors",
        "articulation_ru", "us_uk_note_ru", "example_words", "minimal_pairs", "phrases", "tongue_twisters",
    };

    public ObservableCollection<SoundCard> Sounds { get; } = new();
    public ObservableCollection<DetailLine> Details { get; } = new();
    [ObservableProperty] private SoundCard? selected;
    [ObservableProperty] private string header = "Выберите звук слева";

    // ---------- presentation-only state ----------
    public ObservableCollection<SoundStep> Articulation { get; } = new();
    public ObservableCollection<SoundError> Errors { get; } = new();
    public ObservableCollection<SoundPair> MinimalPairs { get; } = new();
    public ObservableCollection<SoundExample> Examples { get; } = new();
    public ObservableCollection<SoundLine> Phrases { get; } = new();
    public ObservableCollection<SoundLine> TongueTwisters { get; } = new();
    public ObservableCollection<DetailLine> ExtraDetails { get; } = new();

    [ObservableProperty] private bool hasSelected;
    [ObservableProperty] private string selectedIpa = "";
    [ObservableProperty] private string selectedName = "";
    [ObservableProperty] private string selectedDifficulty = "";
    [ObservableProperty] private string usUkNote = "";
    [ObservableProperty] private bool hasUsUkNote;
    [ObservableProperty] private bool hasArticulation;
    [ObservableProperty] private bool hasErrors;
    [ObservableProperty] private bool hasMinimalPairs;
    [ObservableProperty] private bool hasExamples;
    [ObservableProperty] private bool hasPhrases;
    [ObservableProperty] private bool hasTongueTwisters;
    [ObservableProperty] private bool hasExtraDetails;
    [ObservableProperty] private string examplesSpeakText = "";

    partial void OnSelectedChanged(SoundCard? value) => ShowDetails(value);

    public void Load()
    {
        Sounds.Clear();
        foreach (var s in AppServices.Repo.GetSounds()) Sounds.Add(s);
    }

    /// <summary>Speaks a word / phrase with the configured accent (minimal pairs, examples, twisters).</summary>
    public async Task SpeakAsync(string text)
    {
        if (string.IsNullOrWhiteSpace(text)) return;
        try { await Speech.SpeakAsync(text, 0.9); } catch (Exception) { /* no TTS available */ }
    }

    private void ShowDetails(SoundCard? card)
    {
        Details.Clear();
        ClearStructured();
        HasSelected = card != null;
        if (card == null) return;
        Header = card.Title + (string.IsNullOrEmpty(card.Difficulty) ? "" : $" · сложность: {card.Difficulty}");
        SelectedIpa = ShortIpa(card.Ipa);
        SelectedName = card.NameRu;
        SelectedDifficulty = DifficultyText(card.Json);
        try
        {
            using var doc = JsonDocument.Parse(card.Json);
            if (doc.RootElement.ValueKind != JsonValueKind.Object)
            {
                Details.Add(new("", card.Json)); ExtraDetails.Add(new("", card.Json)); UpdateFlags(); return;
            }
            foreach (var p in doc.RootElement.EnumerateObject())
            {
                if (Skip.Contains(p.Name) || p.Name is "name_ru" or "ipa") continue;
                var text = Flatten(p.Value, 0);
                if (string.IsNullOrWhiteSpace(text)) continue;
                var line = new DetailLine(Titles.TryGetValue(p.Name, out var t) ? t : Humanize(p.Name), text);
                Details.Add(line);
                if (!Structured.Contains(p.Name)) ExtraDetails.Add(line);
            }
            FillStructured(doc.RootElement);
        }
        catch (JsonException) { Details.Add(new("", card.Json)); ExtraDetails.Add(new("", card.Json)); }
        UpdateFlags();
    }

    private void ClearStructured()
    {
        Articulation.Clear(); Errors.Clear(); MinimalPairs.Clear(); Examples.Clear();
        Phrases.Clear(); TongueTwisters.Clear(); ExtraDetails.Clear();
        UsUkNote = ""; ExamplesSpeakText = "";
        UpdateFlags();
    }

    private void UpdateFlags()
    {
        HasUsUkNote = !string.IsNullOrWhiteSpace(UsUkNote);
        HasArticulation = Articulation.Count > 0;
        HasErrors = Errors.Count > 0;
        HasMinimalPairs = MinimalPairs.Count > 0;
        HasExamples = Examples.Count > 0;
        HasPhrases = Phrases.Count > 0;
        HasTongueTwisters = TongueTwisters.Count > 0;
        HasExtraDetails = ExtraDetails.Count > 0;
    }

    private void FillStructured(JsonElement root)
    {
        int n = 1;
        foreach (var s in Strings(root, "articulation_ru")) Articulation.Add(new SoundStep(n++, s));
        foreach (var e in Objects(root, "typical_errors"))
            Errors.Add(new SoundError(Str(e, "substitute"), Str(e, "heard_as_example"), Str(e, "tip_ru")));
        foreach (var e in Objects(root, "minimal_pairs"))
        {
            var target = Str(e, "target"); var contrast = Str(e, "contrast");
            if (target.Length > 0 && contrast.Length > 0) MinimalPairs.Add(new SoundPair(target, contrast, Str(e, "contrast_sound")));
        }
        foreach (var e in Objects(root, "example_words"))
        {
            var word = Str(e, "word");
            if (word.Length == 0) continue;
            var ipa = Str(e, AppServices.Settings.Accent?.Contains("uk", StringComparison.OrdinalIgnoreCase) == true ? "ipa_uk" : "ipa_us");
            if (ipa.Length == 0) ipa = Str(e, "ipa_us");
            Examples.Add(new SoundExample(word, ipa, Str(e, "translation_ru")));
        }
        foreach (var s in Strings(root, "phrases")) Phrases.Add(new SoundLine(s));
        foreach (var s in Strings(root, "tongue_twisters")) TongueTwisters.Add(new SoundLine(s));
        UsUkNote = root.TryGetProperty("us_uk_note_ru", out var note) && note.ValueKind == JsonValueKind.String ? note.GetString() ?? "" : "";
        ExamplesSpeakText = string.Join(". ", Examples.Take(6).Select(x => x.Word));
    }

    private static IEnumerable<string> Strings(JsonElement root, string key) =>
        root.TryGetProperty(key, out var a) && a.ValueKind == JsonValueKind.Array
            ? a.EnumerateArray().Where(x => x.ValueKind == JsonValueKind.String).Select(x => x.GetString() ?? "").Where(s => s.Length > 0).ToList()
            : Enumerable.Empty<string>();

    private static IEnumerable<JsonElement> Objects(JsonElement root, string key) =>
        root.TryGetProperty(key, out var a) && a.ValueKind == JsonValueKind.Array
            ? a.EnumerateArray().Where(x => x.ValueKind == JsonValueKind.Object).ToList()
            : Enumerable.Empty<JsonElement>();

    private static string Str(JsonElement e, string key) =>
        e.TryGetProperty(key, out var v) ? v.ValueKind == JsonValueKind.String ? v.GetString() ?? "" : v.ValueKind == JsonValueKind.Number ? v.ToString() : "" : "";

    // ---------- static helpers for x:Bind in the sound grid ----------

    /// <summary>IPA shortened to fit the round badge: parenthetical notes removed, long lists cut to the first symbol.</summary>
    public static string ShortIpa(string ipa)
    {
        if (string.IsNullOrWhiteSpace(ipa)) return "•";
        var s = ipa;
        var paren = s.IndexOf('(');
        if (paren > 0) s = s[..paren];
        s = s.Trim();
        if (s.Length > 5) s = s.Split(' ', StringSplitOptions.RemoveEmptyEntries)[0] + "…";
        return s.Length == 0 ? "•" : s;
    }

    /// <summary>First articulation hint, trimmed, as a short card description.</summary>
    public static string Brief(string json)
    {
        try
        {
            using var doc = JsonDocument.Parse(json);
            if (doc.RootElement.ValueKind == JsonValueKind.Object)
            {
                var s = Strings(doc.RootElement, "articulation_ru").FirstOrDefault() ?? "";
                return s.Length > 90 ? s[..87].TrimEnd() + "…" : s;
            }
        }
        catch (JsonException) { }
        return "";
    }

    /// <summary>"Легко" / "Средне" / "Трудно" from difficulty_for_russians (1–3).</summary>
    public static string DifficultyText(string json)
    {
        try
        {
            using var doc = JsonDocument.Parse(json);
            if (doc.RootElement.ValueKind == JsonValueKind.Object &&
                doc.RootElement.TryGetProperty("difficulty_for_russians", out var d) && d.TryGetInt32(out var v))
                return v switch { <= 1 => "●○○  легко", 2 => "●●○  средне", _ => "●●●  трудно" };
        }
        catch (Exception) { }
        return "";
    }

    private static string Humanize(string key) { var s = key.Replace('_', ' '); return s.Length == 0 ? s : char.ToUpper(s[0]) + s[1..]; }

    private static string Flatten(JsonElement e, int depth)
    {
        switch (e.ValueKind)
        {
            case JsonValueKind.String: return e.GetString() ?? "";
            case JsonValueKind.Number: case JsonValueKind.True: case JsonValueKind.False: return e.ToString();
            case JsonValueKind.Array:
                return string.Join("\n", e.EnumerateArray().Select(x => Flatten(x, depth + 1)).Where(s => s.Length > 0)
                    .Select(s => depth == 0 ? "• " + s : s));
            case JsonValueKind.Object:
                return string.Join(" — ", e.EnumerateObject().Select(p => Flatten(p.Value, depth + 1)).Where(s => s.Length > 0));
            default: return "";
        }
    }
}
