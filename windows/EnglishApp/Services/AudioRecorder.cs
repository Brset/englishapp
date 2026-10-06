using NAudio.Wave;

namespace EnglishApp.Services;

/// <summary>Records 16 kHz mono 16-bit PCM WAV via NAudio WaveInEvent and reports an input level.</summary>
public sealed class AudioRecorder : IDisposable
{
    public static readonly WaveFormat Format = new(16000, 16, 1);

    private WaveInEvent? _in;
    private WaveFileWriter? _writer;
    private TaskCompletionSource? _stopped;

    public bool IsRecording { get; private set; }
    public string? FilePath { get; private set; }
    public TimeSpan Duration => _writer == null ? TimeSpan.Zero : TimeSpan.FromSeconds((double)_writer.Length / Format.AverageBytesPerSecond);

    /// <summary>Input level 0..1 (RMS, slightly boosted), raised on a NAudio thread.</summary>
    public event Action<float>? LevelChanged;
    public event Action<Exception>? Failed;

    public static IReadOnlyList<string> GetDevices()
    {
        var list = new List<string>();
        for (int i = 0; i < WaveInEvent.DeviceCount; i++) list.Add(WaveInEvent.GetCapabilities(i).ProductName);
        return list;
    }

    public void Start(string filePath, int deviceNumber = 0)
    {
        if (IsRecording) return;
        Directory.CreateDirectory(Path.GetDirectoryName(filePath)!);
        FilePath = filePath;
        _writer = new WaveFileWriter(filePath, Format);
        _stopped = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        _in = new WaveInEvent { WaveFormat = Format, DeviceNumber = deviceNumber, BufferMilliseconds = 50 };
        _in.DataAvailable += OnData;
        _in.RecordingStopped += OnStopped;
        try { _in.StartRecording(); }
        catch { Cleanup(); throw; }
        IsRecording = true;
    }

    private void OnData(object? sender, WaveInEventArgs e)
    {
        try { _writer?.Write(e.Buffer, 0, e.BytesRecorded); }
        catch (Exception ex) { Failed?.Invoke(ex); return; }
        long sum = 0; int n = e.BytesRecorded / 2;
        for (int i = 0; i + 1 < e.BytesRecorded; i += 2)
        {
            short s = (short)(e.Buffer[i] | (e.Buffer[i + 1] << 8));
            sum += (long)s * s;
        }
        double rms = n == 0 ? 0 : Math.Sqrt((double)sum / n) / 32768.0;
        LevelChanged?.Invoke((float)Math.Min(1.0, rms * 4));
    }

    private void OnStopped(object? sender, StoppedEventArgs e)
    {
        if (e.Exception != null) Failed?.Invoke(e.Exception);
        _stopped?.TrySetResult();
    }

    /// <summary>Stops and finalizes the WAV; returns its path.</summary>
    public async Task<string?> StopAsync()
    {
        if (!IsRecording || _in == null) return null;
        IsRecording = false;
        _in.StopRecording();
        if (_stopped != null) await Task.WhenAny(_stopped.Task, Task.Delay(2000));
        var path = FilePath;
        Cleanup();
        LevelChanged?.Invoke(0);
        return path;
    }

    private void Cleanup()
    {
        if (_in != null) { _in.DataAvailable -= OnData; _in.RecordingStopped -= OnStopped; _in.Dispose(); _in = null; }
        _writer?.Dispose(); _writer = null;
    }

    public void Dispose() => Cleanup();
}
