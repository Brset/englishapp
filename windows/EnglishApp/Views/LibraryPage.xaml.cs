using EnglishApp.Models;
using EnglishApp.ViewModels;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Navigation;

namespace EnglishApp.Views;

public sealed partial class LibraryPage : Page
{
    public LibraryViewModel ViewModel { get; } = new();
    public LibraryPage() { InitializeComponent(); }

    protected override void OnNavigatedTo(NavigationEventArgs e) => ViewModel.Load();

    private void OnItemClick(object sender, ItemClickEventArgs e)
    {
        if (e.ClickedItem is TextSummary t) App.MainWindow.NavigateTo("reading", t.Id);
    }
}
