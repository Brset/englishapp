using Microsoft.UI.Xaml.Media;

namespace EnglishApp.Services;

/// <summary>Brand palette (deep indigo + warm amber) and score colours shared by code-behind and models.</summary>
public static class Ui
{
    private static SolidColorBrush Make(byte r, byte g, byte b) => new(Windows.UI.Color.FromArgb(255, r, g, b));

    public static readonly SolidColorBrush Indigo = Make(0x4F, 0x46, 0xE5);
    public static readonly SolidColorBrush Amber = Make(0xF5, 0x9E, 0x0B);
    public static readonly SolidColorBrush Green = Make(0x2E, 0x9E, 0x5B);
    public static readonly SolidColorBrush Red = Make(0xD6, 0x45, 0x45);
    public static readonly SolidColorBrush Orange = Make(0xE0, 0x80, 0x1A);
    public static readonly SolidColorBrush Gray = Make(0x80, 0x80, 0x80);

    /// <summary>&gt;= 80 green, 60..79 amber, below 60 red, null gray.</summary>
    public static SolidColorBrush ScoreBrush(double? score) => score switch
    {
        null => Gray,
        >= 80 => Green,
        >= 60 => Amber,
        _ => Red,
    };

    public static readonly string[] FontSizeNames = { "S", "M", "L", "XL" };
    public static readonly double[] FontSizes = { 16, 20, 26, 32 };
    public static readonly string[] LineSpacingNames = { "Узкий", "Обычный", "Широкий" };
    public static readonly double[] LineSpacings = { 1.4, 1.65, 2.0 };
}
