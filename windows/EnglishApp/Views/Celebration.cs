using EnglishApp.Models;
using EnglishApp.Services;
using Microsoft.UI.Dispatching;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;

namespace EnglishApp.Views;

/// <summary>"Text finished" overlay: coverage ring, XP gained, level progress and new badges.</summary>
public static class Celebration
{
    public static async Task ShowAsync(XamlRoot root, CelebrationInfo info)
    {
        var panel = new StackPanel { Spacing = 12, MinWidth = 340 };

        var ringHost = new Grid { Width = 120, Height = 120, HorizontalAlignment = HorizontalAlignment.Center };
        var ring = new ProgressRing
        {
            IsActive = true, IsIndeterminate = false, Minimum = 0, Maximum = 100, Value = 0, Width = 120, Height = 120,
            Foreground = info.Coverage >= 90 ? Ui.Green : Ui.Amber,
        };
        var label = new TextBlock
        {
            Text = "0%", FontSize = 28, FontWeight = Microsoft.UI.Text.FontWeights.SemiBold,
            HorizontalAlignment = HorizontalAlignment.Center, VerticalAlignment = VerticalAlignment.Center,
        };
        ringHost.Children.Add(ring);
        ringHost.Children.Add(label);
        panel.Children.Add(ringHost);
        panel.Children.Add(new TextBlock
        {
            Text = "Прочитано вслух", HorizontalAlignment = HorizontalAlignment.Center, Opacity = 0.7,
        });

        panel.Children.Add(new TextBlock
        {
            Text = info.XpGained > 0 ? $"+{info.XpGained} XP" : "XP за слова уже получены",
            FontSize = 22, FontWeight = Microsoft.UI.Text.FontWeights.SemiBold, Foreground = Ui.Amber,
            HorizontalAlignment = HorizontalAlignment.Center,
        });
        panel.Children.Add(new ProgressBar { Minimum = 0, Maximum = 100, Value = info.Xp.Fraction * 100 });
        panel.Children.Add(new TextBlock { Text = "Уровень: " + info.Xp.Line, HorizontalAlignment = HorizontalAlignment.Center, Opacity = 0.8 });
        panel.Children.Add(new TextBlock
        {
            Text = "Оценка произношения появится, когда закончится фоновая обработка.",
            TextWrapping = TextWrapping.Wrap, Opacity = 0.7,
        });

        if (info.NewBadges.Count > 0)
        {
            panel.Children.Add(new TextBlock { Text = "Новые достижения", FontWeight = Microsoft.UI.Text.FontWeights.SemiBold, Margin = new Thickness(0, 6, 0, 0) });
            foreach (var b in info.NewBadges)
                panel.Children.Add(new TextBlock { Text = $"★ {b.Title} — {b.Description}", TextWrapping = TextWrapping.Wrap });
        }

        var timer = DispatcherQueue.GetForCurrentThread().CreateTimer();
        timer.Interval = TimeSpan.FromMilliseconds(20);
        double cur = 0;
        timer.Tick += (_, _) =>
        {
            cur += Math.Max(1.0, info.Coverage / 40.0);
            if (cur >= info.Coverage) { cur = info.Coverage; timer.Stop(); }
            ring.Value = cur;
            label.Text = $"{cur:0}%";
        };
        timer.Start();

        var dlg = new ContentDialog
        {
            XamlRoot = root, Title = info.Title, Content = panel, CloseButtonText = "Отлично",
            DefaultButton = ContentDialogButton.Close,
        };
        try { await dlg.ShowAsync(); }
        finally { timer.Stop(); }
    }
}
