using EnglishApp.Models;
using EnglishApp.Services;
using EnglishApp.ViewModels;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Navigation;

namespace EnglishApp.Views;

public sealed partial class LibraryPage : Page
{
    public LibraryViewModel ViewModel { get; } = new();
    public LibraryPage() { InitializeComponent(); }

    protected override void OnNavigatedTo(NavigationEventArgs e)
    {
        ViewModel.Load();
        AppServices.Jobs.JobFinished += OnJobFinished;
    }

    protected override void OnNavigatedFrom(NavigationEventArgs e) => AppServices.Jobs.JobFinished -= OnJobFinished;

    private void OnJobFinished(string textId, bool success) => ViewModel.Refresh();

    private void OnItemClick(object sender, ItemClickEventArgs e)
    {
        if (e.ClickedItem is TextSummary t) App.MainWindow.NavigateTo("reading", t.Id);
    }
}
