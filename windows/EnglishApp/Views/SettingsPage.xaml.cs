using EnglishApp.ViewModels;
using Microsoft.UI.Xaml.Controls;

namespace EnglishApp.Views;

public sealed partial class SettingsPage : Page
{
    public SettingsViewModel ViewModel { get; } = new();
    public SettingsPage() { InitializeComponent(); }
}
