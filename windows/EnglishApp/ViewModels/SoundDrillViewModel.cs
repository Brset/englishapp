using System.Collections.ObjectModel;
using System.Text.Json;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using EnglishApp.Models;
using EnglishApp.Services;
using Microsoft.UI.Xaml.Media;

namespace EnglishApp.ViewModels;

public sealed record PairItem(string Target, string Contrast, string ContrastSound);

/// <summary>Sound drill: minimal pairs; the app says a word, the user repeats it and gets an instant score.</summary>
public partial class SoundDrillViewModel : ObservableObject
{
    private readonly Random _rng = new();
    private List<PairItem> _pairs = new();
    private int _idx;
    private string _word = "";
    private string _other = "";

    public ObservableCollection<SoundCard> Sounds { get; } = new();
    public ObservableCollection<string> Tips { get; } = new();

    [ObservableProperty] private SoundCard? selected;
    [ObservableProperty] private string header = "";
    [ObservableProperty] private string pairText = "";
    [ObservableProperty] private string wordText = "";
    [ObservableProperty] private string pairCounter = "";
    [ObservableProperty] private string status = "";
    [ObservableProperty] private bool isBusy;
    [ObservableProperty] private bool hasResult;
    [ObservableProperty] private bool hasPairs;
    [ObservableProperty] private string resultText = "";
    [ObservableProperty] private string heardText = "";
    [ObservableProperty] private Brush resultBrush = Ui.Gray;

    public bool NotBusy => !IsBusy;
    public bool NoPairs => !HasPairs;
    partial void OnIsBusyChanged(bool value) => OnPropertyChanged(nameof(NotBusy));
    partial void OnHasPairsChanged(bool value) => OnPropertyChanged(nameof(NoPairs));
    partial void OnSelectedChanged(SoundCard? value) => LoadCard(value);

    public void Load(string? key)
    {
        Sounds.Clear();
        foreach (var s in AppServices.Repo.GetSounds()) Sounds.Add(s);
        Selected = Find(key) ?? Sounds.FirstOrDefault();
        if (Selected == null) LoadCard(null);
    }

    private SoundCard? Find(string? key)
    {
        if (string.IsNullOrEmpty(key)) return null;
        return Sounds.FirstOrDefault(s => s.Ipa == key || s.Id == key || s.Slug == key)
            ?? Sounds.FirstOrDefault(s => s.Ipa.Split(' ', StringSplitOptions.RemoveEmptyEntries).Contains(key))
            ?? Sounds.FirstOrDefault(s => s.Ipa.Contains(key, StringComparison.Ordinal));
    }

    private void LoadCard(SoundCard? card)
    {
        Tips.Clear();
        _pairs = new List<PairItem>();
        HasResult = false; Status = ""; HeardText = "";
        if (card == null) { Header = "Звуки не найдены"; HasPairs = false; PairText = ""; WordText = ""; PairCounter = ""; return; }
        Header = card.Title;
        try
        {
            using var doc = JsonDocument.Parse(card.Json);
            var root = doc.RootElement;
            if (root.ValueKind == JsonValueKind.Object)
            {
                if (root.TryGetProperty("articulation_ru", out var art) && art.ValueKind == JsonValueKind.Array)
                    foreach (var t in art.EnumerateArray()) if (t.ValueKind == JsonValueKind.String) Tips.Add(t.GetString() ?? "");
                if (root.TryGetProperty("minimal_pairs", out var mp) && mp.ValueKind == JsonValueKind.Array)
                    foreach (var p in mp.EnumerateArray())
                    {
                        if (p.ValueKind != JsonValueKind.Object) continue;
                        var target = p.TryGetProperty("target", out var tg) && tg.ValueKind == JsonValueKind.String ? tg.GetString() : null;
                        var contrast = p.TryGetProperty("contrast", out var ct) && ct.ValueKind == JsonValueKind.String ? ct.GetString() : null;
                        var cs = p.TryGetProperty("contrast_sound", out var cso) && cso.ValueKind == JsonValueKind.String ? cso.GetString() : "";
                        if (!string.IsNullOrWhiteSpace(target) && !string.IsNullOrWhiteSpace(contrast))
                            _pairs.Add(new PairItem(target!, contrast!, cs ?? ""));
                    }
                if (_pairs.Count == 0 && root.TryGetProperty("example_words", out var exw) && exw.ValueKind == JsonValueKind.Array)
                    foreach (var w in exw.EnumerateArray())
                        if (w.ValueKind == JsonValueKind.Object && w.TryGetProperty("word", out var ww) && ww.ValueKind == JsonValueKind.String)
                            _pairs.Add(new PairItem(ww.GetString() ?? "", "", ""));
            }
        }
        catch (JsonException ex) { Diagnostics.LogException("sound card json", ex); }
        _pairs = _pairs.Where(p => p.Target.Length > 0).ToList();
        HasPairs = _pairs.Count > 0;
        _idx = 0;
        ShowPair();
    }

    private void ShowPair()
    {
        HasResult = false; HeardText = ""; Status = "";
        if (_pairs.Count == 0) { PairText = ""; WordText = ""; PairCounter = ""; return; }
        var p = _pairs[_idx % _pairs.Count];
        bool useContrast = p.Contrast.Length > 0 && _rng.Next(10) < 3;
        _word = useContrast ? p.Contrast : p.Target;
        _other = useContrast ? p.Target : p.Contrast;
        WordText = _word;
        PairText = p.Contrast.Length > 0 ? $"{p.Target}  ·  {p.Contrast}" + (p.ContrastSound.Length > 0 ? $"   (контраст: {p.ContrastSound})" : "") : "";
        PairCounter = $"Пара {_idx % _pairs.Count + 1} из {_pairs.Count}";
    }

    [RelayCommand]
    private async Task ListenAsync()
    {
        if (_word.Length == 0) return;
        Status = "";
        if (!await Speech.SpeakAsync(_word, 0.85)) Status = "Синтез речи недоступен.";
    }

    [RelayCommand]
    private async Task SayAsync()
    {
        if (_word.Length == 0 || IsBusy) return;
        IsBusy = true; HasResult = false; Status = "Скажите слово…";
        try
        {
            var wav = await QuickSpeech.RecordAsync(5000, 800);
            if (wav == null) { Status = "Не удалось записать звук."; return; }
            Status = "Оцениваем…";
            var res = await QuickSpeech.ScoreAsync(wav, _word);
            if (res.Error != null) { Status = "Не удалось оценить: " + res.Error; return; }
            ResultText = $"{res.Score:0}";
            ResultBrush = Ui.ScoreBrush(res.Score);
            if (string.IsNullOrWhiteSpace(res.Recognized)) HeardText = "Ничего не расслышал, попробуйте ещё раз.";
            else if (_other.Length > 0 && string.Equals(res.Recognized.Trim(), _other, StringComparison.OrdinalIgnoreCase))
                HeardText = $"Получилось «{_other}» вместо «{_word}». Перечитайте подсказки по артикуляции и повторите.";
            else HeardText = res.Score >= 80 ? $"Отлично! Вы сказали: {res.Recognized}" : $"Вы сказали: {res.Recognized}";
            HasResult = true; Status = "";
            AppServices.Repo.AwardXp(2, "drill-sound");
            AppServices.Repo.AddActivity(0.25, 0);
            var badges = AppServices.Repo.CheckAchievements();
            if (badges.Count > 0) App.MainWindow.ShowBadges(badges);
        }
        catch (Exception ex) { Status = "Ошибка записи: " + ex.Message; }
        finally { IsBusy = false; }
    }

    [RelayCommand]
    private void Next()
    {
        if (_pairs.Count == 0) return;
        _idx++;
        ShowPair();
    }
}
