using EnglishApp.ViewModels;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Navigation;

namespace EnglishApp.Views;

public sealed partial class SoundsPage : Page
{
    public SoundsViewModel ViewModel { get; } = new();
    public SoundsPage() { InitializeComponent(); }
    protected override void OnNavigatedTo(NavigationEventArgs e) => ViewModel.Load();
}
