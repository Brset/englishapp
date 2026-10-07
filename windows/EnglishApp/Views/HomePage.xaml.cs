using EnglishApp.Models;
using EnglishApp.Services;
using EnglishApp.ViewModels;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Navigation;

namespace EnglishApp.Views;

public sealed partial class HomePage : Page
{
    public HomeViewModel ViewModel { get; } = new();
    public HomePage() { InitializeComponent(); }

    protected override void OnNavigatedTo(NavigationEventArgs e)
    {
        ViewModel.Load();
        Charts.Bars(WeekHost, ViewModel.Week, Ui.Indigo);
    }

    private void OnContinue(object sender, RoutedEventArgs e)
    {
        if (ViewModel.ContinueArgs is { } args) App.MainWindow.NavigateTo("record", args);
        else if (ViewModel.ContinueText != null) App.MainWindow.NavigateTo("reading", ViewModel.ContinueText.Id);
    }

    private void OnToday(object sender, RoutedEventArgs e)
    {
        if (ViewModel.TodayText != null) App.MainWindow.NavigateTo("reading", ViewModel.TodayText.Id);
    }

    private void OnLibrary(object sender, RoutedEventArgs e) => App.MainWindow.NavigateTo("library");

    private void OnWeakSound(object sender, RoutedEventArgs e)
    {
        if (sender is Button { Tag: string ipa }) App.MainWindow.NavigateTo("sounddrill", ipa);
    }
}
