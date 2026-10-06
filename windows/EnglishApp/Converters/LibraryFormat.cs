using Microsoft.UI.Xaml;

namespace EnglishApp.Converters;

/// <summary>
/// Presentation helpers for x:Bind function bindings on the Home and Library pages.
/// Level / score colours are drawn as stacked layers whose visibility is chosen here, so the
/// brushes themselves stay {ThemeResource ...} in XAML and follow the in-app light/dark theme.
/// </summary>
public static class LibraryFormat
{
    private static Visibility Is(bool v) => v ? Visibility.Visible : Visibility.Collapsed;

    public static Visibility LevelA1(string? level) => Is(level == "A1");
    public static Visibility LevelA2(string? level) => Is(level == "A2");
    public static Visibility LevelB1(string? level) => Is(level == "B1");
    public static Visibility LevelB2(string? level) => Is(level == "B2");
    public static Visibility LevelC1(string? level) => Is(level == "C1");
    public static Visibility LevelC2(string? level) => Is(level == "C2");
    /// <summary>Fallback layer for an unknown / empty level.</summary>
    public static Visibility LevelOther(string? level) =>
        Is(level is not ("A1" or "A2" or "B1" or "B2" or "C1" or "C2"));

    public static Visibility ScoreGood(double? score) => Is(score is >= 80);
    public static Visibility ScoreFair(double? score) => Is(score is >= 60 and < 80);
    public static Visibility ScorePoor(double? score) => Is(score is < 60);
    public static Visibility HasScore(double? score) => Is(score is not null);

    public static string ScoreText(double? score) => score is double s ? $"{s:0}%" : "";

    /// <summary>Segoe Fluent Icons glyph for a progress status.</summary>
    public static string StatusGlyph(string? status) => status switch
    {
        "done" => "",     // CheckMark
        "started" => "",  // Play
        _ => "",          // FavoriteStar (outline) – new
    };

    public static string WordsText(int words) => $"{words} сл.";
}
