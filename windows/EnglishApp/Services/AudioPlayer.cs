using NAudio.Wave;

namespace EnglishApp.Services;

/// <summary>Plays WAV files / in-memory WAV bytes. A new Play call stops the previous one.</summary>
public sealed class AudioPlayer : IDisposable
{
    private WaveOutEvent? _out;
    private WaveStream? _reader;

    public bool IsPlaying => _out?.PlaybackState == PlaybackState.Playing;
    public event Action? PlaybackFinished;

    public void PlayFile(string path) => Play(new AudioFileReader(path));

    public void PlayWavBytes(byte[] wav) => Play(new WaveFileReader(new MemoryStream(wav)));

    /// <summary>Plays [startSec, endSec] of a WAV file (with a small margin). False when the span is empty/unreadable.</summary>
    public bool PlayFileSegment(string path, double startSec, double endSec)
    {
        byte[] wav;
        using (var r = new WaveFileReader(path))
        {
            var wf = r.WaveFormat;
            int ba = Math.Max(1, wf.BlockAlign);
            long total = r.Length;
            long from = Math.Clamp((long)(Math.Max(0, startSec - 0.08) * wf.AverageBytesPerSecond) / ba * ba, 0, total);
            long to = Math.Clamp((long)((endSec + 0.08) * wf.AverageBytesPerSecond) / ba * ba, from, total);
            if (to - from <= 0) return false;
            r.Position = from;
            var buf = new byte[to - from];
            int got = 0, n;
            while (got < buf.Length && (n = r.Read(buf, got, buf.Length - got)) > 0) got += n;
            using var ms = new MemoryStream();
            using (var w = new WaveFileWriter(ms, wf)) w.Write(buf, 0, got);
            wav = ms.ToArray();
        }
        PlayWavBytes(wav);
        return true;
    }

    private void Play(WaveStream reader)
    {
        Stop();
        _reader = reader;
        _out = new WaveOutEvent();
        _out.PlaybackStopped += OnStopped;
        _out.Init(reader);
        _out.Play();
    }

    private void OnStopped(object? sender, StoppedEventArgs e)
    {
        if (!ReferenceEquals(sender, _out)) return;
        Release();
        PlaybackFinished?.Invoke();
    }

    public void Stop()
    {
        var o = _out;
        if (o == null) return;
        o.PlaybackStopped -= OnStopped;
        o.Stop();
        Release();
    }

    private void Release()
    {
        _out?.Dispose(); _out = null;
        _reader?.Dispose(); _reader = null;
    }

    public void Dispose() => Stop();
}
