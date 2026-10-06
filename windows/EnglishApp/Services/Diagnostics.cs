using System.Runtime.InteropServices;
using System.Text;

namespace EnglishApp.Services;

/// <summary>File logging + native message box. Must never throw and must not depend on WinUI.</summary>
public static class Diagnostics
{
    private static readonly object Lock = new();
    public static string LogPath { get; } = Path.Combine(
        Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "EnglishApp", "logs", "app.log");
    private static string ExeLogPath => Path.Combine(AppContext.BaseDirectory, "EnglishApp.log");

    [DllImport("user32.dll", CharSet = CharSet.Unicode, EntryPoint = "MessageBoxW")]
    private static extern int MessageBoxW(IntPtr hWnd, string text, string caption, uint type);

    public static void Log(string message)
    {
        var line = $"{DateTime.Now:yyyy-MM-dd HH:mm:ss.fff} [{Environment.CurrentManagedThreadId}] {message}{Environment.NewLine}";
        lock (Lock)
        {
            try { Directory.CreateDirectory(Path.GetDirectoryName(LogPath)!); File.AppendAllText(LogPath, line); } catch { }
            try { File.AppendAllText(ExeLogPath, line); } catch { }
        }
    }

    public static void LogException(string source, object? ex)
    {
        var sb = new StringBuilder();
        sb.AppendLine($"UNHANDLED ({source})");
        for (var e = ex as Exception; e != null; e = e.InnerException)
            sb.AppendLine(e.GetType().FullName + ": " + e.Message + Environment.NewLine + e.StackTrace);
        if (ex is not Exception) sb.AppendLine(ex?.ToString());
        sb.Append(SystemInfo());
        Log(sb.ToString());
    }

    public static string SystemInfo() =>
        $"OS: {RuntimeInformation.OSDescription} ({Environment.OSVersion.Version}), arch {RuntimeInformation.OSArchitecture}/{RuntimeInformation.ProcessArchitecture}, " +
        $"runtime {RuntimeInformation.FrameworkDescription}, 64bit={Environment.Is64BitProcess}, exe dir {AppContext.BaseDirectory}";

    public static void InstallGlobalHandlers()
    {
        AppDomain.CurrentDomain.UnhandledException += (_, e) => LogException("AppDomain", e.ExceptionObject);
        TaskScheduler.UnobservedTaskException += (_, e) => { LogException("UnobservedTask", e.Exception); e.SetObserved(); };
    }

    public static void ShowFatal(string message)
    {
        Log("FATAL: " + message);
        try { MessageBoxW(IntPtr.Zero, message + "\n\nЖурнал: " + LogPath, "English Pronunciation", 0x10 /*MB_ICONERROR*/); } catch { }
    }

    /// <summary>Returns a Russian error when the app seems to run from inside an archive or with missing files; null if OK.</summary>
    public static string? CheckInstallation()
    {
        var dir = AppContext.BaseDirectory;
        var temp = Path.GetTempPath();
        // Only real temp locations (zip preview / archiver extraction), never Downloads/Desktop.
        bool inTemp = (temp.Length > 3 && temp.Contains("temp", StringComparison.OrdinalIgnoreCase)
                       && dir.StartsWith(temp, StringComparison.OrdinalIgnoreCase))
            || dir.Contains(@"\AppData\Local\Temp\", StringComparison.OrdinalIgnoreCase);
        bool missing = !File.Exists(Path.Combine(dir, "content.db")) || !File.Exists(Path.Combine(dir, "pron_engine.dll"))
            || !Directory.Exists(Path.Combine(dir, "models"));
        if (inTemp || missing)
            return "Распакуйте архив целиком и запустите EnglishApp.exe из распакованной папки.";
        return null;
    }
}
