using System.Collections.ObjectModel;
using CommunityToolkit.Mvvm.ComponentModel;
using EnglishApp.Models;
using EnglishApp.Native;
using Microsoft.UI.Dispatching;

namespace EnglishApp.Services;

/// <summary>One queued / running assessment, as shown in the processing panel.</summary>
public sealed partial class JobItem : ObservableObject
{
    public JobItem(long id, string textId, string title, double audioSeconds)
    {
        Id = id; TextId = textId; Title = title; AudioSeconds = audioSeconds;
    }

    public long Id { get; }
    public string TextId { get; }
    public string Title { get; }
    public double AudioSeconds { get; }

    [ObservableProperty] private bool isRunning;
    [ObservableProperty] private double percent;
    [ObservableProperty] private string detail = "в очереди";
}

/// <summary>
/// Persistent background queue (table processing_jobs): one heavy assessment at a time on a worker thread.
/// Create it on the UI thread: all collection / event traffic is marshalled back through its DispatcherQueue.
/// </summary>
public sealed class ProcessingQueue
{
    private readonly ContentRepository _repo;
    private readonly DispatcherQueue _ui = DispatcherQueue.GetForCurrentThread();
    private readonly SemaphoreSlim _signal = new(0);
    private long _runningId;
    private volatile bool _cancelRunning;
    private long _lastDbTick;

    /// <summary>Queued and running jobs, oldest first (modified on the UI thread only).</summary>
    public ObservableCollection<JobItem> Jobs { get; } = new();

    /// <summary>UI thread: a job left the queue (textId, success). Also raised for cancelled / failed jobs.</summary>
    public event Action<string, bool>? JobFinished;

    public ProcessingQueue(ContentRepository repo)
    {
        _repo = repo;
        _repo.RequeueInterruptedJobs();
        foreach (var j in _repo.GetActiveJobs())
            Jobs.Add(new JobItem(j.Id, j.TextId, TitleOf(j.TextId, j.ParagraphIndex, j.ParagraphTotal), j.AudioSeconds));
        RefreshEstimates();
        Task.Run(WorkerLoop);
        if (Jobs.Count > 0) _signal.Release();
    }

    private string TitleOf(string textId, int? paragraph = null, int paragraphTotal = 0)
    {
        var title = _repo.GetTextSummary(textId)?.TitleEn ?? textId;
        return paragraph is int p && paragraphTotal > 0 ? $"{title} · Абзац {p + 1}/{paragraphTotal}" : title;
    }

    /// <summary>UI thread: badges unlocked by a finished assessment.</summary>
    public event Action<IReadOnlyList<Achievement>>? BadgesUnlocked;

    public static string Fmt(double seconds)
    {
        int s = (int)Math.Max(0, Math.Ceiling(seconds));
        return $"{s / 60}:{s % 60:00}";
    }

    /// <summary>UI thread. Stores the reading, marks the text read and queues the assessment.</summary>
    public long Enqueue(string textId, string wavPath, double audioSeconds, string? liveMarks,
        int? paragraphIndex = null, int paragraphTotal = 0)
    {
        var id = _repo.AddPendingReading(textId, wavPath, audioSeconds, liveMarks, paragraphIndex, paragraphTotal);
        Jobs.Add(new JobItem(id, textId, TitleOf(textId, paragraphIndex, paragraphTotal), audioSeconds));
        RefreshEstimates();
        _signal.Release();
        return id;
    }

    /// <summary>UI thread. Cancels a running job (aborts the engine call) or removes a queued one.</summary>
    public void Cancel(long id)
    {
        if (Interlocked.Read(ref _runningId) == id)
        {
            _cancelRunning = true;
            AppServices.Engine.CancelAssess();
            var running = Jobs.FirstOrDefault(j => j.Id == id);
            if (running != null) running.Detail = "отмена…";
            return;
        }
        var item = Jobs.FirstOrDefault(j => j.Id == id);
        if (item == null) return;
        if (_repo.EndJob(id, "cancelled", null))
        {
            Jobs.Remove(item);
            RefreshEstimates();
            JobFinished?.Invoke(item.TextId, false);
        }
    }

    private void Post(Action a) => _ui.TryEnqueue(() =>
    {
        try { a(); } catch (Exception ex) { Diagnostics.LogException("queue ui", ex); }
    });

    /// <summary>UI thread: "≈ m:ss" for every waiting job (engine estimate, summed over the jobs ahead of it).</summary>
    private void RefreshEstimates()
    {
        double ahead = 0;
        bool known = true;
        foreach (var j in Jobs)
        {
            if (j.IsRunning) continue;
            var est = AppServices.Engine.EstimateSeconds(j.AudioSeconds);
            if (est < 0) known = false; else ahead += est;
            j.Detail = known ? "в очереди · ≈ " + Fmt(ahead) : "в очереди";
        }
    }

    private async Task WorkerLoop()
    {
        while (true)
        {
            await _signal.WaitAsync();
            while (true)
            {
                JobRow? job;
                try { job = _repo.NextQueuedJob(); }
                catch (Exception ex) { Diagnostics.LogException("queue next", ex); break; }
                if (job == null) break;
                await RunJob(job);
            }
        }
    }

    private async Task RunJob(JobRow job)
    {
        bool ok = false;
        IReadOnlyList<Achievement>? unlocked = null;
        try
        {
            if (!_repo.StartJob(job.Id)) return;   // cancelled while waiting
            _cancelRunning = false;
            Interlocked.Exchange(ref _runningId, job.Id);
            Interlocked.Exchange(ref _lastDbTick, 0);
            Post(() =>
            {
                var item = Jobs.FirstOrDefault(j => j.Id == job.Id);
                if (item != null) { item.IsRunning = true; item.Percent = 0; item.Detail = "оценка времени…"; }
                RefreshEstimates();
            });

            string status = "done";
            string? error = null;
            try
            {
                if (!File.Exists(job.WavPath)) throw new FileNotFoundException("Запись не найдена", job.WavPath);
                var text = _repo.GetText(job.TextId) ?? throw new InvalidOperationException("Текст не найден в библиотеке");
                string reference = text.Body;
                if (job.ParagraphIndex is int pi)
                {
                    var paras = TextParagraphs.Split(text.Body);
                    if (pi >= 0 && pi < paras.Count) reference = paras[pi].Text;
                }
                var json = await AppServices.Engine.AssessWavProgressAsync(job.WavPath, reference,
                    AppServices.Settings.Strictness, (stage, fraction, eta) => OnProgress(job.Id, fraction, eta));
                if (_cancelRunning) throw new PronCancelledException();
                var result = PronAssessor.ParseResult(json);
                int total = result.Words.Count;
                int read = result.Words.Count(w => !(w.Status is "omitted" or "missing" || string.IsNullOrEmpty(w.Recognized)));
                bool missing = _repo.CompleteJob(job.Id, job.TextId, job.WavPath, json, result.Scores.Overall,
                    read, total, result.Fluency.WordsPerMinute, total - read);
                try { _repo.RecordPhonemes(result); }
                catch (Exception ex) { Diagnostics.LogException("record phonemes", ex); }
                try { unlocked = _repo.AfterScored(job.TextId, result, result.Scores.Overall, read, missing); }
                catch (Exception ex) { Diagnostics.LogException("after scored", ex); }
            }
            catch (PronCancelledException) { status = "cancelled"; }
            catch (Exception ex)
            {
                Diagnostics.LogException("job " + job.Id, ex);
                status = "failed";
                error = ex.Message;
            }
            if (status != "done") _repo.EndJob(job.Id, status, error);
            ok = status == "done";
        }
        catch (Exception ex)
        {
            Diagnostics.LogException("job run", ex);
            try { _repo.EndJob(job.Id, "failed", ex.Message); } catch (Exception) { }
        }
        finally
        {
            Interlocked.Exchange(ref _runningId, 0);
            _cancelRunning = false;
            Post(() =>
            {
                var item = Jobs.FirstOrDefault(j => j.Id == job.Id);
                if (item != null) Jobs.Remove(item);
                RefreshEstimates();
                JobFinished?.Invoke(job.TextId, ok);
                if (unlocked is { Count: > 0 }) BadgesUnlocked?.Invoke(unlocked);
            });
        }
    }

    /// <summary>Worker thread (engine callback).</summary>
    private void OnProgress(long jobId, double fraction, double eta)
    {
        fraction = Math.Clamp(fraction, 0, 1);
        var now = Environment.TickCount64;
        if (now - Interlocked.Read(ref _lastDbTick) >= 1000)
        {
            Interlocked.Exchange(ref _lastDbTick, now);
            try { _repo.UpdateJobProgress(jobId, fraction, eta); } catch (Exception) { }
        }
        Post(() =>
        {
            var item = Jobs.FirstOrDefault(j => j.Id == jobId);
            if (item == null || _cancelRunning) return;
            item.Percent = fraction * 100;
            item.Detail = eta < 0 ? $"{fraction * 100:0}% · оценка времени…" : $"{fraction * 100:0}% · осталось {Fmt(eta)}";
        });
    }
}
