using System.Text.RegularExpressions;
using EnglishApp.Models;
using EnglishApp.Native;
using EnglishApp.Services;
using EnglishApp.ViewModels;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Documents;
using Microsoft.UI.Xaml.Input;
using Microsoft.UI.Xaml.Media;
using Microsoft.UI.Xaml.Navigation;

namespace EnglishApp.Views;

public sealed partial class ReadingPage : Page
{
    private static readonly Regex WordRx = new(@"[A-Za-z]+(?:['’][A-Za-z]+)*", RegexOptions.Compiled);

    public ReadingViewModel ViewModel { get; } = new();
    public ReadingPage()
    {
        InitializeComponent();
        Loaded += (_, _) => ShowCelebrationIfAny();
    }

    private readonly Dictionary<Run, WordResult> _resultRuns = new();

    private static readonly SolidColorBrush ReadBrush = new(Windows.UI.Color.FromArgb(255, 0x2E, 0x9E, 0x5B));
    private static readonly SolidColorBrush SkippedBrush = new(Windows.UI.Color.FromArgb(255, 0xE0, 0x80, 0x1A));

    protected override void OnNavigatedTo(NavigationEventArgs e)
    {
        AppServices.Jobs.JobFinished += OnJobFinished;
        if (!ViewModel.Load(e.Parameter as string ?? App.MainWindow.CurrentTextId)) return;
        RebuildBody();
        DispatcherQueue.TryEnqueue(ShowCelebrationIfAny);
    }

    private void OnShowLibrary(object sender, RoutedEventArgs e) => App.MainWindow.NavigateTo("library");

    protected override void OnNavigatedFrom(NavigationEventArgs e)
    {
        AppServices.Jobs.JobFinished -= OnJobFinished;
        AppServices.Player.Stop();
    }

    private void OnJobFinished(string textId, bool success)
    {
        if (ViewModel.Text?.Id != textId) return;
        ViewModel.LoadReadState();
        RebuildBody();
    }

    /// <summary>Result colouring when the assessment is ready, quick live marks while it is pending, plain text otherwise.</summary>
    private void RebuildBody()
    {
        _resultRuns.Clear();
        if (ViewModel.Text == null) return;
        var body = ViewModel.Text.Body;
        var spans = new List<(int, int, Brush?, bool, WordResult?)>();
        if (ViewModel.Result is { } r)
        {
            foreach (var w in r.Words)
            {
                var color = string.IsNullOrEmpty(w.Color) ? ReviewViewModel.ColorFor(w.Score) : ReviewViewModel.ParseHex(w.Color);
                spans.Add((w.U16Begin, w.U16End, new SolidColorBrush(color), IsOmitted(w), w));
            }
        }
        else if (ViewModel.LiveMarks is { } marks)
        {
            IReadOnlyList<TokenInfo> tokens;
            try { tokens = PronAssessor.Tokenize(body); } catch (Exception) { tokens = Array.Empty<TokenInfo>(); }
            for (int i = 0; i < tokens.Count && i < marks.Length; i++)
            {
                var t = tokens[i];
                if (marks[i] == 'r') spans.Add((t.U16Begin, t.U16End, ReadBrush, false, null));
                else if (marks[i] == 's') spans.Add((t.U16Begin, t.U16End, SkippedBrush, true, null));
            }
        }
        foreach (var m in ViewModel.ExtraMarks)
            spans.Add((m.Begin, m.End, m.Mark == 'r' ? ReadBrush : SkippedBrush, m.Mark == 's', null));
        if (spans.Count > 0) BuildSpans(body, spans);
        else BuildText(body);
    }

    // ---------------- celebration ----------------

    private bool _celebrating;

    private async void ShowCelebrationIfAny()
    {
        if (_celebrating || XamlRoot == null || App.MainWindow.PendingCelebration is not { } info) return;
        if (ViewModel.Text?.Id != info.TextId) return;
        App.MainWindow.PendingCelebration = null;
        _celebrating = true;
        try { await Celebration.ShowAsync(XamlRoot, info); }
        catch (Exception ex) { Diagnostics.LogException("celebration", ex); }
        finally { _celebrating = false; }
    }

    private static bool IsOmitted(WordResult w) => w.Status is "omitted" or "missing" || string.IsNullOrEmpty(w.Recognized);

    private void BuildSpans(string text, List<(int Begin, int End, Brush? Brush, bool Underline, WordResult? Word)> spans)
    {
        Body.Blocks.Clear();
        var p = new Paragraph();
        int pos = 0;
        void AddGap(string gap)
        {
            var lines = gap.Replace("\r", "").Split('\n');
            for (int i = 0; i < lines.Length; i++)
            {
                if (i > 0) p.Inlines.Add(new LineBreak());
                if (lines[i].Length > 0) p.Inlines.Add(new Run { Text = lines[i] });
            }
        }
        foreach (var s in spans.OrderBy(x => x.Begin))
        {
            if (s.Begin < pos || s.End <= s.Begin || s.End > text.Length) continue;
            if (s.Begin > pos) AddGap(text[pos..s.Begin]);
            var run = new Run { Text = text[s.Begin..s.End] };
            if (s.Brush != null) run.Foreground = s.Brush;
            if (s.Word != null) { run.FontWeight = Microsoft.UI.Text.FontWeights.SemiBold; _resultRuns[run] = s.Word; }
            // s.Underline is shown by colour only: TextDecorations is ambiguous (WinUI vs Windows SDK projection).
            p.Inlines.Add(run);
            pos = s.End;
        }
        if (pos < text.Length) AddGap(text[pos..]);
        Body.Blocks.Add(p);
    }

    private void BuildText(string body)
    {
        Body.Blocks.Clear();
        foreach (var para in body.Split("\n\n", StringSplitOptions.RemoveEmptyEntries))
        {
            var p = new Paragraph { Margin = new Thickness(0, 0, 0, 12) };
            int pos = 0;
            foreach (Match m in WordRx.Matches(para))
            {
                if (m.Index > pos) p.Inlines.Add(new Run { Text = para[pos..m.Index] });
                p.Inlines.Add(new Run { Text = m.Value });
                pos = m.Index + m.Length;
            }
            if (pos < para.Length) p.Inlines.Add(new Run { Text = para[pos..] });
            Body.Blocks.Add(p);
        }
    }

    private void OnBodyTapped(object sender, TappedRoutedEventArgs e)
    {
        var point = e.GetPosition(Body);
        var ptr = Body.GetPositionFromPoint(point);
        if (ptr?.Parent is not Run run) return;
        if (_resultRuns.TryGetValue(run, out var wr)) { ShowResultFlyout(wr, point); return; }
        var word = run.Text.Trim();
        if (!WordRx.IsMatch(word)) return;
        word = word.Replace('’', '\'');
        ShowPopup(word, point);
    }

    private void ShowPopup(string word, Windows.Foundation.Point point)
    {
        var vocab = ViewModel.FindVocab(word);
        var ipa = ViewModel.IpaFor(word);

        var panel = new StackPanel { Spacing = 6, MinWidth = 240 };
        panel.Children.Add(new TextBlock { Text = word, FontSize = 22, FontWeight = Microsoft.UI.Text.FontWeights.SemiBold });
        panel.Children.Add(new TextBlock { Text = string.IsNullOrEmpty(ipa) ? "транскрипция недоступна" : $"/{ipa}/ ({ViewModel.Accent})" });
        panel.Children.Add(new TextBlock { Text = vocab?.TranslationRu is { Length: > 0 } t ? t : "перевода в словаре текста нет", TextWrapping = TextWrapping.Wrap });

        var row = new StackPanel { Orientation = Orientation.Horizontal, Spacing = 8 };
        var speak = new Button { Content = "Произнести" };
        speak.Click += async (_, _) => await ViewModel.SpeakWordAsync(word);
        var save = new Button { Content = AppServices.Repo.IsWordSaved(word) ? "В словаре" : "Сохранить", IsEnabled = !AppServices.Repo.IsWordSaved(word) };
        save.Click += (_, _) => { ViewModel.SaveWord(word); save.Content = "В словаре"; save.IsEnabled = false; };
        row.Children.Add(speak); row.Children.Add(save);
        panel.Children.Add(row);

        new Flyout { Content = panel }.ShowAt(Body, new Microsoft.UI.Xaml.Controls.Primitives.FlyoutShowOptions { Position = point });
    }

    private static Brush ThemeBrush(string key, Brush fallback) =>
        Application.Current.Resources.TryGetValue(key, out var o) && o is Brush b ? b : fallback;

    private void ShowResultFlyout(WordResult w, Windows.Foundation.Point point)
    {
        var secondary = ThemeBrush("TextFillColorSecondaryBrush", new SolidColorBrush(Microsoft.UI.Colors.Gray));
        var panel = new StackPanel { Spacing = 6, MinWidth = 280, MaxWidth = 420 };
        panel.Children.Add(new TextBlock
        {
            Text = w.Text + (string.IsNullOrEmpty(w.ExpectedIpa) ? "" : $"  /{w.ExpectedIpa}/"),
            FontSize = 22, FontWeight = Microsoft.UI.Text.FontWeights.SemiBold,
        });
        panel.Children.Add(new TextBlock
        {
            Text = IsOmitted(w) ? "пропущено" : $"Вы сказали: {w.Recognized}",
            FontSize = 16, TextWrapping = TextWrapping.Wrap,
        });
        panel.Children.Add(new TextBlock { Text = $"Балл: {w.Score:0}/100", Foreground = secondary });

        if (w.Phonemes.Count > 0)
        {
            panel.Children.Add(new TextBlock { Text = "Звуки: эталон → вы сказали", Foreground = secondary, Margin = new Thickness(0, 6, 0, 0) });
            foreach (var ph in w.Phonemes)
            {
                string heard = ph.Substituted && !string.IsNullOrEmpty(ph.ActualIpa) ? ph.ActualIpa
                    : ph.Score is null ? "?" : ph.Ipa;
                var row = new StackPanel { Orientation = Orientation.Horizontal, Spacing = 10 };
                row.Children.Add(new TextBlock
                {
                    Text = $"/{ph.Ipa}/ → /{heard}/", MinWidth = 120,
                    Foreground = new SolidColorBrush(ReviewViewModel.ColorFor(ph.Score)),
                    FontWeight = Microsoft.UI.Text.FontWeights.SemiBold,
                });
                row.Children.Add(new TextBlock { Text = ph.Score is double sc ? $"{sc:0}" : "–" });
                panel.Children.Add(row);
            }
            var tips = w.Phonemes.Select(ph => ViewModel.AdviceTip(ph.AdviceId)).Where(t => t.Length > 0).Distinct().ToList();
            foreach (var tip in tips)
                panel.Children.Add(new TextBlock { Text = "Совет: " + tip, TextWrapping = TextWrapping.Wrap, Margin = new Thickness(0, 4, 0, 0) });
        }

        var status = new TextBlock { Foreground = ThemeBrush("SystemFillColorCautionBrush", new SolidColorBrush(Microsoft.UI.Colors.DarkOrange)), TextWrapping = TextWrapping.Wrap };
        var buttons = new StackPanel { Orientation = Orientation.Horizontal, Spacing = 8, Margin = new Thickness(0, 6, 0, 0) };
        var reference = new Button { Content = "Эталон" };
        reference.Click += async (_, _) =>
        {
            status.Text = "";
            await ViewModel.SpeakWordAsync(w.Text);
            status.Text = ViewModel.Status;
        };
        var mine = new Button { Content = "Моя запись", IsEnabled = w.Start != null && w.End != null && ViewModel.WavForWord(w) != null };
        mine.Click += (_, _) => status.Text = ViewModel.PlayMyWord(w) ? "" : "Запись этого слова недоступна.";
        var save = new Button { Content = AppServices.Repo.IsWordSaved(w.Text) ? "В словаре" : "В словарь", IsEnabled = !AppServices.Repo.IsWordSaved(w.Text) };
        save.Click += (_, _) => { ViewModel.SaveWord(w.Text); save.Content = "В словаре"; save.IsEnabled = false; };
        buttons.Children.Add(reference); buttons.Children.Add(mine); buttons.Children.Add(save);
        panel.Children.Add(buttons);
        panel.Children.Add(status);

        new Flyout { Content = panel }.ShowAt(Body, new Microsoft.UI.Xaml.Controls.Primitives.FlyoutShowOptions { Position = point });
    }

    private void OnReview(object sender, RoutedEventArgs e)
    {
        if (ViewModel.ReviewArgs is { } args) App.MainWindow.NavigateTo("review", args);
    }

    private void OnRecord(object sender, RoutedEventArgs e)
    {
        if (ViewModel.Text != null) App.MainWindow.NavigateTo("record", new RecordArgs(ViewModel.Text.Id));
    }

    private void OnResume(object sender, RoutedEventArgs e)
    {
        if (ViewModel.Text != null && ViewModel.Position is { } pos)
            App.MainWindow.NavigateTo("record", new RecordArgs(ViewModel.Text.Id, pos.ParagraphIndex, true));
    }

    private void OnParagraphs(object sender, RoutedEventArgs e)
    {
        if (ViewModel.Text != null) App.MainWindow.NavigateTo("record", new RecordArgs(ViewModel.Text.Id, 0, true));
    }

    private void OnWhole(object sender, RoutedEventArgs e)
    {
        if (ViewModel.Text != null) App.MainWindow.NavigateTo("record", new RecordArgs(ViewModel.Text.Id, null, false));
    }

    private void OnShadowing(object sender, RoutedEventArgs e)
    {
        if (ViewModel.Text != null) App.MainWindow.NavigateTo("shadowing", ViewModel.Text.Id);
    }
}
