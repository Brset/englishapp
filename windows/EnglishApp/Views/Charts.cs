using EnglishApp.Models;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Media;
using Microsoft.UI.Xaml.Shapes;

namespace EnglishApp.Views;

/// <summary>Tiny chart helpers built from plain controls (no chart library).</summary>
public static class Charts
{
    public static void Bars(Grid host, IReadOnlyList<DayMinutes> data, Brush fill)
    {
        host.Children.Clear();
        host.ColumnDefinitions.Clear();
        for (int i = 0; i < data.Count; i++)
        {
            host.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(1, GridUnitType.Star) });
            var d = data[i];
            var col = new StackPanel
            {
                VerticalAlignment = VerticalAlignment.Bottom, HorizontalAlignment = HorizontalAlignment.Center, Spacing = 3,
            };
            col.Children.Add(new TextBlock
            {
                Text = d.Minutes >= 1 ? $"{d.Minutes:0}" : "", FontSize = 11, Opacity = 0.8, HorizontalAlignment = HorizontalAlignment.Center,
            });
            col.Children.Add(new Border
            {
                Width = 26, Height = d.BarHeight, CornerRadius = new CornerRadius(5), Background = fill,
                HorizontalAlignment = HorizontalAlignment.Center, Opacity = d.Minutes > 0 ? 1 : 0.25,
            });
            col.Children.Add(new TextBlock { Text = d.Label, FontSize = 11, Opacity = 0.7, HorizontalAlignment = HorizontalAlignment.Center });
            Grid.SetColumn(col, i);
            host.Children.Add(col);
        }
    }

    /// <summary>Line chart of values 0..100 in a canvas (uses the canvas' current size).</summary>
    public static void LineChart(Canvas canvas, IReadOnlyList<double> values, Brush stroke)
    {
        canvas.Children.Clear();
        double w = canvas.ActualWidth, h = canvas.ActualHeight;
        if (w < 20 || h < 20 || values.Count == 0) return;
        double pad = 10;
        double X(int i) => values.Count == 1 ? w / 2 : pad + (w - 2 * pad) * i / (values.Count - 1);
        double Y(double v) => pad + (h - 2 * pad) * (1 - Math.Clamp(v, 0, 100) / 100.0);
        foreach (var guide in new[] { 60.0, 80.0 })
            canvas.Children.Add(new Line { X1 = 0, X2 = w, Y1 = Y(guide), Y2 = Y(guide), Stroke = stroke, StrokeThickness = 1, Opacity = 0.2 });
        var pl = new Polyline { Stroke = stroke, StrokeThickness = 2.5, StrokeLineJoin = PenLineJoin.Round };
        for (int i = 0; i < values.Count; i++) pl.Points.Add(new Windows.Foundation.Point(X(i), Y(values[i])));
        canvas.Children.Add(pl);
        for (int i = 0; i < values.Count; i++)
        {
            var dot = new Ellipse { Width = 7, Height = 7, Fill = stroke };
            Canvas.SetLeft(dot, X(i) - 3.5);
            Canvas.SetTop(dot, Y(values[i]) - 3.5);
            canvas.Children.Add(dot);
        }
    }
}
