using Microsoft.UI.Xaml;

namespace EnglishApp.Models;

public sealed record TextSummary(string Id, string Level, string Genre, string TitleEn, string TitleRu,
    string DescriptionRu, int WordCount, string Status, double? BestScore)
{
    public string StatusRu => Status switch { "done" => "Пройдено", "started" => "В процессе", _ => "Новый" };
    public string Subtitle => $"{Level} · {Genre} · {WordCount} сл. · {StatusRu}" +
                              (BestScore is double b ? $" · {b:0}%" : "");

    /// <summary>Reading attempts summary (null when never read); filled by the library view model.</summary>
    public ReadSummary? Read { get; init; }
    public string BadgeText => Read?.Badge ?? "";
    public Visibility BadgeVisibility => string.IsNullOrEmpty(Read?.Badge) ? Visibility.Collapsed : Visibility.Visible;
    public string ScoreLine => Read?.ScoreLine ?? "";
    public Visibility ScoreVisibility => string.IsNullOrEmpty(ScoreLine) ? Visibility.Collapsed : Visibility.Visible;
}

/// <summary>One recorded reading; Score is null until the background job has finished.</summary>
public sealed record AttemptInfo(long Id, string TextId, long CreatedAt, double? Score, string WavPath,
    string? ScoresJson, string? JobStatus)
{
    public bool Pending => Score == null && (JobStatus is "queued" or "processing");
    public string? Badge => Score != null ? null : JobStatus switch
    {
        "queued" or "processing" => "оценка готовится…",
        "failed" => "оценка не удалась",
        "cancelled" => "оценка отменена",
        _ => null,
    };
}

public sealed record ReadSummary(int Attempts, double? LastScore, IReadOnlyList<double> History, string? Badge, int Skipped)
{
    public string ScoreLine
    {
        get
        {
            var parts = new List<string>();
            if (LastScore is double s) parts.Add($"Оценка: {s:0}/100");
            else if (Skipped > 0) parts.Add($"Прочитано, пропущено слов: {Skipped}");
            else parts.Add("Прочитано");
            if (History.Count > 1) parts.Add("Попытки: " + string.Join(" → ", History.Select(h => h.ToString("0"))));
            else if (Attempts > 1) parts.Add($"Попыток: {Attempts}");
            return string.Join(" · ", parts);
        }
    }
}

/// <summary>A row of processing_jobs.</summary>
public sealed record JobRow(long Id, string TextId, string WavPath, double AudioSeconds, string Status);

public sealed record TextDetail(string Id, string Level, string Genre, string TitleEn, string TitleRu,
    string DescriptionRu, string Body, int WordCount);

public sealed record VocabEntry(string Word, string IpaUs, string IpaUk, string TranslationRu);

public sealed record FocusSound(string Sound, string TipRu, IReadOnlyList<string> Examples);

public sealed record SoundCard(string Id, string Slug, string NameRu, string Ipa, string Difficulty, string Json)
{
    public string Title => string.IsNullOrWhiteSpace(Ipa) ? NameRu : $"/{Ipa}/  {NameRu}";
}

public sealed record SavedWord(long Id, string Word, string Ipa, string TranslationRu, string SourceTextId,
    double Ease, double IntervalDays, long DueAt, int Reps, int Lapses)
{
    public string DueText => DateTimeOffset.FromUnixTimeSeconds(DueAt).ToLocalTime().ToString("dd.MM.yyyy");
}

public sealed record ProgressStats(int TextsTotal, int TextsDone, int TextsStarted, int Attempts, double AvgBest,
    int Streak, double MinutesTotal, int WordsSaved, int WordsDue);

public sealed record PhonemeStat(string Phoneme, int Attempts, int Errors, double AvgScore)
{
    public string Text => $"/{Phoneme}/  ошибок {Errors} из {Attempts}, средний балл {AvgScore:0}";
}
