using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using EnglishApp.Models;
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

    /// <summary>Raised on the UI thread when an assessment is ready.</summary>
    public event Action<ReviewArgs>? Completed;

    public RecordViewModel()
    {
        AppServices.Recorder.LevelChanged += OnLevel;
        AppServices.Recorder.Failed += OnFailed;
    }

    public void Detach()
    {
        AppServices.Recorder.LevelChanged -= OnLevel;
        AppServices.Recorder.Failed -= OnFailed;
        _timer?.Stop();
        if (AppServices.Recorder.IsRecording) _ = AppServices.Recorder.StopAsync();
    }

    private void OnLevel(float v) => _ui.TryEnqueue(() => Level = v * 100);
    private void OnFailed(Exception ex) => _ui.TryEnqueue(() => { Status = "Ошибка записи: " + ex.Message; IsRecording = false; });

    public void Load(string? textId)
    {
        _text = string.IsNullOrEmpty(textId) ? null : AppServices.Repo.GetText(textId);
        if (_text == null) return;
        Title = _text.TitleEn;
        ReferenceText = _text.Body;
        OnPropertyChanged(nameof(HasText));
    }

    [RelayCommand]
    public async Task ToggleAsync()
    {
        if (IsBusy || _text == null) return;
        if (!IsRecording) StartRecording(); else await StopAndAssessAsync();
    }

    private void StartRecording()
    {
        AppServices.Player.Stop();
        var path = Path.Combine(AppPaths.RecordingsDir, $"{_text!.Id}_{DateTime.Now:yyyyMMdd_HHmmss}.wav");
        try { AppServices.Recorder.Start(path, AppServices.Settings.MicDevice); }
        catch (Exception ex) { Status = "Не удалось открыть микрофон: " + ex.Message; return; }
        IsRecording = true;
        ButtonText = "Остановить (Пробел)";
        Status = "Идёт запись… Читайте текст вслух.";
        _clock.Restart();
        _timer = _ui.CreateTimer();
        _timer.Interval = TimeSpan.FromMilliseconds(250);
        _timer.Tick += (_, _) => Elapsed = $"{(int)_clock.Elapsed.TotalMinutes}:{_clock.Elapsed.Seconds:00}";
        _timer.Start();
    }

    private async Task StopAndAssessAsync()
    {
        _timer?.Stop();
        IsBusy = true; IsRecording = false;
        ButtonText = "Обработка…";
        Status = "Анализ произношения…";
        var seconds = _clock.Elapsed.TotalSeconds;
        try
        {
            var wav = await AppServices.Recorder.StopAsync();
            if (wav == null) return;
            var json = await AppServices.Engine.AssessWavAsync(wav, _text!.Body, AppServices.Settings.Strictness);
            var result = Native.PronAssessor.ParseResult(json);
            AppServices.Repo.SaveAttempt(_text.Id, wav, (int)(seconds * 1000), result.Scores.Overall, json, seconds / 60.0);
            AppServices.Repo.RecordPhonemes(result);
            string? note = AppServices.Engine.Status is { Asr: false }
                ? "Модель распознавания речи (whisper) не загружена: слова не распознаны." : null;
            Status = "Готово.";
            Completed?.Invoke(new ReviewArgs(_text.Id, _text.Body, json, result, note));
        }
        catch (Exception ex)
        {
            Status = "Ошибка анализа: " + ex.Message;
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
