package app.englishpron.ui

import androidx.compose.animation.core.*
import androidx.compose.foundation.Canvas
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.material3.Button
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.alpha
import androidx.compose.ui.draw.clip
import androidx.compose.ui.geometry.CornerRadius
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.Path
import androidx.compose.ui.graphics.StrokeCap
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.material3.Icon
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import kotlin.math.max

/** Animated circular progress ring (0..1) with optional centre content. */
@Composable
fun ProgressRing(
    fraction: Float, modifier: Modifier = Modifier.size(56.dp), stroke: Dp = 6.dp,
    color: Color = MaterialTheme.colorScheme.primary, track: Color = MaterialTheme.colorScheme.surfaceVariant,
    content: @Composable BoxScope.() -> Unit = {},
) {
    val a by animateFloatAsState(fraction.coerceIn(0f, 1f), tween(700), label = "ring")
    Box(modifier, Alignment.Center) {
        Canvas(Modifier.fillMaxSize()) {
            val sw = stroke.toPx()
            val tl = Offset(sw / 2, sw / 2)
            val sz = Size(size.width - sw, size.height - sw)
            drawArc(track, 0f, 360f, false, tl, sz, style = Stroke(sw))
            if (a > 0f) drawArc(color, -90f, 360f * a, false, tl, sz, style = Stroke(sw, cap = StrokeCap.Round))
        }
        content()
    }
}

/** Bars with labels underneath (e.g. minutes per day). */
@Composable
fun BarChart(values: List<Pair<String, Double>>, modifier: Modifier = Modifier, height: Dp = 110.dp, color: Color = MaterialTheme.colorScheme.primary) {
    val peak = max(1.0, values.maxOfOrNull { it.second } ?: 1.0)
    val track = MaterialTheme.colorScheme.surfaceVariant
    Column(modifier) {
        Canvas(Modifier.fillMaxWidth().height(height)) {
            if (values.isEmpty()) return@Canvas
            val slot = size.width / values.size
            val bw = slot * 0.55f
            values.forEachIndexed { i, (_, v) ->
                val h = (size.height * (v / peak)).toFloat().coerceAtLeast(if (v > 0) 4f else 2f)
                drawRoundRect(if (v > 0) color else track, Offset(i * slot + (slot - bw) / 2, size.height - h), Size(bw, h), CornerRadius(bw / 3))
            }
        }
        Row(Modifier.fillMaxWidth()) {
            values.forEach { (l, v) ->
                Column(Modifier.weight(1f), horizontalAlignment = Alignment.CenterHorizontally) {
                    Text(l, style = MaterialTheme.typography.labelSmall, color = MaterialTheme.colorScheme.onSurfaceVariant)
                    Text(if (v > 0) "%.0f".format(v) else "", style = MaterialTheme.typography.labelSmall)
                }
            }
        }
    }
}

/** Line chart of values 0..100 (accuracy over time). */
@Composable
fun LineChart(values: List<Double>, modifier: Modifier = Modifier, height: Dp = 120.dp, color: Color = MaterialTheme.colorScheme.primary) {
    val grid = MaterialTheme.colorScheme.surfaceVariant
    Canvas(modifier.fillMaxWidth().height(height)) {
        for (g in listOf(0f, 0.5f, 1f)) drawLine(grid, Offset(0f, size.height * g), Offset(size.width, size.height * g), 1.dp.toPx())
        if (values.size < 2) {
            values.firstOrNull()?.let { drawCircle(color, 5.dp.toPx(), Offset(size.width / 2, size.height * (1f - (it / 100.0).toFloat().coerceIn(0f, 1f)))) }
            return@Canvas
        }
        val path = Path()
        values.forEachIndexed { i, v ->
            val x = size.width * i / (values.size - 1)
            val y = size.height * (1f - (v / 100.0).toFloat().coerceIn(0f, 1f))
            if (i == 0) path.moveTo(x, y) else path.lineTo(x, y)
        }
        drawPath(path, color, style = Stroke(2.5.dp.toPx(), cap = StrokeCap.Round))
        values.forEachIndexed { i, v ->
            drawCircle(color, 3.dp.toPx(), Offset(size.width * i / (values.size - 1), size.height * (1f - (v / 100.0).toFloat().coerceIn(0f, 1f))))
        }
    }
}

/** Loading skeleton block. */
@Composable
fun SkeletonBox(modifier: Modifier = Modifier) {
    val t = rememberInfiniteTransition(label = "skeleton")
    val a by t.animateFloat(0.25f, 0.6f, infiniteRepeatable(tween(800), RepeatMode.Reverse), label = "a")
    Box(modifier.clip(RoundedCornerShape(12.dp)).background(MaterialTheme.colorScheme.surfaceVariant).alpha(a))
}

@Composable
fun SkeletonList(count: Int = 4) = Column(Modifier.fillMaxWidth().padding(16.dp), verticalArrangement = Arrangement.spacedBy(12.dp)) {
    repeat(count) { SkeletonBox(Modifier.fillMaxWidth().height(88.dp)) }
}

/** Friendly empty state: icon, headline, hint and an optional action. */
@Composable
fun EmptyState(icon: ImageVector, title: String, hint: String, actionLabel: String? = null, onAction: () -> Unit = {}) =
    Column(Modifier.fillMaxSize().padding(32.dp), horizontalAlignment = Alignment.CenterHorizontally, verticalArrangement = Arrangement.Center) {
        Icon(icon, null, Modifier.size(56.dp), tint = MaterialTheme.colorScheme.primary.copy(alpha = 0.6f))
        Spacer(Modifier.height(12.dp))
        Text(title, style = MaterialTheme.typography.titleMedium, textAlign = TextAlign.Center)
        Text(hint, style = MaterialTheme.typography.bodyMedium, color = MaterialTheme.colorScheme.onSurfaceVariant, textAlign = TextAlign.Center)
        if (actionLabel != null) { Spacer(Modifier.height(16.dp)); Button(onAction) { Text(actionLabel) } }
    }

/** "Пройден" (>= 90 %) / "Начат" (50..89 %) badge; nothing below 50 %. */
@Composable
fun CoverageBadge(coverage: Double?, modifier: Modifier = Modifier) {
    val c = coverage ?: return
    val (label, col) = when {
        c >= 90 -> "Пройден" to Color(0xFF2E7D32)
        c >= 50 -> "Начат" to Color(0xFFB26A00)
        else -> return
    }
    Text(label, modifier.clip(RoundedCornerShape(8.dp)).background(col.copy(alpha = 0.16f)).padding(horizontal = 8.dp, vertical = 2.dp),
        style = MaterialTheme.typography.labelMedium, color = col)
}

fun coverageOf(live: app.englishpron.engine.LiveState?): Double {
    val w = live?.words ?: return 0.0
    if (w.isEmpty()) return 0.0
    return w.count { it.state == app.englishpron.engine.WordState.READ } * 100.0 / w.size
}
