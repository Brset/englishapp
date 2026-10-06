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
    public ReviewPage() { InitializeComponent(); }

    protected override void OnNavigatedTo(NavigationEventArgs e)
    {
        var args = e.Parameter as ReviewArgs ?? App.MainWindow.LastReview;
        ViewModel.Load(args);
        Empty.Visibility = args == null ? Visibility.Visible : Visibility.Collapsed;
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
            var color = string.IsNullOrEmpty(w.Color) ? ReviewViewModel.ColorFor(w.Score) : ReviewViewModel.ParseHex(w.Color);
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
}
