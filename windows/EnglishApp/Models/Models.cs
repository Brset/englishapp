namespace EnglishApp.Models;

public sealed record TextSummary(string Id, string Level, string Genre, string TitleEn, string TitleRu,
    string DescriptionRu, int WordCount, string Status, double? BestScore)
{
    public string StatusRu => Status switch { "done" => "Пройдено", "started" => "В процессе", _ => "Новый" };
    public string Subtitle => $"{Level} · {Genre} · {WordCount} сл. · {StatusRu}" +
                              (BestScore is double b ? $" · {b:0}%" : "");
}

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
