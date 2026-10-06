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
            Diagnostics.LogException("Application", e.Exception);
            e.Handled = true;
        };
    }

    protected override void OnLaunched(LaunchActivatedEventArgs args)
    {
        try
        {
            Diagnostics.Log("OnLaunched");
            Directory.CreateDirectory(AppPaths.UserDir);
            AppServices.Init();
            Diagnostics.Log("db opened (content available: " + AppServices.Repo.ContentAvailable + ", " + AppServices.Repo.ContentError + ")");
            MainWindow = new MainWindow();
            MainWindow.Activate();
            MainWindow.ApplyTheme(AppServices.Settings.Theme);
            Diagnostics.Log("window created");
        }
        catch (Exception ex)
        {
            Diagnostics.LogException("OnLaunched", ex);
            Diagnostics.ShowFatal("Ошибка запуска: " + ex.Message);
            Environment.Exit(1);
        }
    }
}
