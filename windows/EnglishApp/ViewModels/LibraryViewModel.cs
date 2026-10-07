using System.Collections.ObjectModel;
using CommunityToolkit.Mvvm.ComponentModel;
using EnglishApp.Models;
using EnglishApp.Services;

namespace EnglishApp.ViewModels;

public partial class LibraryViewModel : ObservableObject
{
    private const string All = "Все";
    private bool _ready;

    public IReadOnlyList<string> Levels { get; } = new[] { All, "A1", "A2", "B1", "B2", "C1", "C2" };
    public IReadOnlyList<string> Statuses { get; } = new[] { All, "Новые", "В процессе", "Пройдено" };
    public ObservableCollection<string> Genres { get; } = new();
    public ObservableCollection<TextSummary> Items { get; } = new();

    [ObservableProperty] private string selectedLevel = All;
    [ObservableProperty] private string selectedGenre = All;
    [ObservableProperty] private string selectedStatus = All;
    [ObservableProperty] private string searchText = "";
    [ObservableProperty] private string countText = "";
    [ObservableProperty] private bool isEmpty;
    [ObservableProperty] private string emptyText = "";

    partial void OnSelectedLevelChanged(string value) => Refresh();
    partial void OnSelectedGenreChanged(string value) => Refresh();
    partial void OnSelectedStatusChanged(string value) => Refresh();
    partial void OnSearchTextChanged(string value) => Refresh();

    public void Load()
    {
        _ready = false;
        Genres.Clear();
        Genres.Add(All);
        foreach (var g in AppServices.Repo.GetGenres()) Genres.Add(g);
        SelectedGenre = All;
        _ready = true;
        Refresh();
    }

    public void Refresh()
    {
        if (!_ready) return;
        string? status = SelectedStatus switch { "Новые" => "new", "В процессе" => "started", "Пройдено" => "done", _ => null };
        var list = AppServices.Repo.GetTexts(
            SelectedLevel == All ? null : SelectedLevel,
            SelectedGenre == All || string.IsNullOrEmpty(SelectedGenre) ? null : SelectedGenre,
            status, SearchText);
        var reads = AppServices.Repo.GetReadSummaries();
        Items.Clear();
        foreach (var t in list) Items.Add(reads.TryGetValue(t.Id, out var rs) ? t with { Read = rs } : t);
        CountText = $"Найдено: {list.Count}";
        IsEmpty = list.Count == 0;
        bool filtered = SelectedLevel != All || SelectedGenre != All || SelectedStatus != All || !string.IsNullOrWhiteSpace(SearchText);
        EmptyText = !AppServices.Repo.ContentAvailable ? "База текстов не найдена."
            : filtered ? "Ничего не найдено. Попробуйте изменить фильтры или поисковый запрос." : "Библиотека пока пуста.";
    }
}
