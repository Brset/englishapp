using EnglishApp.Native;

namespace EnglishApp.Services;

public sealed record SpeakResult(double Score, string Recognized, double Coverage, AssessmentResult? Result, string? Error);

/// <summary>Short "say it" interactions for the drills: record one utterance, assess it immediately.</summary>
public static class QuickSpeech
{
    private const float VoiceLevel = 0.1f;

    private static CancellationTokenSource? _current;

    /// <summary>Aborts a running RecordAsync (call when leaving a drill page so the recorder is freed).</summary>
    public static void CancelCurrent() { try { _current?.Cancel(); } catch (Exception) { } }

    /// <summary>Records until a pause after speech (or maxMs); returns the WAV path, or null when nothing was recorded.</summary>
    public static async Task<string?> RecordAsync(int maxMs = 7000, int silenceMs = 900, CancellationToken ct = default)
    {
        var rec = AppServices.Recorder;
        if (rec.IsRecording) return null;
        using var cts = CancellationTokenSource.CreateLinkedTokenSource(ct);
        _current = cts;
        ct = cts.Token;
        AppServices.Player.Stop();
        var path = Path.Combine(Path.GetTempPath(), "pron_quick_" + Guid.NewGuid().ToString("N") + ".wav");
        var tcs = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        long start = Environment.TickCount64;
        long lastVoice = 0;
        bool voice = false;
        void OnLevel(float v)
        {
            long now = Environment.TickCount64;
            if (v > VoiceLevel) { voice = true; Interlocked.Exchange(ref lastVoice, now); }
            else if (voice && now - Interlocked.Read(ref lastVoice) >= silenceMs) tcs.TrySetResult();
            if (now - start >= maxMs) tcs.TrySetResult();
        }
        using var reg = ct.Register(() => tcs.TrySetResult());
        rec.LevelChanged += OnLevel;
        try
        {
            rec.Start(path, AppServices.Settings.MicDevice);
            await Task.WhenAny(tcs.Task, Task.Delay(maxMs + 2000));
        }
        catch (Exception)
        {
            if (rec.IsRecording) await rec.StopAsync();
            throw;
        }
        finally { rec.LevelChanged -= OnLevel; }
        var wav = await rec.StopAsync();
        if (ct.IsCancellationRequested)
        {
            if (wav != null) { try { File.Delete(wav); } catch (Exception) { } }
            return null;
        }
        return wav;
    }

    /// <summary>Assesses the recording against the reference text (instant, non-queued) and deletes the temp file.</summary>
    public static async Task<SpeakResult> ScoreAsync(string wavPath, string reference)
    {
        try
        {
            var json = await AppServices.Engine.AssessWavAsync(wavPath, reference, AppServices.Settings.Strictness);
            var r = PronAssessor.ParseResult(json);
            int total = r.Words.Count;
            int read = r.Words.Count(w => !(w.Status is "omitted" or "missing" || string.IsNullOrEmpty(w.Recognized)));
            double score = total == 1 ? r.Words[0].Score : r.Scores.Overall;
            var heard = string.Join(" ", r.Words.Where(w => !string.IsNullOrEmpty(w.Recognized)).Select(w => w.Recognized));
            try { AppServices.Repo.RecordPhonemes(r); }
            catch (Exception ex) { Diagnostics.LogException("drill phonemes", ex); }
            return new SpeakResult(score, heard, total > 0 ? 100.0 * read / total : 0, r, null);
        }
        catch (Exception ex)
        {
            Diagnostics.LogException("quick score", ex);
            return new SpeakResult(0, "", 0, null, ex.Message);
        }
        finally
        {
            try { File.Delete(wavPath); } catch (Exception) { /* temp file */ }
        }
    }
}
