package app.englishpron.ui

import android.os.Build
import androidx.compose.foundation.isSystemInDarkTheme
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.*
import androidx.compose.runtime.Composable
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import app.englishpron.UserSettings

// Brand palette: deep indigo + warm amber accent (used on Android < 12, where there is no dynamic color).
private val Light = lightColorScheme(
    primary = Color(0xFF3F3DA8), onPrimary = Color.White, primaryContainer = Color(0xFFE1E0FF), onPrimaryContainer = Color(0xFF0B0A5E),
    secondary = Color(0xFF5B5D72), secondaryContainer = Color(0xFFE0E0F9), onSecondaryContainer = Color(0xFF181A2C),
    tertiary = Color(0xFFB26A00), onTertiary = Color.White, tertiaryContainer = Color(0xFFFFDDB3), onTertiaryContainer = Color(0xFF2B1700),
    background = Color(0xFFFBF8FF), surface = Color(0xFFFBF8FF), surfaceVariant = Color(0xFFE4E1EC), onSurfaceVariant = Color(0xFF46464F),
)
private val Dark = darkColorScheme(
    primary = Color(0xFFC0C1FF), onPrimary = Color(0xFF1B1B7C), primaryContainer = Color(0xFF2A2A92), onPrimaryContainer = Color(0xFFE1E0FF),
    secondary = Color(0xFFC4C4DD), secondaryContainer = Color(0xFF424559), onSecondaryContainer = Color(0xFFE0E0F9),
    tertiary = Color(0xFFFFB95C), onTertiary = Color(0xFF492900), tertiaryContainer = Color(0xFF6A3E00), onTertiaryContainer = Color(0xFFFFDDB3),
    background = Color(0xFF131318), surface = Color(0xFF131318), surfaceVariant = Color(0xFF46464F), onSurfaceVariant = Color(0xFFC7C5D0),
)

private val AppShapes = Shapes(
    small = RoundedCornerShape(10.dp), medium = RoundedCornerShape(16.dp), large = RoundedCornerShape(20.dp), extraLarge = RoundedCornerShape(28.dp),
)

private fun appTypography(): Typography {
    val d = Typography()
    return d.copy(
        headlineSmall = d.headlineSmall.copy(fontWeight = FontWeight.SemiBold),
        titleLarge = d.titleLarge.copy(fontWeight = FontWeight.SemiBold),
        titleMedium = d.titleMedium.copy(fontWeight = FontWeight.SemiBold),
    )
}

@Composable
fun AppTheme(dark: Boolean = isSystemInDarkTheme(), content: @Composable () -> Unit) {
    val ctx = LocalContext.current
    val scheme = when {
        Build.VERSION.SDK_INT >= 31 -> if (dark) dynamicDarkColorScheme(ctx) else dynamicLightColorScheme(ctx)
        dark -> Dark
        else -> Light
    }
    MaterialTheme(colorScheme = scheme, shapes = AppShapes, typography = appTypography(), content = content)
}

/** Text style of the reading text from the user's settings (size S/M/L/XL, line spacing, serif option). */
fun UserSettings.readingStyle(): TextStyle {
    val size = listOf(16, 20, 24, 28)[fontSize.coerceIn(0, 3)]
    val mult = listOf(1.3f, 1.5f, 1.75f)[lineSpacing.coerceIn(0, 2)]
    return TextStyle(fontSize = size.sp, lineHeight = (size * mult).sp, fontFamily = if (serif) FontFamily.Serif else FontFamily.Default)
}
