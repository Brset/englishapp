using EnglishApp.ViewModels;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Navigation;

namespace EnglishApp.Views;

public sealed partial class HomePage : Page
{
    public HomeViewModel ViewModel { get; } = new();
    public HomePage() { InitializeComponent(); }

    protected override void OnNavigatedTo(NavigationEventArgs e) => ViewModel.Load();

    private void OnContinue(object sender, RoutedEventArgs e)
    {
        if (ViewModel.ContinueText != null) App.MainWindow.NavigateTo("reading", ViewModel.ContinueText.Id);
    }

    private void OnToday(object sender, RoutedEventArgs e)
    {
        if (ViewModel.TodayText != null) App.MainWindow.NavigateTo("reading", ViewModel.TodayText.Id);
    }
}
