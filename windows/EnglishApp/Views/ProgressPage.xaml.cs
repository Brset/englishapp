using EnglishApp.Services;
using EnglishApp.ViewModels;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Navigation;

namespace EnglishApp.Views;

public sealed partial class ProgressPage : Page
{
    public ProgressViewModel ViewModel { get; } = new();
    public ProgressPage() { InitializeComponent(); }

    protected override void OnNavigatedTo(NavigationEventArgs e)
    {
        ViewModel.Load();
        Charts.Bars(WeeksHost, ViewModel.Weeks, Ui.Indigo);
        Charts.LineChart(ScoreCanvas, ViewModel.Scores, Ui.Indigo);
    }

    private void OnScoreCanvasSizeChanged(object sender, SizeChangedEventArgs e) =>
        Charts.LineChart(ScoreCanvas, ViewModel.Scores, Ui.Indigo);
}
