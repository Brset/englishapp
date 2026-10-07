using System.Collections.ObjectModel;
using CommunityToolkit.Mvvm.ComponentModel;
using EnglishApp.Native;
using Microsoft.UI;
using Microsoft.UI.Xaml.Media;

namespace EnglishApp.ViewModels;

public sealed record PhonemeRow(string Ipa, string ScoreText, Brush Color, string Detail, string Tip);

public partial class ReviewViewModel : ObservableObject
{
    public ReviewArgs? Args { get; private set; }
    public AssessmentResult? Result => Args?.Result;

    [ObservableProperty] private string accuracy = "–";
    [ObservableProperty] private string completeness = "–";
    [ObservableProperty] private string fluency = "–";
    [ObservableProperty] private string overall = "–";
    [ObservableProperty] private string fluencyDetail = "";
    [ObservableProperty] private string notes = "";
    [ObservableProperty] private string wordTitle = "Нажмите на слово, чтобы увидеть звуки";
    [ObservableProperty] private string wordSubtitle = "";
    public ObservableCollection<PhonemeRow> Phonemes { get; } = new();
    public ObservableCollection<AdviceItem> Advice { get; } = new();

    public void Load(ReviewArgs? args)
    {
        Args = args;
        Phonemes.Clear(); Advice.Clear();
        if (args == null) return;
        var r = args.Result;
        Accuracy = $"{r.Scores.Accuracy:0}"; Completeness = $"{r.Scores.Completeness:0}";
        Fluency = $"{r.Scores.Fluency:0}"; Overall = $"{r.Scores.Overall:0}";
        var f = r.Fluency;
        FluencyDetail = $"{f.WordsPerMinute:0} сл/мин · длинных пауз: {f.LongPauses.Count} · повторов: {f.Repetitions} · запинок: {f.Hesitations}";
        var notes = new List<string>();
        if (args.Note != null) notes.Add(args.Note);
        if (!r.PhonemeLevel) notes.Add("Оценка по звукам недоступна (нет фонемной модели): показан разбор на уровне слов.");
        notes.AddRange(r.Warnings);
        Notes = string.Join("\n", notes);
        foreach (var a in r.Advice.OrderByDescending(a => a.Count)) Advice.Add(a);
    }

    public void SelectWord(int index)
    {
        Phonemes.Clear();
        var r = Result;
        if (r == null || index < 0 || index >= r.Words.Count) return;
        var w = r.Words[index];
        WordTitle = $"{w.Text}  {(string.IsNullOrEmpty(w.ExpectedIpa) ? "" : "/" + w.ExpectedIpa + "/")}";
        WordSubtitle = $"Балл: {w.Score:0} · {StatusRu(w.Status)}" +
                       (string.IsNullOrEmpty(w.Recognized) ? "" : $" · услышано: {w.Recognized}");
        foreach (var p in w.Phonemes)
        {
            var tip = p.AdviceId == null ? "" : r.Advice.FirstOrDefault(a => a.Id == p.AdviceId)?.TipRu ?? "";
            var detail = p.Substituted && !string.IsNullOrEmpty(p.ActualIpa) ? $"произнесено как /{p.ActualIpa}/" : "";
            Phonemes.Add(new PhonemeRow("/" + p.Ipa + "/", p.Score is double s ? $"{s:0}" : "–",
                new SolidColorBrush(ColorFor(p.Score)), detail, tip));
        }
    }

    private static string StatusRu(string s) => s switch
    {
        "matched" => "верно", "substituted" => "заменено", "omitted" or "missing" => "пропущено", _ => s,
    };

    public static Windows.UI.Color ColorFor(double? score) => score switch
    {
        null => Colors.Gray,
        >= 80 => ParseHex("#2E9E5B"),
        >= 60 => ParseHex("#E0A100"),
        _ => ParseHex("#D64545"),
    };

    /// <summary>Parses #RRGGBB / #AARRGGBB; falls back to gray.</summary>
    public static Windows.UI.Color ParseHex(string? hex)
    {
        if (string.IsNullOrWhiteSpace(hex)) return Colors.Gray;
        var h = hex.TrimStart('#');
        try
        {
            if (h.Length == 6)
                return Windows.UI.Color.FromArgb(255, Convert.ToByte(h[..2], 16), Convert.ToByte(h[2..4], 16), Convert.ToByte(h[4..6], 16));
            if (h.Length == 8)
                return Windows.UI.Color.FromArgb(Convert.ToByte(h[..2], 16), Convert.ToByte(h[2..4], 16), Convert.ToByte(h[4..6], 16), Convert.ToByte(h[6..8], 16));
        }
        catch (FormatException) { }
        return Colors.Gray;
    }
}
