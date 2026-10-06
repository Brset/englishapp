using System.Collections.ObjectModel;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using EnglishApp.Models;
using EnglishApp.Services;

namespace EnglishApp.ViewModels;

public partial class VocabularyViewModel : ObservableObject
{
    private List<SavedWord> _due = new();

    public ObservableCollection<SavedWord> Words { get; } = new();
    [ObservableProperty] private SavedWord? current;
    [ObservableProperty] private bool isRevealed;
    [ObservableProperty] private string dueText = "";

    public bool HasCurrent => Current != null;
    public bool ShowReveal => HasCurrent && !IsRevealed;
    partial void OnCurrentChanged(SavedWord? value) { OnPropertyChanged(nameof(HasCurrent)); OnPropertyChanged(nameof(ShowReveal)); }
    partial void OnIsRevealedChanged(bool value) => OnPropertyChanged(nameof(ShowReveal));

    public void Load()
    {
        Words.Clear();
        foreach (var w in AppServices.Repo.GetSavedWords()) Words.Add(w);
        _due = AppServices.Repo.GetDueWords().ToList();
        NextCard();
    }

    private void NextCard()
    {
        IsRevealed = false;
        Current = _due.Count > 0 ? _due[0] : null;
        DueText = _due.Count == 0 ? "Сейчас повторять нечего." : $"К повторению: {_due.Count}";
    }

    [RelayCommand] private void Reveal() => IsRevealed = true;

    [RelayCommand]
    private async Task SpeakAsync() { if (Current != null) await Speech.SpeakAsync(Current.Word, 0.9); }

    [RelayCommand]
    private void Grade(string quality)
    {
        if (Current == null) return;
        AppServices.Repo.ReviewWord(Current, int.Parse(quality));
        _due.RemoveAt(0);
        NextCard();
    }

    public void Delete(SavedWord w)
    {
        AppServices.Repo.DeleteWord(w.Id);
        Words.Remove(w);
        _due.RemoveAll(d => d.Id == w.Id);
        if (Current?.Id == w.Id) NextCard();
    }
}
