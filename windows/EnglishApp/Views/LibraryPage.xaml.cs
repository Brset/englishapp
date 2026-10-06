using EnglishApp.Models;
using EnglishApp.Services;
using EnglishApp.ViewModels;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Controls.Primitives;
using Microsoft.UI.Xaml.Navigation;

namespace EnglishApp.Views;

public sealed partial class LibraryPage : Page
{
    private const double MinCardWidth = 300;   // card content width; the container adds a 12px gutter

    public LibraryViewModel ViewModel { get; } = new();
    public LibraryPage() { InitializeComponent(); }

    protected override void OnNavigatedTo(NavigationEventArgs e)
    {
        ViewModel.Load();
        SyncLevelPills();
        AppServices.Jobs.JobFinished += OnJobFinished;
    }

    protected override void OnNavigatedFrom(NavigationEventArgs e) => AppServices.Jobs.JobFinished -= OnJobFinished;

    private void OnJobFinished(string textId, bool success) => ViewModel.Refresh();

    private void OnItemClick(object sender, ItemClickEventArgs e)
    {
        if (e.ClickedItem is TextSummary t) App.MainWindow.NavigateTo("reading", t.Id);
    }

    // ---- level pills (exclusive selection mirrored onto ViewModel.SelectedLevel) ----

    private IEnumerable<ToggleButton> LevelPills() => new[]
        { LevelPillAll, LevelPillA1, LevelPillA2, LevelPillB1, LevelPillB2, LevelPillC1, LevelPillC2 };

    private void OnLevelPillClick(object sender, RoutedEventArgs e)
    {
        if (sender is ToggleButton { Tag: string level }) ViewModel.SelectedLevel = level;
        SyncLevelPills();
    }

    private void SyncLevelPills()
    {
        foreach (var p in LevelPills()) p.IsChecked = (p.Tag as string) == ViewModel.SelectedLevel;
    }

    // ---- search / empty state ----

    private void OnSearchTextChanged(AutoSuggestBox sender, AutoSuggestBoxTextChangedEventArgs args)
    {
        if (args.Reason == AutoSuggestionBoxTextChangeReason.UserInput) ViewModel.SearchText = sender.Text;
    }

    private void OnResetFilters(object sender, RoutedEventArgs e)
    {
        ViewModel.ResetFilters();
        SyncLevelPills();
    }

    // ---- adaptive card grid: as many >= 300px columns as fit, stretched to fill the row ----

    private void OnCardsSizeChanged(object sender, SizeChangedEventArgs e)
    {
        if (Cards.ItemsPanelRoot is not ItemsWrapGrid panel) return;
        var width = e.NewSize.Width - Cards.Padding.Left - Cards.Padding.Right - 1;
        if (width <= 0) return;
        var columns = Math.Max(1, (int)(width / (MinCardWidth + 12)));
        panel.ItemWidth = Math.Floor(width / columns);
    }
}
