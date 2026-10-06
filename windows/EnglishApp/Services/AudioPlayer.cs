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
