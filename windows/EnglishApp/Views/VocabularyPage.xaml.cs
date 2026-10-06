using EnglishApp.ViewModels;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Navigation;

namespace EnglishApp.Views;

public sealed partial class VocabularyPage : Page
{
    public VocabularyViewModel ViewModel { get; } = new();
    public VocabularyPage() { InitializeComponent(); }
    protected override void OnNavigatedTo(NavigationEventArgs e) => ViewModel.Load();

    private void OnDelete(object sender, RoutedEventArgs e)
    {
        if (sender is Button { Tag: long id } && ViewModel.Words.FirstOrDefault(w => w.Id == id) is { } w) ViewModel.Delete(w);
    }
}
