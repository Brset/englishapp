using EnglishApp.Services;
using Microsoft.UI.Xaml;

namespace EnglishApp;

public partial class App : Application
{
    public static MainWindow MainWindow { get; private set; } = null!;

    public App()
    {
        InitializeComponent();
        UnhandledException += (_, e) =>
        {
            try { File.AppendAllText(Path.Combine(AppPaths.UserDir, "crash.log"), e.Exception + Environment.NewLine); } catch { }
        };
    }

    protected override void OnLaunched(LaunchActivatedEventArgs args)
    {
        Directory.CreateDirectory(AppPaths.UserDir);
        AppServices.Tts = new WinRtTtsEngine();   // temporary; piper later
        AppServices.Init();
        MainWindow = new MainWindow();
        MainWindow.Activate();
        MainWindow.ApplyTheme(AppServices.Settings.Theme);
    }
}
