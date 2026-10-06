using EnglishApp.ViewModels;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Navigation;

namespace EnglishApp.Views;

public sealed partial class ProgressPage : Page
{
    private const double MinTileWidth = 200;   // tile content width; each tile adds a 12px right gutter

    public ProgressViewModel ViewModel { get; } = new();
    public ProgressPage() { InitializeComponent(); }
    protected override void OnNavigatedTo(NavigationEventArgs e) => ViewModel.Load();

    private void OnOpenLibrary(object sender, RoutedEventArgs e) => App.MainWindow.NavigateTo("library");

    /// <summary>Stretch the stat tiles so each row is filled by as many >= 200px tiles as fit.</summary>
    private void OnStatsSizeChanged(object sender, SizeChangedEventArgs e)
    {
        var width = e.NewSize.Width - 1;
        if (width <= 0) return;
        var columns = Math.Clamp((int)(width / (MinTileWidth + 12)), 1, 4);
        var itemWidth = Math.Floor(width / columns);
        if (Math.Abs(StatsPanel.ItemWidth - itemWidth) > 0.5) StatsPanel.ItemWidth = itemWidth;
    }
}
