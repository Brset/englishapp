using EnglishApp.Native;
using EnglishApp.ViewModels;
using Microsoft.UI;
using Microsoft.UI.Text;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Documents;
using Microsoft.UI.Xaml.Input;
using Microsoft.UI.Xaml.Media;
using Microsoft.UI.Xaml.Navigation;

namespace EnglishApp.Views;

public sealed partial class RecordPage : Page
{
    public RecordViewModel ViewModel { get; } = new();

    private readonly List<Run?> _runs = new();
    private readonly Dictionary<Run, int> _runIndex = new();
    private string?[] _applied = Array.Empty<string?>();
    private long _manualUntil;                    // auto-scroll paused until this tick (ms)
    private int _forceFrom = int.MaxValue;        // re-apply states from here (after a manual cursor jump)
    private bool _gotUpdate;

    private static readonly SolidColorBrush ReadBrush = new(ColorHelper.FromArgb(255, 0x2E, 0x9E, 0x5B));
    private static readonly SolidColorBrush SkippedBrush = new(ColorHelper.FromArgb(255, 0xE0, 0x80, 0x1A));
    private static readonly SolidColorBrush PendingBrush = new(ColorHelper.FromArgb(255, 0x80, 0x80, 0x80));

    public RecordPage()
    {
        InitializeComponent();
        // Manual scrolling pauses the teleprompter. ScrollViewer marks wheel/press handled, hence handledEventsToo.
        Scroller.AddHandler(UIElement.PointerWheelChangedEvent, new PointerEventHandler(OnManualScroll), true);
        Scroller.AddHandler(UIElement.PointerPressedEvent, new PointerEventHandler(OnManualScroll), true);
        Scroller.ViewChanging += OnViewChanging;
    }

    protected override void OnNavigatedTo(NavigationEventArgs e)
    {
        ViewModel.Load(e.Parameter as string ?? App.MainWindow.CurrentTextId);
        BuildText(ViewModel.ReferenceText, ViewModel.Tokens);
        ViewModel.Queued += OnQueued;
        ViewModel.LiveStarted += OnLiveStarted;
        ViewModel.LiveUpdated += OnLiveUpdated;
    }

    protected override void OnNavigatedFrom(NavigationEventArgs e)
    {
        ViewModel.Queued -= OnQueued;
        ViewModel.LiveStarted -= OnLiveStarted;
        ViewModel.LiveUpdated -= OnLiveUpdated;
        ViewModel.Detach();
    }

    private void OnQueued(string textId) => App.MainWindow.NavigateTo("reading", textId);

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

    // ---------------- text rendering ----------------

    private void BuildText(string text, IReadOnlyList<TokenInfo> tokens)
    {
        Body.Blocks.Clear();
        _runs.Clear();
        _runIndex.Clear();
        _applied = Array.Empty<string?>();
        _gotUpdate = false;
        var p = new Paragraph();
        int pos = 0;

        void AddGap(string gap)
        {
            var lines = gap.Replace("\r", "").Split('\n');
            for (int i = 0; i < lines.Length; i++)
            {
                if (i > 0) p.Inlines.Add(new LineBreak());
                if (lines[i].Length > 0) p.Inlines.Add(new Run { Text = lines[i] });
            }
        }

        foreach (var t in tokens)
        {
            if (t.U16Begin < pos || t.U16End <= t.U16Begin || t.U16End > text.Length)
            {
                _runs.Add(null);   // keep indexes aligned with the engine's tokens
                continue;
            }
            if (t.U16Begin > pos) AddGap(text[pos..t.U16Begin]);
            var run = new Run { Text = text[t.U16Begin..t.U16End] };
            p.Inlines.Add(run);
            _runIndex[run] = _runs.Count;
            _runs.Add(run);
            pos = t.U16End;
        }
        if (pos < text.Length) AddGap(text[pos..]);
        Body.Blocks.Add(p);
        _applied = new string?[_runs.Count];
    }


    private Brush AccentBrush()
    {
        try
        {
            var ui = new Windows.UI.ViewManagement.UISettings();
            var c = ui.GetColorValue(ActualTheme == ElementTheme.Dark
                ? Windows.UI.ViewManagement.UIColorType.AccentLight2
                : Windows.UI.ViewManagement.UIColorType.AccentDark1);
            return new SolidColorBrush(c);
        }
        catch (Exception) { return new SolidColorBrush(Colors.DodgerBlue); }
    }

    private void SetWordState(int i, string? state)
    {
        if (i < 0 || i >= _runs.Count || _runs[i] is not { } r) return;
        _applied[i] = state;
        switch (state)
        {
            case "read":
                r.Foreground = ReadBrush; r.FontWeight = FontWeights.Normal;
                break;
            case "skipped":
                r.Foreground = SkippedBrush; r.FontWeight = FontWeights.Normal;
                break; // no underline: TextDecorations is ambiguous between WinUI and the Windows SDK projection
            case "current":
                r.Foreground = AccentBrush(); r.FontWeight = FontWeights.Bold;
                break;
            case "pending":
                r.Foreground = PendingBrush; r.FontWeight = FontWeights.Normal;
                break;
            default:
                r.ClearValue(TextElement.ForegroundProperty); r.FontWeight = FontWeights.Normal;
                break;
        }
    }

    // ---------------- live tracking ----------------

    private void OnLiveStarted()
    {
        if (_gotUpdate) return;   // a state already arrived; do not overwrite it
        for (int i = 0; i < _runs.Count; i++) SetWordState(i, i == 0 ? "current" : "pending");
        _manualUntil = 0;
        Scroller.ChangeView(null, 0, null, true);
    }

    private void OnLiveUpdated(LiveState st)
    {
        _gotUpdate = true;
        if (_applied.Length != _runs.Count) _applied = new string?[_runs.Count];
        int from = Math.Max(0, Math.Min(st.ChangedFrom, _forceFrom));
        _forceFrom = int.MaxValue;
        foreach (var w in st.Words)
        {
            if (w.I < from || w.I >= _runs.Count) continue;
            if (_applied[w.I] == w.State) continue;
            SetWordState(w.I, w.State);
        }
        ScrollToWord(st.ScrollTo);
    }

    private void ScrollToWord(int i)
    {
        if (i < 0 || i >= _runs.Count || _runs[i] is not { } r) return;
        if (Environment.TickCount64 < _manualUntil) return;
        try
        {
            var rect = r.ContentStart.GetCharacterRect(LogicalDirection.Forward);   // relative to Body (top of scroll content)
            double target = Math.Max(0, rect.Y - Scroller.ViewportHeight / 3.0);
            if (Math.Abs(target - Scroller.VerticalOffset) < 24) return;            // avoid per-word jitter
            Scroller.ChangeView(null, target, null, false);
        }
        catch (Exception) { /* layout not ready */ }
    }

    private void OnManualScroll(object sender, PointerRoutedEventArgs e) => _manualUntil = Environment.TickCount64 + 3000;

    private void OnViewChanging(object? sender, ScrollViewerViewChangingEventArgs e)
    {
        if (e.IsInertial) _manualUntil = Environment.TickCount64 + 3000;   // touch fling
    }

    private void OnScrollerSizeChanged(object sender, SizeChangedEventArgs e)
    {
        // extra room below the text so the last words can still sit a third from the top
        Body.Margin = new Thickness(0, 0, 0, Math.Max(0, e.NewSize.Height * 2 / 3));
    }

    private void OnBodyTapped(object sender, TappedRoutedEventArgs e)
    {
        if (!ViewModel.LiveActive) return;
        var ptr = Body.GetPositionFromPoint(e.GetPosition(Body));
        if (ptr?.Parent is not Run run || !_runIndex.TryGetValue(run, out var idx)) return;
        _manualUntil = 0;
        _forceFrom = Math.Min(_forceFrom, idx);
        for (int i = idx; i < _runs.Count; i++) SetWordState(i, i == idx ? "current" : "pending");
        ViewModel.SetCursor(idx);
    }
}
