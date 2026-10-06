using EnglishApp.ViewModels;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Documents;
using Microsoft.UI.Xaml.Input;
using Microsoft.UI.Xaml.Media;
using Microsoft.UI.Xaml.Navigation;

namespace EnglishApp.Views;

public sealed partial class ReviewPage : Page
{
    private readonly Dictionary<Run, int> _runs = new();
    public ReviewViewModel ViewModel { get; } = new();

    public ReviewPage()
    {
        InitializeComponent();
        Loaded += (_, _) => SyncTheme(ActualTheme == ElementTheme.Dark);
        ActualThemeChanged += (_, _) => SyncTheme(ActualTheme == ElementTheme.Dark);
    }

    protected override void OnNavigatedTo(NavigationEventArgs e)
    {
        var args = e.Parameter as ReviewArgs ?? App.MainWindow.LastReview;
        ViewModel.SetTheme(IsAppDark());
        ViewModel.Load(args);
        Empty.Visibility = args == null ? Visibility.Visible : Visibility.Collapsed;
        BuildWords();
    }

    /// <summary>Before the page is in the visual tree its own ActualTheme is not resolved; ask the window root.</summary>
    private static bool IsAppDark()
    {
        try
        {
            if (App.MainWindow?.Content is FrameworkElement fe) return fe.ActualTheme == ElementTheme.Dark;
            return Application.Current.RequestedTheme == ApplicationTheme.Dark;
        }
        catch (Exception) { return false; }
    }

    private void SyncTheme(bool dark)
    {
        if (dark == ViewModel.IsDark) return;
        ViewModel.SetTheme(dark);
        BuildWords();
    }

    private void BuildWords()
    {
        Words.Blocks.Clear();
        _runs.Clear();
        var args = ViewModel.Args;
        if (args == null) return;
        var text = args.Reference;
        var p = new Paragraph();
        int pos = 0;
        foreach (var w in args.Result.Words.OrderBy(w => w.U16Begin))
        {
            if (w.U16Begin < pos || w.U16End > text.Length || w.U16End <= w.U16Begin) continue;
            if (w.U16Begin > pos) p.Inlines.Add(new Run { Text = text[pos..w.U16Begin] });
            // Engine band mapped onto the shared (theme-aware) palette; fall back to the engine colour, then the score.
            var color = ReviewViewModel.BandColor(w.Band, ViewModel.IsDark)
                        ?? (string.IsNullOrEmpty(w.Color) ? ReviewViewModel.ColorFor(w.Score, ViewModel.IsDark) : ReviewViewModel.ParseHex(w.Color));
            var run = new Run { Text = text[w.U16Begin..w.U16End], Foreground = new SolidColorBrush(color), FontWeight = Microsoft.UI.Text.FontWeights.SemiBold };
            _runs[run] = w.Index;
            p.Inlines.Add(run);
            pos = w.U16End;
        }
        if (pos < text.Length) p.Inlines.Add(new Run { Text = text[pos..] });
        Words.Blocks.Add(p);
    }

    private void OnWordsTapped(object sender, TappedRoutedEventArgs e)
    {
        var ptr = Words.GetPositionFromPoint(e.GetPosition(Words));
        if (ptr?.Parent is Run run && _runs.TryGetValue(run, out var idx))
        {
            var pos = ViewModel.Result!.Words.FindIndex(w => w.Index == idx);
            ViewModel.SelectWord(pos);
        }
    }

    private void OnOpenLibrary(object sender, RoutedEventArgs e) => App.MainWindow.NavigateTo("library");

    // ---------- x:Bind helpers ----------
    public static Visibility VisibleWhenFalse(bool value) => value ? Visibility.Collapsed : Visibility.Visible;

    public static string AdviceIpa(string expected, string actual)
    {
        if (string.IsNullOrEmpty(expected)) return "";
        return string.IsNullOrEmpty(actual) ? $"/{expected}/" : $"/{expected}/ → /{actual}/";
    }

    public static Visibility AdviceIpaVisibility(string expected, string actual) =>
        string.IsNullOrEmpty(AdviceIpa(expected, actual)) ? Visibility.Collapsed : Visibility.Visible;

    public static string TimesText(int count) => count switch
    {
        <= 0 => "в тексте",
        _ when count % 10 == 1 && count % 100 != 11 => $"{count} раз в тексте",
        _ when count % 10 is >= 2 and <= 4 && count % 100 is < 12 or > 14 => $"{count} раза в тексте",
        _ => $"{count} раз в тексте",
    };
}
