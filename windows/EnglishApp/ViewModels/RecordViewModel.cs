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
    [ObservableProperty] private bool isParagraphMode;
    [ObservableProperty] private bool autoAdvance = true;
    [ObservableProperty] private int paraIndex;
    [ObservableProperty] private string paraLabel = "";
    [ObservableProperty] private double coveragePct;
    [ObservableProperty] private string coverageText = "Прочитано 0%";

    public ReadingTypography Typo { get; } = new();
    public bool CanChangeMode => !IsRecording && !IsBusy && _paras.Count > 1;
    partial void OnIsRecordingChanged(bool value) => OnPropertyChanged(nameof(CanChangeMode));
    partial void OnIsBusyChanged(bool value) => OnPropertyChanged(nameof(CanChangeMode));
    partial void OnIsParagraphModeChanged(bool value)
    {
        if (_loading || _text == null) return;
        _paraRead.Clear(); _paraMarks.Clear(); _paraTokens.Clear();
        ParaIndex = 0;
        Rebuild();
    }

    private bool _loading;
    private List<ParagraphInfo> _paras = new();
    private readonly Dictionary<int, int> _paraRead = new();         // key: paragraph index (whole text: -1)
    private readonly Dictionary<int, string> _paraMarks = new();
    private readonly Dictionary<int, IReadOnlyList<TokenInfo>> _paraTokens = new();
    private int _sessionXp;
    private int _totalWords;

    public IReadOnlyList<ParagraphInfo> Paragraphs => _paras;
    public bool HasParagraphs => _paras.Count > 1;
    public string? TextId => _text?.Id;
    /// <summary>UI thread: reference text / mode / paragraph changed; the page must rebuild its runs.</summary>
    public event Action? ViewChanged;
    public string? MarksOf(int paragraph) => _paraMarks.TryGetValue(paragraph, out var m) ? m : null;

    public IReadOnlyList<TokenInfo> TokensOf(int paragraph)
    {
        if (_paraTokens.TryGetValue(paragraph, out var t)) return t;
        t = paragraph >= 0 && paragraph < _paras.Count ? TokenizeSafe(_paras[paragraph].Text) : Array.Empty<TokenInfo>();
        _paraTokens[paragraph] = t;
        return t;
    }

    private static IReadOnlyList<TokenInfo> TokenizeSafe(string text)
    {
        try { return PronAssessor.Tokenize(text); }
        catch (Exception ex) { Diagnostics.LogException("tokenize", ex); return Array.Empty<TokenInfo>(); }
    }

    private int CurrentKey => IsParagraphMode ? ParaIndex : -1;

    private void Rebuild()
    {
        if (_text == null) return;
        if (IsParagraphMode && _paras.Count > 0)
        {
            ParaIndex = Math.Clamp(ParaIndex, 0, _paras.Count - 1);
            ReferenceText = _paras[ParaIndex].Text;
            ParaLabel = $"Абзац {ParaIndex + 1}/{_paras.Count}";
        }
        else { ReferenceText = _text.Body; ParaLabel = ""; }
        Tokens = TokenizeSafe(ReferenceText);
        _liveStates.Clear();
        UpdateCoverage();
        ViewChanged?.Invoke();
    }

    private void GoToParagraph(int i)
    {
        ParaIndex = Math.Clamp(i, 0, Math.Max(0, _paras.Count - 1));
        Rebuild();
    }

    private void UpdateCoverage()
    {
        int saved = _paraRead.TryGetValue(CurrentKey, out var sv) ? sv : 0;
        int own = _liveStates.Count > 0 ? _liveStates.Values.Count(st => st == "read") : saved;
        double pct;
        if (IsParagraphMode)
        {
            int others = _paraRead.Where(kv => kv.Key != ParaIndex).Sum(kv => kv.Value);
            pct = (others + own) * 100.0 / Math.Max(1, _totalWords);
        }
        else pct = own * 100.0 / Math.Max(1, Tokens.Count);
        CoveragePct = Math.Min(100, Math.Round(pct));
        CoverageText = $"Прочитано {CoveragePct:0}%";
    }

    public bool HasText => _text != null;
    public bool NoText => _text == null;

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
        UpdateCoverage();
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
                if (IsRecording && !IsBusy) await StopAndAssessAsync(forceAdvance: true);
            });
        }
    }

    private void OnChunk(short[] chunk) => AppServices.Engine.FeedLive(chunk);

    private void OnLiveState(LiveState st)
    {
        _ui.TryEnqueue(() =>
        {
            if (!_liveRequested) return;   // stale state of a session that was already closed
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

    public void Load(string? textId, int? startParagraph = null, bool? paragraphMode = null)
    {
        _text = string.IsNullOrEmpty(textId) ? null : AppServices.Repo.GetText(textId);
        if (_text == null) return;
        _loading = true;
        Title = _text.TitleEn;
        _paras = TextParagraphs.Split(_text.Body);
        _totalWords = _paras.Sum(p => TextParagraphs.WordCount(p.Text));
        _paraRead.Clear(); _paraMarks.Clear(); _paraTokens.Clear();
        _sessionXp = 0;
        IsParagraphMode = paragraphMode ?? (_text.WordCount > 300 && _paras.Count > 1);
        if (_paras.Count < 2) IsParagraphMode = false;
        ParaIndex = IsParagraphMode ? Math.Clamp(startParagraph ?? 0, 0, _paras.Count - 1) : 0;
        _loading = false;
        Rebuild();
        OnPropertyChanged(nameof(HasText));
        OnPropertyChanged(nameof(HasParagraphs));
        OnPropertyChanged(nameof(CanChangeMode));
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
        string suffix = IsParagraphMode ? "_p" + (ParaIndex + 1) : "";
        var path = Path.Combine(AppPaths.RecordingsDir, $"{_text!.Id}_{DateTime.Now:yyyyMMdd_HHmmss}{suffix}.wav");
        Task<bool>? liveTask = null;
        string? hint = null;
        if (AppServices.Engine.LiveSupported)
        {
            _liveDone = false;
            _liveStates.Clear();
            UpdateCoverage();
            Interlocked.Exchange(ref _lastVoiceTick, Environment.TickCount64);
            _liveRequested = true;
            AppServices.Engine.LiveUpdated += OnLiveState;
            AppServices.Recorder.ChunkAvailable += OnChunk;
            liveTask = AppServices.Engine.StartLive(ReferenceText);   // sync part only queues; audio is buffered until it opens
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

    private async Task StopAndAssessAsync(bool forceAdvance = false, bool finish = false)
    {
        _timer?.Stop();
        IsBusy = true; IsRecording = false;
        ButtonText = "Обработка…";
        Status = "Сохранение записи…";
        var seconds = _clock.Elapsed.TotalSeconds;
        bool saved = false;
        double fraction = 0;
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
            var marks = BuildLiveMarks();
            // Heavy assessment runs in the background queue (one job per paragraph in paragraph mode).
            AppServices.Jobs.Enqueue(_text!.Id, wav, seconds, marks, IsParagraphMode ? ParaIndex : null, IsParagraphMode ? _paras.Count : 0);
            saved = true;
            fraction = OnSaved(marks);
            Status = "Чтение сохранено. Оценка готовится в фоне…";
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
        if (saved) await AfterSavedAsync(fraction, forceAdvance, finish);
    }

    /// <summary>Stores XP / position for the saved attempt; returns the fraction of the reference that was read.</summary>
    private double OnSaved(string? marks)
    {
        int read = marks?.Count(c => c == 'r') ?? 0;
        int len = marks?.Length ?? 0;
        double fraction = marks == null ? 1 : len > 0 ? (double)read / len : 0;
        var repo = AppServices.Repo;
        int key = CurrentKey;
        _paraRead.TryGetValue(key, out var prev);
        int inc = Math.Max(0, read - prev);
        if (read > prev) _paraRead[key] = read;
        if (IsParagraphMode) _paraMarks[ParaIndex] = marks ?? "";
        try
        {
            _sessionXp += repo.AwardXp(inc, "reading");
            SavePositionAfter(marks, fraction);
        }
        catch (Exception ex) { Diagnostics.LogException("save progress", ex); }
        UpdateCoverage();
        return fraction;
    }

    private void SavePositionAfter(string? marks, double fraction)
    {
        if (_text == null || marks == null) return;
        var repo = AppServices.Repo;
        int lastRead = marks.LastIndexOf('r');
        if (IsParagraphMode)
        {
            bool complete = fraction >= 0.8;
            int pi = complete ? ParaIndex + 1 : ParaIndex;
            if (pi >= _paras.Count) { repo.ClearPosition(_text.Id); return; }
            int words = _paras.Take(pi).Sum(p => TextParagraphs.WordCount(p.Text)) + (complete ? 0 : Math.Max(0, lastRead + 1));
            repo.SavePosition(_text.Id, words, pi);
        }
        else
        {
            if (fraction >= 0.9) { repo.ClearPosition(_text.Id); return; }
            if (lastRead < 0 || lastRead >= Tokens.Count) return;
            repo.SavePosition(_text.Id, lastRead + 1, TextParagraphs.ParagraphAt(_paras, Tokens[lastRead].U16Begin));
        }
    }

    private async Task AfterSavedAsync(double fraction, bool forceAdvance, bool finish)
    {
        if (finish || !IsParagraphMode) { FinishSession(); return; }
        if (fraction < 0.8 && !forceAdvance)
        {
            Status = $"Абзац сохранён ({fraction * 100:0}%). Прочитайте его ещё раз (Пробел) или нажмите «Дальше».";
            return;
        }
        if (ParaIndex >= _paras.Count - 1) { FinishSession(); return; }
        GoToParagraph(ParaIndex + 1);
        if (AutoAdvance) await StartRecordingAsync();
        else Status = $"Готово. Нажмите Пробел, чтобы читать абзац {ParaIndex + 1}.";
    }

    /// <summary>Whole session done: celebration payload for the reading page, then navigation.</summary>
    private void FinishSession()
    {
        if (_text == null) return;
        try
        {
            var repo = AppServices.Repo;
            double cov = repo.UpdateTextCoverage(_text.Id) ?? CoveragePct;
            var badges = repo.CheckAchievements();
            var xp = repo.GetXpInfo();
            if (cov >= 90) repo.ClearPosition(_text.Id);
            string title = cov >= 90 ? "Текст пройден!" : cov >= 50 ? "Хорошее начало!" : "Чтение сохранено";
            if (_paraRead.Count > 0 || badges.Count > 0)
                App.MainWindow.PendingCelebration = new CelebrationInfo(_text.Id, title, cov, _sessionXp, badges, xp);
        }
        catch (Exception ex) { Diagnostics.LogException("finish session", ex); }
        Queued?.Invoke(_text.Id);
    }

    [RelayCommand]
    public async Task NextAsync()
    {
        if (IsBusy || _text == null || !IsParagraphMode) return;
        if (IsRecording) { await StopAndAssessAsync(forceAdvance: true); return; }
        if (ParaIndex < _paras.Count - 1) { GoToParagraph(ParaIndex + 1); Status = "Нажмите Пробел, чтобы читать этот абзац."; }
        else FinishSession();
    }

    [RelayCommand]
    public void Prev()
    {
        if (IsBusy || IsRecording || _text == null || !IsParagraphMode || ParaIndex == 0) return;
        GoToParagraph(ParaIndex - 1);
        Status = "Нажмите Пробел, чтобы читать этот абзац.";
    }

    [RelayCommand]
    public async Task FinishAsync()
    {
        if (IsBusy || _text == null) return;
        if (IsRecording) await StopAndAssessAsync(finish: true);
        else FinishSession();
    }

    [RelayCommand]
    public async Task ReplayReferenceAsync()
    {
        if (_text == null || IsRecording) return;
        if (!await Speech.SpeakAsync(ReferenceText.Replace("\n\n", "\n"), AppServices.Settings.Speed))
            Status = "Синтез речи недоступен.";
    }
}
