using EnglishApp.Services;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;

namespace EnglishApp.Views;

/// <summary>First launch: what the app does, microphone check, level / daily goal / accent.</summary>
public static class OnboardingDialog
{
    private const string Passage = "The weather is nice today. I usually walk to work and drink a cup of coffee before the meeting.";
    private static readonly string[] Levels = { "A1", "A2", "B1", "B2", "C1", "C2" };
    private static readonly int[] Goals = { 5, 10, 15, 20, 30 };

    private static TextBlock Text(string s, double size = 14, bool bold = false) => new()
    {
        Text = s, FontSize = size, TextWrapping = TextWrapping.Wrap,
        FontWeight = bold ? Microsoft.UI.Text.FontWeights.SemiBold : Microsoft.UI.Text.FontWeights.Normal,
    };

    public static async Task ShowAsync(XamlRoot root)
    {
        // ---- step 1: welcome ----
        var welcome = new StackPanel { Spacing = 12, Width = 440 };
        welcome.Children.Add(Text("Читайте тексты вслух и получайте точную оценку произношения.", 18, true));
        welcome.Children.Add(Text("Приложение подсвечивает слова прямо во время чтения, показывает, сколько текста вы прочитали, " +
                                  "и разбирает ошибки по звукам. Всё работает без интернета."));
        welcome.Children.Add(Text("Длинные тексты читаются по абзацам, а слабые звуки и слова можно потренировать отдельно."));

        // ---- step 2: microphone ----
        var mic = new StackPanel { Spacing = 12, Width = 440 };
        mic.Children.Add(Text("Для чтения вслух нужен микрофон.", 18, true));
        mic.Children.Add(Text("Разрешите доступ в настройках Windows (Конфиденциальность → Микрофон) и проверьте, что запись работает."));
        var micStatus = Text("");
        var micBtn = new Button { Content = "Проверить микрофон" };
        var privacyBtn = new Button { Content = "Открыть настройки конфиденциальности" };
        micBtn.Click += async (_, _) =>
        {
            micBtn.IsEnabled = false;
            try
            {
                if (AudioRecorder.GetDevices().Count == 0) { micStatus.Text = "Микрофон не найден. Подключите его и повторите проверку."; return; }
                if (AppServices.Recorder.IsRecording) return;
                micStatus.Text = "Скажите что-нибудь…";
                float peak = 0;
                Action<float> onLevel = v => peak = Math.Max(peak, v);
                var path = Path.Combine(Path.GetTempPath(), "pron_mic_test.wav");
                AppServices.Recorder.LevelChanged += onLevel;
                try
                {
                    AppServices.Recorder.Start(path, AppServices.Settings.MicDevice);
                    await Task.Delay(2000);
                    await AppServices.Recorder.StopAsync();
                }
                finally { AppServices.Recorder.LevelChanged -= onLevel; }
                try { File.Delete(path); } catch (Exception) { /* temp file */ }
                micStatus.Text = peak > 0.05f ? "Микрофон работает." : "Звука почти нет. Проверьте выбранный микрофон и разрешения.";
            }
            catch (Exception ex) { micStatus.Text = "Нет доступа к микрофону: " + ex.Message; }
            finally { micBtn.IsEnabled = true; }
        };
        privacyBtn.Click += async (_, _) =>
        {
            try { await Windows.System.Launcher.LaunchUriAsync(new Uri("ms-settings:privacy-microphone")); }
            catch (Exception ex) { Diagnostics.LogException("privacy settings", ex); }
        };
        mic.Children.Add(micBtn);
        mic.Children.Add(micStatus);
        mic.Children.Add(privacyBtn);

        // ---- step 3: level, goal, accent ----
        var prefs = new StackPanel { Spacing = 12, Width = 440 };
        var levelBox = new ComboBox { Header = "Ваш уровень", Width = 260, ItemsSource = Levels, SelectedIndex = 0 };
        var goalBox = new ComboBox { Header = "Цель на день", Width = 260, ItemsSource = Goals.Select(g => $"{g} минут").ToList(), SelectedIndex = 1 };
        var accentBox = new ComboBox
        {
            Header = "Акцент", Width = 260, SelectedIndex = AppServices.Settings.Accent == "UK" ? 1 : 0,
            ItemsSource = new[] { "US (американский)", "UK (британский)" },
        };
        var levelNote = Text("");
        var detect = new Button { Content = "Определить уровень" };
        detect.Click += async (_, _) =>
        {
            detect.IsEnabled = false;
            try
            {
                levelNote.Text = "Прочитайте вслух: " + Passage;
                var wav = await QuickSpeech.RecordAsync(15000, 1500);
                if (wav == null) { levelNote.Text = "Не удалось записать звук."; return; }
                levelNote.Text = "Оцениваем…";
                var res = await QuickSpeech.ScoreAsync(wav, Passage);
                if (res.Error != null) { levelNote.Text = "Не удалось оценить: " + res.Error; return; }
                int li = res.Score < 45 ? 0 : res.Score < 60 ? 1 : res.Score < 72 ? 2 : res.Score < 82 ? 3 : res.Score < 90 ? 4 : 5;
                levelBox.SelectedIndex = li;
                levelNote.Text = $"Оценка {res.Score:0}/100, предлагаем уровень {Levels[li]}. Его можно изменить.";
            }
            catch (Exception ex) { levelNote.Text = "Ошибка записи: " + ex.Message; }
            finally { detect.IsEnabled = true; }
        };
        prefs.Children.Add(Text("Настройте под себя", 18, true));
        prefs.Children.Add(levelBox);
        prefs.Children.Add(detect);
        prefs.Children.Add(levelNote);
        prefs.Children.Add(goalBox);
        prefs.Children.Add(accentBox);

        var titles = new[] { "Добро пожаловать", "Микрофон", "Уровень и цель" };
        var pages = new UIElement[] { welcome, mic, prefs };
        int step = 0;
        var dlg = new ContentDialog
        {
            XamlRoot = root, Title = titles[0], Content = pages[0],
            PrimaryButtonText = "Далее", SecondaryButtonText = "Назад", CloseButtonText = "Пропустить",
            DefaultButton = ContentDialogButton.Primary, IsSecondaryButtonEnabled = false,
        };
        void Apply()
        {
            dlg.Title = titles[step];
            dlg.Content = pages[step];
            dlg.PrimaryButtonText = step == pages.Length - 1 ? "Готово" : "Далее";
            dlg.IsSecondaryButtonEnabled = step > 0;
        }
        dlg.PrimaryButtonClick += (_, a) =>
        {
            if (step < pages.Length - 1) { a.Cancel = true; step++; Apply(); return; }
            var s = AppServices.Settings;
            s.Level = Levels[Math.Max(0, levelBox.SelectedIndex)];
            s.DailyGoal = Goals[Math.Max(0, goalBox.SelectedIndex)];
            s.Accent = accentBox.SelectedIndex == 1 ? "UK" : "US";
        };
        dlg.SecondaryButtonClick += (_, a) =>
        {
            a.Cancel = true;
            if (step > 0) { step--; Apply(); }
        };
        try { await dlg.ShowAsync(); }
        finally { AppServices.Settings.Onboarded = true; }
    }
}
