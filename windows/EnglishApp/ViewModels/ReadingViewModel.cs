using System.Collections.ObjectModel;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using EnglishApp.Models;
using EnglishApp.Services;

namespace EnglishApp.ViewModels;

public partial class ReadingViewModel : ObservableObject
{
    private Dictionary<string, VocabEntry> _vocab = new(StringComparer.OrdinalIgnoreCase);

    [ObservableProperty] private TextDetail? text;
    [ObservableProperty] private double speed = AppServices.Settings.Speed;
    [ObservableProperty] private string status = "";
    public ObservableCollection<FocusSound> Focus { get; } = new();

    public string Accent => AppServices.Settings.Accent;
    public string SpeedLabel => $"Скорость: {Speed:0.00}×";
    partial void OnSpeedChanged(double value)
    {
        OnPropertyChanged(nameof(SpeedLabel));
        AppServices.Settings.Speed = value;
    }

    public bool Load(string? textId)
    {
        if (string.IsNullOrEmpty(textId)) return false;
        Text = AppServices.Repo.GetText(textId);
        if (Text == null) return false;
        AppServices.Repo.MarkOpened(textId);
        _vocab = AppServices.Repo.GetVocabulary(textId).GroupBy(v => v.Word, StringComparer.OrdinalIgnoreCase)
            .ToDictionary(g => g.Key, g => g.First(), StringComparer.OrdinalIgnoreCase);
        Focus.Clear();
        foreach (var f in AppServices.Repo.GetFocusSounds(textId)) Focus.Add(f);
        return true;
    }

    public VocabEntry? FindVocab(string word) => _vocab.TryGetValue(word, out var v) ? v : null;

    /// <summary>IPA for the current accent: vocabulary first, then the native lexicon.</summary>
    public string IpaFor(string word)
    {
        var v = FindVocab(word);
        var ipa = v == null ? "" : (Accent == "UK" ? v.IpaUk : v.IpaUs);
        if (string.IsNullOrWhiteSpace(ipa)) ipa = AppServices.Assessor?.Lookup(word.ToLowerInvariant())?.Ipa ?? "";
        return ipa;
    }

    [RelayCommand]
    public async Task SpeakAllAsync()
    {
        if (Text == null) return;
        Status = "";
        if (!await Speech.SpeakAsync(Text.Body.Replace("\n\n", "\n"), Speed)) Status = "Синтез речи недоступен.";
    }

    [RelayCommand] public void Stop() => AppServices.Player.Stop();

    public async Task SpeakWordAsync(string word)
    {
        Status = "";
        if (!await Speech.SpeakAsync(word, Math.Min(Speed, 1.0))) Status = "Синтез речи недоступен.";
    }

    public void SaveWord(string word)
    {
        var v = FindVocab(word);
        AppServices.Repo.SaveWord(word, IpaFor(word), v?.TranslationRu, Text?.Id);
    }
}
