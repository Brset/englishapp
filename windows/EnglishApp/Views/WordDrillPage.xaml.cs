using EnglishApp.ViewModels;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Navigation;

namespace EnglishApp.Views;

public sealed partial class WordDrillPage : Page
{
    public WordDrillViewModel ViewModel { get; } = new();
    public WordDrillPage() { InitializeComponent(); }

    protected override void OnNavigatedTo(NavigationEventArgs e) => ViewModel.Load();

    protected override void OnNavigatedFrom(NavigationEventArgs e) { EnglishApp.Services.QuickSpeech.CancelCurrent(); EnglishApp.Services.AppServices.Player.Stop(); }

    private void OnVocab(object sender, RoutedEventArgs e) => App.MainWindow.NavigateTo("vocab");
}
