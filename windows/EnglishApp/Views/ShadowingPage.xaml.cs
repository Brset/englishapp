using EnglishApp.Native;
using EnglishApp.ViewModels;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Documents;
using Microsoft.UI.Xaml.Navigation;

namespace EnglishApp.Views;

public sealed partial class ShadowingPage : Page
{
    public ShadowingViewModel ViewModel { get; } = new();
    public ShadowingPage() { InitializeComponent(); }

    protected override void OnNavigatedTo(NavigationEventArgs e)
    {
        ViewModel.Changed += Render;
        ViewModel.Load(e.Parameter as string ?? App.MainWindow.CurrentTextId);
    }

    protected override void OnNavigatedFrom(NavigationEventArgs e)
    {
        ViewModel.Changed -= Render;
        EnglishApp.Services.QuickSpeech.CancelCurrent();
        EnglishApp.Services.AppServices.Player.Stop();
    }

    /// <summary>Shows the current sentence; after a result every word is coloured by its score.</summary>
    private void Render()
    {
        SentenceBlock.Blocks.Clear();
        var text = ViewModel.CurrentSentence;
        var p = new Paragraph();
        var words = ViewModel.LastResult?.Result?.Words;
        if (words == null || words.Count == 0) p.Inlines.Add(new Run { Text = text });
        else
        {
            int pos = 0;
            foreach (var w in words.OrderBy(x => x.U16Begin))
            {
                if (w.U16Begin < pos || w.U16End <= w.U16Begin || w.U16End > text.Length) continue;
                if (w.U16Begin > pos) p.Inlines.Add(new Run { Text = text[pos..w.U16Begin] });
                p.Inlines.Add(new Run
                {
                    Text = text[w.U16Begin..w.U16End],
                    Foreground = new Microsoft.UI.Xaml.Media.SolidColorBrush(ReviewViewModel.ColorFor(w.Score)),
                });
                pos = w.U16End;
            }
            if (pos < text.Length) p.Inlines.Add(new Run { Text = text[pos..] });
        }
        SentenceBlock.Blocks.Add(p);
    }

    private void OnBack(object sender, RoutedEventArgs e)
    {
        if (ViewModel.TextId.Length > 0) App.MainWindow.NavigateTo("reading", ViewModel.TextId);
    }

    private void OnLibrary(object sender, RoutedEventArgs e) => App.MainWindow.NavigateTo("library");
}
