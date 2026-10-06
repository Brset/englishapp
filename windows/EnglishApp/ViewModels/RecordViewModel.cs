using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using EnglishApp.Models;
using EnglishApp.Native;
using EnglishApp.Services;
using Microsoft.UI.Dispatching;

namespace EnglishApp.ViewModels;

public partial class RecordViewModel : ObservableObject
{
    private readonly DispatcherQueue _ui = DispatcherQueue.GetForCurrentThread();
    private readonly System.Diagnostics.Stopwatch _clock = new();
    private DispatcherQueueTimer? _timer;
    private TextDetail? _text;

    [ObservableProperty] private string title = "Выберите текст в библиотеке";
    [ObservableProperty] private string referenceText = "";
    [ObservableProperty] private bool isRecording;
    [ObservableProperty] private bool isBusy;
    [ObservableProperty] private double level;
    [ObservableProperty] private string elapsed = "0:00";
    [ObservableProperty] private string status = "Нажмите Пробел, чтобы начать запись.";
    [ObservableProperty] private string buttonText = "Начать запись (Пробел)";

    public bool HasText => _text != null;

    // ---- live tracking ----
    private volatile bool _liveRequested;
    private volatile bool _liveDone;
    private long _lastVoiceTick;
    private const float VoiceLevel = 0.1f;
    private const long SilenceMs = 1500;

    /// <summary>Word tokens (same indexing as the live state); empty when the engine is unavailable.</summary>
    public IReadOnlyList<TokenInfo> Tokens { get; private set; } = Array.Empty<TokenInfo>();
    /// <summary>True while a live session is running (UI thread).</summary>
    public bool LiveActive { get; private set; }
    /// <summary>UI thread: live session opened; reset word states.</summary>
    public event Action? LiveStarted;
    /// <summary>UI thread: new live state.</summary>
    public event Action<LiveState>? LiveUpdated;
    /// <summary>UI thread: live session closed.</summary>
    public event Action? LiveEnded;

    public void SetCursor(int wordIndex)
    {
        if (LiveActive) AppServices.Engine.SetLiveCursor(wordIndex);
    }

    /// <summary>Raised on the UI thread when the reading is saved and its assessment queued (argument: text id).</summary>
    public event Action<string>? Queued;

    // Latest known live state per word index (merged across incremental updates), for the quick read/skipped marks.
    private readonly Dictionary<int, string> _liveStates = new();

    private void MergeLive(LiveState st)
    {
        foreach (var w in st.Words) _liveStates[w.I] = w.State;
    }

    private string? BuildLiveMarks()
    {
        if (_liveStates.Count == 0) return null;
        int n = Math.Max(Tokens.Count, _liveStates.Keys.Max() + 1);
        var chars = new char[n];
        for (int i = 0; i < n; i++)
            chars[i] = _liveStates.TryGetValue(i, out var st) ? (st == "read" ? 'r' : st == "skipped" ? 's' : '-') : '-';
        return new string(chars);
    }

    public RecordViewModel()
    {
        AppServices.Recorder.LevelChanged += OnLevel;
        AppServices.Recorder.Failed += OnFailed;
    }

    public void Detach()
    {
        AppServices.Recorder.LevelChanged -= OnLevel;
        AppServices.Recorder.Failed -= OnFailed;
        StopLiveWiring();
        _timer?.Stop();
        if (AppServices.Recorder.IsRecording) _ = AppServices.Recorder.StopAsync();
        _ = AppServices.Engine.FinishLive();
        LiveActive = false;
    }

    private void OnLevel(float v)
    {
        _ui.TryEnqueue(() => Level = v * 100);
        if (!_liveRequested) return;
        var now = Environment.TickCount64;
        if (v > VoiceLevel) Interlocked.Exchange(ref _lastVoiceTick, now);
        else if (_liveDone && now - Interlocked.Read(ref _lastVoiceTick) >= SilenceMs)
        {
            _liveDone = false;   // fire once
            _ui.TryEnqueue(async () =>
            {
                if (IsRecording && !IsBusy) await StopAndAssessAsync();
            });
        }
    }

    private void OnChunk(short[] chunk) => AppServices.Engine.FeedLive(chunk);

    private void OnLiveState(LiveState st)
    {
        _ui.TryEnqueue(() =>
        {
            MergeLive(st);
            LiveUpdated?.Invoke(st);
            if (!IsRecording) return;
            if (st.Done && !_liveDone && _liveRequested)
            {
                Interlocked.Exchange(ref _lastVoiceTick, Environment.TickCount64);
                _liveDone = true;
                Status = "Текст дочитан. Остановка через 1,5 с тишины или нажмите Пробел.";
            }
            else if (!st.Done) _liveDone = false;
        });
    }

    private void StopLiveWiring()
    {
        AppServices.Recorder.ChunkAvailable -= OnChunk;
        AppServices.Engine.LiveUpdated -= OnLiveState;
        _liveRequested = false;
        _liveDone = false;
    }
    private void OnFailed(Exception ex) => _ui.TryEnqueue(() => { Status = "Ошибка записи: " + ex.Message; IsRecording = false; });

    public void Load(string? textId)
    {
        _text = string.IsNullOrEmpty(textId) ? null : AppServices.Repo.GetText(textId);
        if (_text == null) return;
        Title = _text.TitleEn;
        ReferenceText = _text.Body;
        try { Tokens = PronAssessor.Tokenize(_text.Body); }
        catch (Exception ex) { Diagnostics.LogException("tokenize", ex); Tokens = Array.Empty<TokenInfo>(); }
        OnPropertyChanged(nameof(HasText));
    }

    [RelayCommand]
    public async Task ToggleAsync()
    {
        if (IsBusy || _text == null) return;
        if (!IsRecording) await StartRecordingAsync(); else await StopAndAssessAsync();
    }

    private async Task StartRecordingAsync()
    {
        AppServices.Player.Stop();
        var path = Path.Combine(AppPaths.RecordingsDir, $"{_text!.Id}_{DateTime.Now:yyyyMMdd_HHmmss}.wav");
        Task<bool>? liveTask = null;
        string? hint = null;
        if (AppServices.Engine.LiveSupported)
        {
            _liveDone = false;
            _liveStates.Clear();
            Interlocked.Exchange(ref _lastVoiceTick, Environment.TickCount64);
            _liveRequested = true;
            AppServices.Engine.LiveUpdated += OnLiveState;
            AppServices.Recorder.ChunkAvailable += OnChunk;
            liveTask = AppServices.Engine.StartLive(_text.Body);   // sync part only queues; audio is buffered until it opens
        }
        else hint = "Живая подсветка недоступна (модель live не найдена): запись работает как обычно.";
        try { AppServices.Recorder.Start(path, AppServices.Settings.MicDevice); }
        catch (Exception ex)
        {
            Status = "Не удалось открыть микрофон: " + ex.Message;
            StopLiveWiring();
            _ = AppServices.Engine.FinishLive();
            return;
        }
        IsRecording = true;
        ButtonText = "Остановить (Пробел)";
        Status = "Идёт запись… Читайте текст вслух.";
        _clock.Restart();
        _timer = _ui.CreateTimer();
        _timer.Interval = TimeSpan.FromMilliseconds(250);
        _timer.Tick += (_, _) => Elapsed = $"{(int)_clock.Elapsed.TotalMinutes}:{_clock.Elapsed.Seconds:00}";
        _timer.Start();
        if (hint != null) Status = "Идёт запись… " + hint;
        if (liveTask != null)
        {
            bool ok = await liveTask;
            if (ok && _liveRequested)
            {
                LiveActive = true;
                LiveStarted?.Invoke();
            }
            else if (!ok)
            {
                StopLiveWiring();
                if (IsRecording) Status = "Идёт запись… Живая подсветка недоступна (модель live не найдена или ошибка): запись работает как обычно.";
            }
        }
    }

    private async Task StopAndAssessAsync()
    {
        _timer?.Stop();
        IsBusy = true; IsRecording = false;
        ButtonText = "Обработка…";
        Status = "Сохранение записи…";
        var seconds = _clock.Elapsed.TotalSeconds;
        try
        {
            var wav = await AppServices.Recorder.StopAsync();
            _liveDone = false;
            if (_liveRequested)
            {
                AppServices.Recorder.ChunkAvailable -= OnChunk;
                var fin = await AppServices.Engine.FinishLive();   // flushes queued audio; the final state also arrives via LiveUpdated
                if (fin != null) MergeLive(fin);
                StopLiveWiring();
                LiveActive = false;
                LiveEnded?.Invoke();
            }
            if (wav == null) return;
            // Heavy assessment runs in the background queue; the text is marked read right away.
            AppServices.Jobs.Enqueue(_text!.Id, wav, seconds, BuildLiveMarks());
            Status = "Чтение сохранено. Оценка готовится в фоне…";
            Queued?.Invoke(_text.Id);
        }
        catch (Exception ex)
        {
            Status = "Ошибка сохранения: " + ex.Message;
        }
        finally
        {
            IsBusy = false;
            ButtonText = "Начать запись (Пробел)";
        }
    }

    [RelayCommand]
    public async Task ReplayReferenceAsync()
    {
        if (_text == null || IsRecording) return;
        if (!await Speech.SpeakAsync(_text.Body.Replace("\n\n", "\n"), AppServices.Settings.Speed))
            Status = "Синтез речи недоступен.";
    }
}
