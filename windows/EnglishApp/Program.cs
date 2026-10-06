using EnglishApp.Services;
using Microsoft.UI.Dispatching;
using Microsoft.UI.Xaml;

namespace EnglishApp;

public static class Program
{
    [STAThread]
    public static int Main(string[] args)
    {
        Diagnostics.InstallGlobalHandlers();
        Diagnostics.Log("start. " + Diagnostics.SystemInfo());
        try
        {
            var problem = Diagnostics.CheckInstallation();
            if (problem != null) { Diagnostics.ShowFatal(problem); return 2; }

            WinRT.ComWrappersSupport.InitializeComWrappers();
            Application.Start(_ =>
            {
                var ctx = new DispatcherQueueSynchronizationContext(DispatcherQueue.GetForCurrentThread());
                SynchronizationContext.SetSynchronizationContext(ctx);
                new App();
            });
            Diagnostics.Log("exit normally");
            return 0;
        }
        catch (Exception ex)
        {
            Diagnostics.LogException("Main", ex);
            Diagnostics.ShowFatal("Не удалось запустить приложение: " + ex.Message);
            return 1;
        }
    }
}
