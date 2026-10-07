package app.englishpron.ui

import androidx.compose.animation.AnimatedContent
import androidx.compose.foundation.background
import androidx.compose.foundation.horizontalScroll
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.*
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import app.englishpron.EnglishApp
import app.englishpron.UserSettings

private const val PASSAGE = "Every morning I walk to work through the park. The weather is usually cool, and I enjoy listening to the birds " +
    "while I think about the day ahead."

/** Suggested CEFR level from the pronunciation score of the sample passage. */
private fun levelFromScore(score: Double) = when {
    score < 40 -> "A1"
    score < 55 -> "A2"
    score < 68 -> "B1"
    score < 78 -> "B2"
    score < 88 -> "C1"
    else -> "C2"
}

/** First launch: what the app does, microphone permission, level (or a quick placement read) + daily goal + accent. */
@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun OnboardingScreen(initial: UserSettings, onFinish: (UserSettings) -> Unit) {
    val app = LocalContext.current.applicationContext as EnglishApp
    val scorer = remember { DrillScorer(app) }
    val gate = rememberMicGate()
    var step by rememberSaveable { mutableIntStateOf(0) }
    var level by rememberSaveable { mutableStateOf(initial.level) }
    var goal by rememberSaveable { mutableIntStateOf(initial.dailyGoal) }
    var uk by rememberSaveable { mutableStateOf(initial.british) }
    var micAsked by rememberSaveable { mutableStateOf(false) }
    var placement by remember { mutableStateOf(false) }
    var suggestion by remember { mutableStateOf<String?>(null) }

    if (placement) AlertDialog(
        onDismissRequest = { placement = false },
        title = { Text("Определить уровень") },
        text = {
            Column(Modifier.verticalScroll(rememberScrollState()), verticalArrangement = Arrangement.spacedBy(12.dp)) {
                Text("Прочитайте вслух:")
                Text(PASSAGE, style = MaterialTheme.typography.bodyLarge)
                SayPanel(scorer, PASSAGE, "placement", 30000) { suggestion = levelFromScore(it.score) }
                suggestion?.let { Text("Предлагаем уровень: $it", style = MaterialTheme.typography.titleMedium) }
            }
        },
        confirmButton = { TextButton({ suggestion?.let { level = it }; placement = false }, enabled = suggestion != null) { Text("Применить") } },
        dismissButton = { TextButton({ placement = false }) { Text("Закрыть") } },
    )

    Column(Modifier.fillMaxSize().background(MaterialTheme.colorScheme.background).systemBarsPadding().padding(24.dp)) {
        Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.Center) {
            repeat(3) { i ->
                Box(Modifier.padding(4.dp).size(if (i == step) 12.dp else 8.dp).clip(CircleShape)
                    .background(if (i == step) MaterialTheme.colorScheme.primary else MaterialTheme.colorScheme.surfaceVariant))
            }
        }
        AnimatedContent(step, Modifier.weight(1f).fillMaxWidth(), label = "onboarding") { st ->
            Column(Modifier.fillMaxSize().verticalScroll(rememberScrollState()), horizontalAlignment = Alignment.CenterHorizontally,
                verticalArrangement = Arrangement.spacedBy(14.dp, Alignment.CenterVertically)) {
                when (st) {
                    0 -> {
                        Icon(Icons.Filled.RecordVoiceOver, null, Modifier.size(80.dp), tint = MaterialTheme.colorScheme.primary)
                        Text("Читайте вслух — улучшайте произношение", style = MaterialTheme.typography.headlineMedium, textAlign = TextAlign.Center)
                        Feature(Icons.Filled.Book, "Длинные тексты по абзацам, с подсветкой слов по ходу чтения")
                        Feature(Icons.Filled.Insights, "Оценка каждого слова и звука, советы по артикуляции")
                        Feature(Icons.Filled.Hearing, "Тренировка слов и звуков, повторение с диктором")
                        Feature(Icons.Filled.WifiOff, "Всё работает офлайн")
                    }
                    1 -> {
                        Icon(Icons.Filled.Mic, null, Modifier.size(80.dp), tint = MaterialTheme.colorScheme.primary)
                        Text("Доступ к микрофону", style = MaterialTheme.typography.headlineMedium, textAlign = TextAlign.Center)
                        Text("Микрофон нужен, чтобы слышать, как вы читаете. Запись остаётся на устройстве.",
                            style = MaterialTheme.typography.bodyLarge, textAlign = TextAlign.Center)
                        Button({ gate { micAsked = true } }) { Text(if (micAsked) "Разрешено" else "Разрешить микрофон") }
                        Text("Можно пропустить и разрешить позже.", style = MaterialTheme.typography.bodySmall, color = MaterialTheme.colorScheme.onSurfaceVariant)
                    }
                    else -> {
                        Text("Ваш уровень", style = MaterialTheme.typography.headlineSmall)
                        Row(Modifier.horizontalScroll(rememberScrollState()), horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                            listOf("A1", "A2", "B1", "B2", "C1", "C2").forEach { l -> FilterChip(level == l, { level = l }, { Text(l) }) }
                        }
                        OutlinedButton({ suggestion = null; placement = true }) { Text("Определить уровень") }
                        Text("Цель на день", style = MaterialTheme.typography.headlineSmall)
                        Row(Modifier.horizontalScroll(rememberScrollState()), horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                            listOf(5, 10, 15, 20, 30).forEach { g -> FilterChip(goal == g, { goal = g }, { Text("$g мин") }) }
                        }
                        Text("Произношение", style = MaterialTheme.typography.headlineSmall)
                        SingleChoiceSegmentedButtonRow(Modifier.fillMaxWidth()) {
                            SegmentedButton(!uk, { uk = false }, SegmentedButtonDefaults.itemShape(0, 2)) { Text("US") }
                            SegmentedButton(uk, { uk = true }, SegmentedButtonDefaults.itemShape(1, 2)) { Text("UK") }
                        }
                    }
                }
            }
        }
        Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceBetween) {
            TextButton({ step-- }, enabled = step > 0) { Text("Назад") }
            if (step < 2) Button({ step++ }) { Text("Дальше") }
            else Button({ onFinish(initial.copy(onboarded = true, level = level, dailyGoal = goal, british = uk)) }) { Text("Начать") }
        }
    }
}

@Composable
private fun Feature(icon: ImageVector, text: String) = Row(Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically) {
    Icon(icon, null, tint = MaterialTheme.colorScheme.tertiary)
    Spacer(Modifier.width(12.dp))
    Text(text, style = MaterialTheme.typography.bodyLarge)
}
