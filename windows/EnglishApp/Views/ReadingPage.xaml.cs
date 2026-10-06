using System.Text.RegularExpressions;
using EnglishApp.Services;
using EnglishApp.ViewModels;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Documents;
using Microsoft.UI.Xaml.Input;
using Microsoft.UI.Xaml.Navigation;

namespace EnglishApp.Views;

public sealed partial class ReadingPage : Page
{
    private static readonly Regex WordRx = new(@"[A-Za-z]+(?:['’][A-Za-z]+)*", RegexOptions.Compiled);

    public ReadingViewModel ViewModel { get; } = new();
    public ReadingPage() { InitializeComponent(); }

    protected override void OnNavigatedTo(NavigationEventArgs e)
    {
        if (!ViewModel.Load(e.Parameter as string)) return;
        BuildText(ViewModel.Text!.Body);
    }

    protected override void OnNavigatedFrom(NavigationEventArgs e) => AppServices.Player.Stop();

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

    private void OnRecord(object sender, RoutedEventArgs e)
    {
        if (ViewModel.Text != null) App.MainWindow.NavigateTo("record", ViewModel.Text.Id);
    }
}
