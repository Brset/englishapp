using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Media;

namespace EnglishApp.Converters;

/// <summary>
/// Theme-aware lookup of the design-system brushes (Styles/Theme.xaml) for code-behind that builds
/// Runs by hand (reading / record pages). Resolves against the element's ActualTheme, which follows the
/// in-app theme override, not only the system theme. Falls back to the given colour on any failure.
/// </summary>
public static class ReadingThemeBrushes
{
    private static readonly Dictionary<string, Brush> Cache = new();

    public static Brush Get(FrameworkElement? scope, string key, Windows.UI.Color fallback)
    {
        try
        {
            var theme = scope?.ActualTheme ?? ElementTheme.Default;
            if (theme == ElementTheme.Default)
                theme = Application.Current.RequestedTheme == ApplicationTheme.Dark ? ElementTheme.Dark : ElementTheme.Light;
            var themeKey = theme == ElementTheme.Dark ? "Dark" : "Light";
            var cacheKey = themeKey + "|" + key;
            lock (Cache)
            {
                if (Cache.TryGetValue(cacheKey, out var cached)) return cached;
            }
            Brush? found = Find(Application.Current.Resources, key, themeKey, 0) as Brush;
            if (found == null && Application.Current.Resources.TryGetValue(key, out var o) && o is Brush b) found = b;
            if (found != null)
            {
                lock (Cache) Cache[cacheKey] = found;
                return found;
            }
        }
        catch (Exception) { /* fall through to the hard-coded colour */ }
        return new SolidColorBrush(fallback);
    }

    private static object? Find(ResourceDictionary dict, string key, string themeKey, int depth)
    {
        if (depth > 6) return null;
        try
        {
            if (dict.ThemeDictionaries.TryGetValue(themeKey, out var td) && td is ResourceDictionary t
                && t.TryGetValue(key, out var v)) return v;
            if (themeKey == "Dark" && dict.ThemeDictionaries.TryGetValue("Default", out var dd) && dd is ResourceDictionary d
                && d.TryGetValue(key, out var dv)) return dv;
        }
        catch (Exception) { /* some framework dictionaries refuse enumeration; skip them */ }
        foreach (var merged in dict.MergedDictionaries)
        {
            var r = Find(merged, key, themeKey, depth + 1);
            if (r != null) return r;
        }
        return null;
    }

    private static Windows.UI.Color Rgb(byte r, byte g, byte b) => Windows.UI.Color.FromArgb(255, r, g, b);

    /// <summary>Word / phoneme read correctly (live tracker "read").</summary>
    public static Brush Good(FrameworkElement? scope) => Get(scope, "ScoreGoodBrush", Rgb(0x2E, 0x9E, 0x5B));
    /// <summary>Middle band; also the live tracker "skipped" colour.</summary>
    public static Brush Fair(FrameworkElement? scope) => Get(scope, "ScoreFairBrush", Rgb(0xE0, 0x80, 0x1A));
    public static Brush Poor(FrameworkElement? scope) => Get(scope, "ScorePoorBrush", Rgb(0xD6, 0x45, 0x45));
    /// <summary>Omitted words, unscored phonemes, live tracker "pending".</summary>
    public static Brush Missing(FrameworkElement? scope) => Get(scope, "ScoreMissingBrush", Rgb(0x80, 0x80, 0x80));
    public static Brush Primary(FrameworkElement? scope, Windows.UI.Color fallback) => Get(scope, "PrimaryBrush", fallback);

    /// <summary>Score thresholds of the design system: &gt;=80 good, 60-79 fair, &lt;60 poor, null missing.</summary>
    public static Brush ForScore(FrameworkElement? scope, double? score) => score switch
    {
        null => Missing(scope),
        >= 80 => Good(scope),
        >= 60 => Fair(scope),
        _ => Poor(scope),
    };
}
