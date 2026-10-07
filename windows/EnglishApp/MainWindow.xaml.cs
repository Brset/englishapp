using EnglishApp.Models;
using EnglishApp.Services;
using EnglishApp.ViewModels;
using EnglishApp.Views;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Controls.Primitives;
using Microsoft.UI.Xaml.Media;

namespace EnglishApp;

public sealed partial class MainWindow : Window
{
    private bool _suppress;

    /// <summary>Text the user is currently working on (Reading/Record pages use it when opened from the menu).</summary>
    public string? CurrentTextId { get; set; }
    public ReviewArgs? LastReview { get; set; }

    /// <summary>Set when a reading session ends; the reading page shows it once.</summary>
    public CelebrationInfo? PendingCelebration { get; set; }

    private Microsoft.UI.Dispatching.DispatcherQueueTimer? _toastTimer;
    private bool _onboardingChecked;

    public MainWindow()
    {
        InitializeComponent();
        Title = "English Pronunciation";
        try { SystemBackdrop = new MicaBackdrop(); }
        catch (Exception ex) { Diagnostics.LogException("mica", ex); }
        Nav.SelectedItem = Nav.MenuItems[0];
        AppServices.Jobs.BadgesUnlocked += ShowBadges;
        Nav.Loaded += async (_, _) =>
        {
            if (_onboardingChecked) return;
            _onboardingChecked = true;
            try
            {
                if (!AppServices.Settings.Onboarded)
                {
                    var st = AppServices.Repo.GetStats();   // existing v1 user: has data already, skip the first-run tour
                    if (st.Attempts > 0 || st.TextsDone > 0 || st.TextsStarted > 0 || st.WordsSaved > 0) AppServices.Settings.Onboarded = true;
                    else await OnboardingDialog.ShowAsync(Nav.XamlRoot);
                }
            }
            catch (Exception ex) { Diagnostics.LogException("onboarding", ex); }
        };
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
        else if (e.InvokedItemContainer is NavigationViewItem { Tag: string tag } && ContentFrame.CurrentSourcePageType != PageFor(tag))
            ContentFrame.Navigate(PageFor(tag), ParamFor(tag));   // re-click of the highlighted parent while on a drill page
    }

    public void ShowBadges(IReadOnlyList<Achievement> list)
    {
        if (list.Count == 0) return;
        Toast.Title = list.Count == 1 ? "Новое достижение" : "Новые достижения";
        Toast.Message = string.Join(", ", list.Select(a => a.Title));
        Toast.IsOpen = true;
        _toastTimer ??= DispatcherQueue.CreateTimer();
        _toastTimer.Stop();
        _toastTimer.Interval = TimeSpan.FromSeconds(7);
        _toastTimer.IsRepeating = false;
        _toastTimer.Tick -= OnToastTick;
        _toastTimer.Tick += OnToastTick;
        _toastTimer.Start();
    }

    private void OnToastTick(Microsoft.UI.Dispatching.DispatcherQueueTimer sender, object args)
    {
        Toast.IsOpen = false;
        sender.Stop();
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
        "worddrill" => typeof(WordDrillPage),
        "sounddrill" => typeof(SoundDrillPage),
        "shadowing" => typeof(ShadowingPage),
        _ => typeof(SettingsPage),
    };

    private object? ParamFor(string tag) => tag switch
    {
        "reading" or "record" or "shadowing" => CurrentTextId,
        "review" => LastReview,
        _ => null,
    };

    private void OnSelectionChanged(NavigationView sender, NavigationViewSelectionChangedEventArgs e)
    {
        if (_suppress || e.SelectedItemContainer?.Tag is not string tag) return;
        if (ContentFrame.CurrentSourcePageType == PageFor(tag)) return;
        ContentFrame.Navigate(PageFor(tag), ParamFor(tag));
    }

    /// <summary>Programmatic navigation that also highlights the menu item.</summary>
    public void NavigateTo(string tag, object? parameter = null)
    {
        if (tag is "reading" or "record" or "shadowing" && parameter is string id) CurrentTextId = id;
        if (parameter is RecordArgs rec) CurrentTextId = rec.TextId;
        if (parameter is ReviewArgs ra) LastReview = ra;
        var highlight = tag switch { "worddrill" => "vocab", "sounddrill" => "sounds", "shadowing" => "reading", _ => tag };
        var item = Nav.MenuItems.Concat(Nav.FooterMenuItems).OfType<NavigationViewItem>().FirstOrDefault(i => (string?)i.Tag == highlight);
        _suppress = true;
        Nav.SelectedItem = item;
        _suppress = false;
        ContentFrame.Navigate(PageFor(tag), parameter ?? ParamFor(tag));
    }
}
