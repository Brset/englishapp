using EnglishApp.ViewModels;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Navigation;

namespace EnglishApp.Views;

public sealed partial class SoundDrillPage : Page
{
    public SoundDrillViewModel ViewModel { get; } = new();
    public SoundDrillPage() { InitializeComponent(); }

    protected override void OnNavigatedTo(NavigationEventArgs e) => ViewModel.Load(e.Parameter as string);

    protected override void OnNavigatedFrom(NavigationEventArgs e) { EnglishApp.Services.QuickSpeech.CancelCurrent(); EnglishApp.Services.AppServices.Player.Stop(); }
}
