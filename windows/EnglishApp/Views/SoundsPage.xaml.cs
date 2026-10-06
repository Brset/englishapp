using EnglishApp.ViewModels;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Navigation;

namespace EnglishApp.Views;

public sealed partial class SoundsPage : Page
{
    public SoundsViewModel ViewModel { get; } = new();
    public SoundsPage() { InitializeComponent(); }
    protected override void OnNavigatedTo(NavigationEventArgs e) => ViewModel.Load();

    /// <summary>Minimal-pair pills, example tiles, phrases and twisters carry the text to speak in Tag.</summary>
    private async void OnSpeak(object sender, RoutedEventArgs e)
    {
        if (sender is FrameworkElement { Tag: string text }) await ViewModel.SpeakAsync(text);
    }

    public static Visibility VisibleWhenFalse(bool value) => value ? Visibility.Collapsed : Visibility.Visible;
}
