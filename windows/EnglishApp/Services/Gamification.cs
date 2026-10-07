using EnglishApp.Models;
using CommunityToolkit.Mvvm.ComponentModel;

namespace EnglishApp.Services;

public static class Gamification
{
    private static readonly (string Name, int Min)[] Levels =
    {
        ("Новичок", 0), ("Ученик", 300), ("Читатель", 1000), ("Знаток", 3000), ("Эксперт", 7000), ("Мастер", 15000),
    };

    public static XpInfo Info(int total)
    {
        int li = 0;
        for (int i = 0; i < Levels.Length; i++) if (total >= Levels[i].Min) li = i;
        int next = li + 1 < Levels.Length ? Levels[li + 1].Min : -1;
        return new XpInfo(total, li, Levels[li].Name, Levels[li].Min, next);
    }

    public static readonly IReadOnlyList<Achievement> All = new Achievement[]
    {
        new("first_text", "Первый текст", "Прочитайте первый текст (90% и больше)", "1"),
        new("texts_10", "Десять текстов", "Пройдите 10 текстов", "10"),
        new("streak_3", "Три дня подряд", "Занимайтесь 3 дня подряд", "3д"),
        new("streak_7", "Неделя без пропусков", "Серия из 7 дней", "7д"),
        new("words_1000", "1000 слов", "Прочитайте вслух 1000 слов", "1k"),
        new("words_10000", "10 000 слов", "Прочитайте вслух 10 000 слов", "10k"),
        new("all_a1", "Весь уровень A1", "Пройдите все тексты уровня A1", "A1"),
        new("perfect_theta", "Идеальный θ", "Средний балл звука θ не ниже 90", "θ"),
        new("score_90", "Отличное произношение", "Получите оценку 90 и выше", "90"),
        new("drill_20", "Тренировка слов", "Проговорите 20 слов в тренировке", "20"),
        new("shadow_5", "Эхо", "Повторите 5 предложений в шэдоуинге", "5"),
    };

    public static Achievement? Find(string id) => All.FirstOrDefault(a => a.Id == id);
}

/// <summary>Font size / line spacing choice shared by the reading and record pages.</summary>
public sealed partial class ReadingTypography : ObservableObject
{
    [ObservableProperty] private int fontSizeIndex = Math.Clamp(AppServices.Settings.FontSizeIndex, 0, Ui.FontSizes.Length - 1);
    [ObservableProperty] private int lineSpacingIndex = Math.Clamp(AppServices.Settings.LineSpacingIndex, 0, Ui.LineSpacings.Length - 1);

    public IReadOnlyList<string> FontSizeNames => Ui.FontSizeNames;
    public IReadOnlyList<string> LineSpacingNames => Ui.LineSpacingNames;

    public double BodyFontSize => Ui.FontSizes[Math.Clamp(FontSizeIndex, 0, Ui.FontSizes.Length - 1)];
    public double BodyLineHeight => BodyFontSize * Ui.LineSpacings[Math.Clamp(LineSpacingIndex, 0, Ui.LineSpacings.Length - 1)];

    partial void OnFontSizeIndexChanged(int value)
    {
        if (value < 0) return;
        AppServices.Settings.FontSizeIndex = value;
        OnPropertyChanged(nameof(BodyFontSize));
        OnPropertyChanged(nameof(BodyLineHeight));
    }

    partial void OnLineSpacingIndexChanged(int value)
    {
        if (value < 0) return;
        AppServices.Settings.LineSpacingIndex = value;
        OnPropertyChanged(nameof(BodyLineHeight));
    }
}
