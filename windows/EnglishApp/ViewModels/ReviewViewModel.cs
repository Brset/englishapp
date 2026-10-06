using System.Collections.ObjectModel;
using CommunityToolkit.Mvvm.ComponentModel;
using EnglishApp.Native;
using Microsoft.UI;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Media;

namespace EnglishApp.ViewModels;

public sealed record PhonemeRow(string Ipa, string ScoreText, Brush Color, string Detail, string Tip)
{
    /// <summary>Translucent tint of <see cref="Color"/> used as the score pill background.</summary>
    public Brush? Tint { get; init; }
    public Visibility DetailVisibility => string.IsNullOrEmpty(Detail) ? Visibility.Collapsed : Visibility.Visible;
    public Visibility TipVisibility => string.IsNullOrEmpty(Tip) ? Visibility.Collapsed : Visibility.Visible;
}

public partial class ReviewViewModel : ObservableObject
{
    private int _selectedWord = -1;

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

    // ---------- presentation-only state ----------
    /// <summary>Set by the page from its ActualTheme; selects the dark score palette.</summary>
    public bool IsDark { get; private set; }

    [ObservableProperty] private bool hasResult;
    [ObservableProperty] private bool hasNotes;
    [ObservableProperty] private bool hasAdvice;
    [ObservableProperty] private bool hasPhonemes;
    [ObservableProperty] private bool hasWordSelection;
    [ObservableProperty] private double overallValue;
    [ObservableProperty] private double accuracyValue;
    [ObservableProperty] private double completenessValue;
    [ObservableProperty] private double fluencyValue;
    [ObservableProperty] private string wpm = "–";
    [ObservableProperty] private string pausesText = "";
    [ObservableProperty] private string repetitionsText = "";
    [ObservableProperty] private string hesitationsText = "";
    [ObservableProperty] private string overallLabel = "";
    [ObservableProperty] private string overallHint = "";
    [ObservableProperty] private Brush overallBrush = new SolidColorBrush(Colors.Gray);
    [ObservableProperty] private Brush accuracyBrush = new SolidColorBrush(Colors.Gray);
    [ObservableProperty] private Brush completenessBrush = new SolidColorBrush(Colors.Gray);
    [ObservableProperty] private Brush fluencyBrush = new SolidColorBrush(Colors.Gray);
    [ObservableProperty] private Brush goodBrush = new SolidColorBrush(ParseHex(GoodLight));
    [ObservableProperty] private Brush fairBrush = new SolidColorBrush(ParseHex(FairLight));
    [ObservableProperty] private Brush poorBrush = new SolidColorBrush(ParseHex(PoorLight));
    [ObservableProperty] private Brush missingBrush = new SolidColorBrush(ParseHex(MissingLight));

    public void Load(ReviewArgs? args)
    {
        Args = args;
        _selectedWord = -1;
        Phonemes.Clear(); Advice.Clear();
        HasPhonemes = false; HasWordSelection = false;
        WordTitle = "Нажмите на слово, чтобы увидеть звуки";
        WordSubtitle = "";
        HasResult = args != null;
        if (args == null) { HasAdvice = false; HasNotes = false; return; }
        var r = args.Result;
        Accuracy = $"{r.Scores.Accuracy:0}"; Completeness = $"{r.Scores.Completeness:0}";
        Fluency = $"{r.Scores.Fluency:0}"; Overall = $"{r.Scores.Overall:0}";
        OverallValue = Clamp(r.Scores.Overall); AccuracyValue = Clamp(r.Scores.Accuracy);
        CompletenessValue = Clamp(r.Scores.Completeness); FluencyValue = Clamp(r.Scores.Fluency);
        var f = r.Fluency;
        FluencyDetail = $"{f.WordsPerMinute:0} сл/мин · длинных пауз: {f.LongPauses.Count} · повторов: {f.Repetitions} · запинок: {f.Hesitations}";
        Wpm = $"{f.WordsPerMinute:0}";
        PausesText = $"Длинных пауз: {f.LongPauses.Count}";
        RepetitionsText = $"Повторов: {f.Repetitions}";
        HesitationsText = $"Запинок: {f.Hesitations}";
        (OverallLabel, OverallHint) = r.Scores.Overall switch
        {
            >= 80 => ("Отличный результат", "Произношение звучит уверенно. Попробуйте текст посложнее."),
            >= 60 => ("Хороший результат", "Есть несколько звуков, над которыми стоит поработать."),
            _ => ("Есть над чем поработать", "Посмотрите выделенные слова и советы ниже, затем запишитесь ещё раз."),
        };
        var notes = new List<string>();
        if (args.Note != null) notes.Add(args.Note);
        if (!r.PhonemeLevel) notes.Add("Оценка по звукам недоступна (нет фонемной модели): показан разбор на уровне слов.");
        notes.AddRange(r.Warnings);
        Notes = string.Join("\n", notes);
        HasNotes = notes.Count > 0;
        foreach (var a in r.Advice.OrderByDescending(a => a.Count)) Advice.Add(a);
        HasAdvice = Advice.Count > 0;
        ApplyBrushes();
    }

    /// <summary>Switches the score palette between light and dark variants and refreshes coloured state.</summary>
    public void SetTheme(bool dark)
    {
        IsDark = dark;
        ApplyBrushes();
        if (_selectedWord >= 0) SelectWord(_selectedWord);
    }

    private void ApplyBrushes()
    {
        GoodBrush = new SolidColorBrush(ParseHex(IsDark ? GoodDark : GoodLight));
        FairBrush = new SolidColorBrush(ParseHex(IsDark ? FairDark : FairLight));
        PoorBrush = new SolidColorBrush(ParseHex(IsDark ? PoorDark : PoorLight));
        MissingBrush = new SolidColorBrush(ParseHex(IsDark ? MissingDark : MissingLight));
        var s = Result?.Scores;
        OverallBrush = new SolidColorBrush(ColorFor(s?.Overall, IsDark));
        AccuracyBrush = new SolidColorBrush(ColorFor(s?.Accuracy, IsDark));
        CompletenessBrush = new SolidColorBrush(ColorFor(s?.Completeness, IsDark));
        FluencyBrush = new SolidColorBrush(ColorFor(s?.Fluency, IsDark));
    }

    private static double Clamp(double v) => double.IsFinite(v) ? Math.Clamp(v, 0, 100) : 0;

    public void SelectWord(int index)
    {
        Phonemes.Clear();
        var r = Result;
        if (r == null || index < 0 || index >= r.Words.Count) { HasPhonemes = false; return; }
        _selectedWord = index;
        HasWordSelection = true;
        var w = r.Words[index];
        WordTitle = $"{w.Text}  {(string.IsNullOrEmpty(w.ExpectedIpa) ? "" : "/" + w.ExpectedIpa + "/")}";
        WordSubtitle = $"Балл: {w.Score:0} · {StatusRu(w.Status)}" +
                       (string.IsNullOrEmpty(w.Recognized) ? "" : $" · услышано: {w.Recognized}");
        foreach (var p in w.Phonemes)
        {
            var tip = p.AdviceId == null ? "" : r.Advice.FirstOrDefault(a => a.Id == p.AdviceId)?.TipRu ?? "";
            var detail = p.Substituted && !string.IsNullOrEmpty(p.ActualIpa) ? $"произнесено как /{p.ActualIpa}/" : "";
            var c = ColorFor(p.Score, IsDark);
            Phonemes.Add(new PhonemeRow("/" + p.Ipa + "/", p.Score is double s ? $"{s:0}" : "–",
                new SolidColorBrush(c), detail, tip)
            {
                Tint = new SolidColorBrush(Windows.UI.Color.FromArgb(0x29, c.R, c.G, c.B)),
            });
        }
        HasPhonemes = Phonemes.Count > 0;
    }

    private static string StatusRu(string s) => s switch
    {
        "matched" => "верно", "substituted" => "заменено", "omitted" or "missing" => "пропущено", _ => s,
    };

    // Shared palette (Styles/Theme.xaml Score*Brush).
    private const string GoodLight = "#2E9E5B", FairLight = "#D99A00", PoorLight = "#D64545", MissingLight = "#9AA0AE";
    private const string GoodDark = "#5BD08A", FairDark = "#F2C14E", PoorDark = "#FF7A7A", MissingDark = "#7D8292";

    /// <summary>Score colour from the shared palette (light variant): ≥80 good, 60–79 fair, &lt;60 poor, null missing.</summary>
    public static Windows.UI.Color ColorFor(double? score) => ColorFor(score, false);

    public static Windows.UI.Color ColorFor(double? score, bool dark) => score switch
    {
        null => ParseHex(dark ? MissingDark : MissingLight),
        double d when double.IsNaN(d) => ParseHex(dark ? MissingDark : MissingLight),
        >= 80 => ParseHex(dark ? GoodDark : GoodLight),
        >= 60 => ParseHex(dark ? FairDark : FairLight),
        _ => ParseHex(dark ? PoorDark : PoorLight),
    };

    /// <summary>Colour for an engine score band ("good"/"fair"/"poor"/"omitted"); null for unknown bands.</summary>
    public static Windows.UI.Color? BandColor(string? band, bool dark) => band switch
    {
        "good" => ParseHex(dark ? GoodDark : GoodLight),
        "fair" => ParseHex(dark ? FairDark : FairLight),
        "poor" => ParseHex(dark ? PoorDark : PoorLight),
        "omitted" => ParseHex(dark ? MissingDark : MissingLight),
        _ => null,
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
