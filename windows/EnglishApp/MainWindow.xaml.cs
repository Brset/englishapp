using EnglishApp.Services;
using EnglishApp.ViewModels;
using EnglishApp.Views;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Controls.Primitives;

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
        QueueList.ItemsSource = AppServices.Jobs.Jobs;
        AppServices.Jobs.Jobs.CollectionChanged += (_, _) => UpdateQueueUi();
        UpdateQueueUi();
    }

    /// <summary>Footer "Обработка" item (ProgressRing + count badge) is visible only while the queue is non-empty.</summary>
    private void UpdateQueueUi()
    {
        int n = AppServices.Jobs.Jobs.Count;
        QueueBadge.Value = n;
        QueueItem.Visibility = n > 0 ? Visibility.Visible : Visibility.Collapsed;
        if (n == 0) QueueFlyout.Hide();
    }

    private void OnItemInvoked(NavigationView sender, NavigationViewItemInvokedEventArgs e)
    {
        if (e.InvokedItemContainer is NavigationViewItem { Tag: "processing" } item)
            FlyoutBase.ShowAttachedFlyout(item);
    }

    private void OnCancelJob(object sender, RoutedEventArgs e)
    {
        if (sender is FrameworkElement { DataContext: JobItem job }) AppServices.Jobs.Cancel(job.Id);
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
