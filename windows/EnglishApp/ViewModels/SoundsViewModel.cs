using System.Collections.ObjectModel;
using System.Text.Json;
using CommunityToolkit.Mvvm.ComponentModel;
using EnglishApp.Models;
using EnglishApp.Services;

namespace EnglishApp.ViewModels;

public sealed record DetailLine(string Heading, string Text);

public partial class SoundsViewModel : ObservableObject
{
    private static readonly Dictionary<string, string> Titles = new()
    {
        ["articulation"] = "Артикуляция", ["minimal_pairs"] = "Минимальные пары", ["examples"] = "Примеры",
        ["tips"] = "Советы", ["common_errors"] = "Типичные ошибки", ["description"] = "Описание",
        ["difficulty"] = "Сложность", ["name_ru"] = "Название", ["ipa"] = "Транскрипция", ["words"] = "Слова",
    };
    private static readonly HashSet<string> Skip = new() { "id", "slug", "sort_order" };

    public ObservableCollection<SoundCard> Sounds { get; } = new();
    public ObservableCollection<DetailLine> Details { get; } = new();
    [ObservableProperty] private SoundCard? selected;
    [ObservableProperty] private string header = "Выберите звук слева";

    partial void OnSelectedChanged(SoundCard? value) => ShowDetails(value);

    public void Load()
    {
        Sounds.Clear();
        foreach (var s in AppServices.Repo.GetSounds()) Sounds.Add(s);
    }

    private void ShowDetails(SoundCard? card)
    {
        Details.Clear();
        if (card == null) return;
        Header = card.Title + (string.IsNullOrEmpty(card.Difficulty) ? "" : $" · сложность: {card.Difficulty}");
        try
        {
            using var doc = JsonDocument.Parse(card.Json);
            if (doc.RootElement.ValueKind != JsonValueKind.Object) { Details.Add(new("", card.Json)); return; }
            foreach (var p in doc.RootElement.EnumerateObject())
            {
                if (Skip.Contains(p.Name) || p.Name is "name_ru" or "ipa") continue;
                var text = Flatten(p.Value, 0);
                if (string.IsNullOrWhiteSpace(text)) continue;
                Details.Add(new DetailLine(Titles.TryGetValue(p.Name, out var t) ? t : Humanize(p.Name), text));
            }
        }
        catch (JsonException) { Details.Add(new("", card.Json)); }
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
