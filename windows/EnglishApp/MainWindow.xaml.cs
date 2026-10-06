using EnglishApp.ViewModels;
using EnglishApp.Views;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;

namespace EnglishApp;

public sealed partial class MainWindow : Window
{
    private bool _suppress;

    /// <summary>Text the user is currently working on (Reading/Record pages use it when opened from the menu).</summary>
    public string? CurrentTextId { get; set; }
    public ReviewArgs? LastReview { get; set; }

    public MainWindow()
    {
        InitializeComponent();
        Title = "English Pronunciation";
        Nav.SelectedItem = Nav.MenuItems[0];
    }

    public void ApplyTheme(string theme)
    {
        if (Content is FrameworkElement fe)
            fe.RequestedTheme = theme switch { "Light" => ElementTheme.Light, "Dark" => ElementTheme.Dark, _ => ElementTheme.Default };
    }

    private static Type PageFor(string tag) => tag switch
    {
        "home" => typeof(HomePage),
        "library" => typeof(LibraryPage),
        "reading" => typeof(ReadingPage),
        "record" => typeof(RecordPage),
        "review" => typeof(ReviewPage),
        "sounds" => typeof(SoundsPage),
        "vocab" => typeof(VocabularyPage),
        "progress" => typeof(ProgressPage),
        _ => typeof(SettingsPage),
    };

    private object? ParamFor(string tag) => tag switch
    {
        "reading" or "record" => CurrentTextId,
        "review" => LastReview,
        _ => null,
    };

    private void OnSelectionChanged(NavigationView sender, NavigationViewSelectionChangedEventArgs e)
    {
        if (_suppress || e.SelectedItemContainer?.Tag is not string tag) return;
        ContentFrame.Navigate(PageFor(tag), ParamFor(tag));
    }

    /// <summary>Programmatic navigation that also highlights the menu item.</summary>
    public void NavigateTo(string tag, object? parameter = null)
    {
        if (tag is "reading" or "record" && parameter is string id) CurrentTextId = id;
        if (parameter is ReviewArgs ra) LastReview = ra;
        var item = Nav.MenuItems.Concat(Nav.FooterMenuItems).OfType<NavigationViewItem>().FirstOrDefault(i => (string?)i.Tag == tag);
        _suppress = true;
        Nav.SelectedItem = item;
        _suppress = false;
        ContentFrame.Navigate(PageFor(tag), parameter ?? ParamFor(tag));
    }
}
