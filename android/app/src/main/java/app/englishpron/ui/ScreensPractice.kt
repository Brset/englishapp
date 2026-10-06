package app.englishpron.ui

import android.Manifest
import android.content.pm.PackageManager
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.*
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.SpanStyle
import androidx.compose.ui.text.buildAnnotatedString
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.withStyle
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.core.content.ContextCompat
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import app.englishpron.data.WordResult
import app.englishpron.data.parseColor

// ---- Record ---------------------------------------------------------------------------------
@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun RecordScreen(vm: PracticeViewModel, onResult: () -> Unit) {
    val s by vm.state.collectAsStateWithLifecycle()
    val level by vm.level.collectAsStateWithLifecycle()
    val ctx = LocalContext.current
    val launcher = rememberLauncherForActivityResult(ActivityResultContracts.RequestPermission()) { granted ->
        vm.onPermissionResult(granted)
        if (granted) vm.startRecording()
    }
    fun toggle() {
        when (s.status) {
            RecStatus.IDLE -> {
                if (ContextCompat.checkSelfPermission(ctx, Manifest.permission.RECORD_AUDIO) == PackageManager.PERMISSION_GRANTED) vm.startRecording()
                else launcher.launch(Manifest.permission.RECORD_AUDIO)
            }
            RecStatus.RECORDING -> vm.stopAndAssess(onResult)
            RecStatus.PROCESSING -> {}
        }
    }
    if (s.text == null) { Empty("Сначала выберите текст в библиотеке"); return }

    Column(Modifier.fillMaxSize().padding(16.dp), verticalArrangement = Arrangement.spacedBy(12.dp)) {
        SingleChoiceSegmentedButtonRow(Modifier.fillMaxWidth()) {
            SegmentedButton(s.mode == RecordMode.WHOLE, { vm.setMode(RecordMode.WHOLE) }, SegmentedButtonDefaults.itemShape(0, 2),
                enabled = s.status == RecStatus.IDLE) { Text("Весь текст") }
            SegmentedButton(s.mode == RecordMode.SENTENCE, { vm.setMode(RecordMode.SENTENCE) }, SegmentedButtonDefaults.itemShape(1, 2),
                enabled = s.status == RecStatus.IDLE) { Text("По предложениям") }
        }
        if (s.mode == RecordMode.SENTENCE) {
            Row(verticalAlignment = Alignment.CenterVertically) {
                IconButton({ vm.setSentence(s.sentenceIdx - 1) }, enabled = s.sentenceIdx > 0 && s.status == RecStatus.IDLE) {
                    Icon(Icons.Filled.ChevronLeft, "Предыдущее")
                }
                Text("Предложение ${s.sentenceIdx + 1} из ${s.sentences.size}", Modifier.weight(1f), style = MaterialTheme.typography.labelLarge)
                IconButton({ vm.setSentence(s.sentenceIdx + 1) }, enabled = s.sentenceIdx < s.sentences.size - 1 && s.status == RecStatus.IDLE) {
                    Icon(Icons.Filled.ChevronRight, "Следующее")
                }
            }
        }
        Card(Modifier.weight(1f).fillMaxWidth()) {
            Text(s.referenceNow, Modifier.verticalScroll(rememberScrollState()).padding(16.dp), fontSize = 20.sp, lineHeight = 30.sp)
        }
        LinearProgressIndicator(progress = { level }, Modifier.fillMaxWidth().height(10.dp).clip(RoundedCornerShape(5.dp)))
        s.error?.let { Text(it, color = MaterialTheme.colorScheme.error) }
        if (s.permissionDenied) Text("Нужен доступ к микрофону (Настройки Android → Приложения → разрешения).", color = MaterialTheme.colorScheme.error)
        Text(
            when (s.status) {
                RecStatus.IDLE -> "Нажмите на микрофон и читайте вслух"
                RecStatus.RECORDING -> "Идёт запись… нажмите, чтобы закончить"
                RecStatus.PROCESSING -> "Оцениваем…"
            }, Modifier.align(Alignment.CenterHorizontally), style = MaterialTheme.typography.bodyMedium
        )
        Box(Modifier.fillMaxWidth(), Alignment.Center) {
            if (s.status == RecStatus.PROCESSING) CircularProgressIndicator()
            else LargeFloatingActionButton(onClick = ::toggle,
                containerColor = if (s.status == RecStatus.RECORDING) MaterialTheme.colorScheme.error else MaterialTheme.colorScheme.primary) {
                Icon(if (s.status == RecStatus.RECORDING) Icons.Filled.Stop else Icons.Filled.Mic,
                    if (s.status == RecStatus.RECORDING) "Стоп" else "Запись", Modifier.size(36.dp))
            }
        }
    }
}

// ---- Result ---------------------------------------------------------------------------------
@OptIn(ExperimentalLayoutApi::class)
@Composable
fun ResultScreen(vm: PracticeViewModel, onRetry: () -> Unit, onLibrary: () -> Unit) {
    val s by vm.state.collectAsStateWithLifecycle()
    val r = s.result
    if (r == null) { Empty("Результата пока нет"); return }
    var selected by remember { mutableStateOf<WordResult?>(null) }

    val annotated = remember(r, s.reference, selected) {
        buildAnnotatedString {
            var last = 0
            for (w in r.words.sortedBy { it.u16Begin }) {
                if (w.u16Begin < last || w.u16End > s.reference.length) continue
                append(s.reference.substring(last, w.u16Begin))
                val c = parseColor(w.colorHex)
                withStyle(SpanStyle(background = c.copy(alpha = if (w == selected) 0.7f else 0.3f), fontWeight = FontWeight.Medium)) {
                    append(s.reference.substring(w.u16Begin, w.u16End))
                }
                last = w.u16End
            }
            append(s.reference.substring(last))
        }
    }

    Column(Modifier.fillMaxSize().verticalScroll(rememberScrollState()).padding(16.dp), verticalArrangement = Arrangement.spacedBy(10.dp)) {
        Card(Modifier.fillMaxWidth()) {
            Column(Modifier.padding(16.dp), horizontalAlignment = Alignment.CenterHorizontally) {
                Text("%.0f".format(r.overall), fontSize = 56.sp, fontWeight = FontWeight.Bold)
                Text(levelLabel(r.overall), style = MaterialTheme.typography.titleMedium)
                Spacer(Modifier.height(8.dp))
                Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceAround) {
                    Score("Точность", r.accuracy); Score("Полнота", r.completeness); Score("Беглость", r.fluency)
                }
                if (r.wpm > 0) Text("Темп: %.0f слов/мин, длинных пауз: ${r.longPauses}".format(r.wpm), style = MaterialTheme.typography.labelMedium)
            }
        }
        if (!r.phonemeLevel) Text("Оценка отдельных звуков недоступна: фонемная модель ещё не подключена.",
            style = MaterialTheme.typography.bodySmall, color = MaterialTheme.colorScheme.onSurfaceVariant)
        r.warnings.forEach { Text(it, style = MaterialTheme.typography.bodySmall, color = MaterialTheme.colorScheme.tertiary) }

        Row(horizontalArrangement = Arrangement.spacedBy(12.dp)) {
            Legend(Color(0xFF2E7D32), "хорошо"); Legend(Color(0xFFF9A825), "средне"); Legend(Color(0xFFC62828), "слабо"); Legend(Color(0xFF9E9E9E), "пропущено")
        }
        Card(Modifier.fillMaxWidth()) { Text(annotated, Modifier.padding(16.dp), fontSize = 20.sp, lineHeight = 32.sp) }

        Text("Слова", style = MaterialTheme.typography.titleSmall)
        FlowRow(horizontalArrangement = Arrangement.spacedBy(6.dp)) {
            r.words.forEach { w ->
                FilterChip(selected == w, { selected = if (selected == w) null else w },
                    label = { Text("${w.text} ${"%.0f".format(w.score)}") },
                    colors = FilterChipDefaults.filterChipColors(
                        containerColor = parseColor(w.colorHex).copy(alpha = 0.2f),
                        selectedContainerColor = parseColor(w.colorHex).copy(alpha = 0.6f)))
            }
        }
        selected?.let { w ->
            ElevatedCard(Modifier.fillMaxWidth()) {
                Column(Modifier.padding(16.dp), verticalArrangement = Arrangement.spacedBy(6.dp)) {
                    Row(verticalAlignment = Alignment.CenterVertically) {
                        Text(w.text, style = MaterialTheme.typography.titleLarge)
                        Spacer(Modifier.width(8.dp))
                        if (w.expectedIpa.isNotEmpty()) Text("/${w.expectedIpa}/", color = MaterialTheme.colorScheme.primary)
                        Spacer(Modifier.weight(1f))
                        IconButton({ vm.speak(w.text) }) { Icon(Icons.Filled.VolumeUp, "Озвучить") }
                    }
                    Text(when (w.status) { "omitted" -> "Слово пропущено"; "substituted" -> "Распознано как другое слово"; else -> "Слово прочитано" })
                    if (w.phonemes.isNotEmpty()) FlowRow(horizontalArrangement = Arrangement.spacedBy(6.dp)) {
                        w.phonemes.forEach { p ->
                            val c = when {
                                p.score == null -> Color.Gray
                                p.score >= 80 -> Color(0xFF2E7D32)
                                p.score >= 60 -> Color(0xFFF9A825)
                                else -> Color(0xFFC62828)
                            }
                            Box(Modifier.clip(RoundedCornerShape(8.dp)).background(c.copy(alpha = 0.25f)).padding(horizontal = 8.dp, vertical = 4.dp)) {
                                Text(p.ipa + (if (p.substituted && p.actualIpa.isNotEmpty()) "→${p.actualIpa}" else "") +
                                    (p.score?.let { " %.0f".format(it) } ?: ""), fontSize = 14.sp)
                            }
                        }
                    }
                    r.advice.filter { a -> w.index in a.words }.forEach { a ->
                        Text("${a.titleRu}: ${a.tipRu}", style = MaterialTheme.typography.bodyMedium)
                    }
                }
            }
        }

        if (r.advice.isNotEmpty()) {
            SectionTitle("Над чем поработать")
            r.advice.forEach { a ->
                Card(Modifier.fillMaxWidth(), colors = CardDefaults.cardColors(containerColor = MaterialTheme.colorScheme.secondaryContainer)) {
                    Column(Modifier.padding(12.dp)) {
                        Text("${a.titleRu}  (/${a.expectedIpa}/" + (if (a.actualIpa.isNotEmpty()) " → /${a.actualIpa}/" else "") + ", ×${a.count})",
                            fontWeight = FontWeight.Medium)
                        Text(a.tipRu, style = MaterialTheme.typography.bodyMedium)
                    }
                }
            }
        }
        Row(horizontalArrangement = Arrangement.spacedBy(12.dp), modifier = Modifier.fillMaxWidth()) {
            OutlinedButton({ vm.playLast() }, Modifier.weight(1f)) { Icon(Icons.Filled.PlayArrow, null); Text("Моя запись") }
            if (s.mode == RecordMode.SENTENCE && s.sentenceIdx < s.sentences.size - 1)
                Button({ vm.nextSentence(); onRetry() }, Modifier.weight(1f)) { Text("Дальше") }
            else Button(onRetry, Modifier.weight(1f)) { Text("Ещё раз") }
        }
        TextButton(onLibrary, Modifier.align(Alignment.CenterHorizontally)) { Text("В библиотеку") }
    }
}

@Composable
private fun Score(label: String, v: Double) = Column(horizontalAlignment = Alignment.CenterHorizontally) {
    Text("%.0f".format(v), style = MaterialTheme.typography.titleLarge)
    Text(label, style = MaterialTheme.typography.labelMedium)
}

@Composable
private fun Legend(c: Color, label: String) = Row(verticalAlignment = Alignment.CenterVertically) {
    Box(Modifier.size(12.dp).clip(RoundedCornerShape(3.dp)).background(c))
    Spacer(Modifier.width(4.dp)); Text(label, style = MaterialTheme.typography.labelSmall)
}
