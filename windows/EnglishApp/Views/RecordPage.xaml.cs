using EnglishApp.ViewModels;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Input;
using Microsoft.UI.Xaml.Navigation;

namespace EnglishApp.Views;

public sealed partial class RecordPage : Page
{
    public RecordViewModel ViewModel { get; } = new();
    public RecordPage() { InitializeComponent(); }

    protected override void OnNavigatedTo(NavigationEventArgs e)
    {
        ViewModel.Load(e.Parameter as string ?? App.MainWindow.CurrentTextId);
        ViewModel.Completed += OnCompleted;
    }

    protected override void OnNavigatedFrom(NavigationEventArgs e)
    {
        ViewModel.Completed -= OnCompleted;
        ViewModel.Detach();
    }

    private void OnCompleted(ReviewArgs args) => App.MainWindow.NavigateTo("review", args);

    private async void OnSpace(KeyboardAccelerator sender, KeyboardAcceleratorInvokedEventArgs args)
    {
        args.Handled = true;
        await ViewModel.ToggleAsync();
    }

    private async void OnReplay(KeyboardAccelerator sender, KeyboardAcceleratorInvokedEventArgs args)
    {
        args.Handled = true;
        await ViewModel.ReplayReferenceAsync();
    }
}
