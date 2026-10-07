package app.englishpron.ui

import androidx.compose.animation.core.Animatable
import androidx.compose.animation.core.Spring
import androidx.compose.animation.core.spring
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.EmojiEvents
import androidx.compose.material.icons.filled.Star
import androidx.compose.material3.*
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.remember
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.graphicsLayer
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.compose.ui.window.Dialog
import kotlin.math.roundToInt

/** Overlay after a finished text: coverage, score (when the background assessment is ready), XP, level and new badges. */
@Composable
fun CelebrationOverlay(c: Celebration, score: Double?, onDismiss: () -> Unit) {
    val scale = remember { Animatable(0.75f) }
    LaunchedEffect(Unit) { scale.animateTo(1f, spring(Spring.DampingRatioMediumBouncy, Spring.StiffnessLow)) }
    val done = c.coverage >= 90
    Dialog(onDismissRequest = onDismiss) {
        Card(Modifier.fillMaxWidth().graphicsLayer { scaleX = scale.value; scaleY = scale.value }, shape = RoundedCornerShape(28.dp)) {
            Column(Modifier.padding(24.dp), horizontalAlignment = Alignment.CenterHorizontally, verticalArrangement = Arrangement.spacedBy(10.dp)) {
                Icon(Icons.Filled.EmojiEvents, null, Modifier.size(44.dp), tint = MaterialTheme.colorScheme.tertiary)
                Text(if (done) "Текст пройден!" else "Хорошее начало!", style = MaterialTheme.typography.headlineSmall, textAlign = TextAlign.Center)
                if (c.title.isNotEmpty()) Text(c.title, style = MaterialTheme.typography.bodyMedium, color = MaterialTheme.colorScheme.onSurfaceVariant, textAlign = TextAlign.Center)
                ProgressRing(c.coverage.toFloat() / 100f, Modifier.size(110.dp), 10.dp, if (done) Color(0xFF2E7D32) else MaterialTheme.colorScheme.tertiary) {
                    Text("${c.coverage.roundToInt()}%", fontSize = 28.sp, fontWeight = FontWeight.Bold)
                }
                Text("прочитано", style = MaterialTheme.typography.labelMedium)
                if (score != null) Text("Оценка %.0f · %s".format(score, levelLabel(score)), style = MaterialTheme.typography.titleMedium, color = scoreColor(score))
                else Text("Оценка считается в фоне", style = MaterialTheme.typography.bodySmall, color = MaterialTheme.colorScheme.onSurfaceVariant)
                Row(verticalAlignment = Alignment.CenterVertically) {
                    Icon(Icons.Filled.Star, null, tint = MaterialTheme.colorScheme.tertiary)
                    Spacer(Modifier.width(6.dp))
                    Text("+${c.xp} XP", style = MaterialTheme.typography.titleLarge)
                }
                Text("${c.level.name} · ${c.totalXp} XP", style = MaterialTheme.typography.bodyMedium)
                c.level.next?.let { n ->
                    LinearProgressIndicator(progress = { ((c.totalXp - c.level.floor).toFloat() / (n - c.level.floor)).coerceIn(0f, 1f) },
                        Modifier.fillMaxWidth().clip(RoundedCornerShape(4.dp)))
                }
                c.achievements.forEach { a ->
                    Row(Modifier.fillMaxWidth().clip(RoundedCornerShape(12.dp)).padding(vertical = 2.dp), verticalAlignment = Alignment.CenterVertically) {
                        Icon(Icons.Filled.EmojiEvents, null, tint = MaterialTheme.colorScheme.primary)
                        Spacer(Modifier.width(8.dp))
                        Column { Text(a.title, fontWeight = FontWeight.Medium); Text(a.desc, style = MaterialTheme.typography.bodySmall) }
                    }
                }
                Button(onDismiss, Modifier.fillMaxWidth()) { Text("Отлично") }
            }
        }
    }
}
