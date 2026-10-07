package app.englishpron.ui

import android.Manifest
import android.content.pm.PackageManager
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.animation.core.animateFloatAsState
import androidx.compose.animation.core.tween
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
import kotlin.math.roundToInt
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
        if (granted) vm.startRecording(onResult)
    }
    fun toggle() {
        when (s.status) {
            RecStatus.IDLE -> {
                if (ContextCompat.checkSelfPermission(ctx, Manifest.permission.RECORD_AUDIO) == PackageManager.PERMISSION_GRANTED) vm.startRecording(onResult)
                else launcher.launch(Manifest.permission.RECORD_AUDIO)
            }
            RecStatus.RECORDING -> vm.stopAndSave(onDone = onResult)
            RecStatus.PROCESSING -> {}
        }
    }
    DisposableEffect(Unit) {
        onDispose { if ((ctx as? android.app.Activity)?.isChangingConfigurations != true) vm.cancelRecording() }
    }
    if (s.text == null) { Empty("Сначала выберите текст в библиотеке"); return }
    val settings by vm.settings.collectAsStateWithLifecycle()
    val style = settings.readingStyle()
    val idle = s.status == RecStatus.IDLE
    val pct = s.coveragePct()
    val anim by animateFloatAsState((pct / 100.0).toFloat().coerceIn(0f, 1f), tween(400), label = "coverage")
    val lastPar = s.paragraphs.size - 1

    Column(Modifier.fillMaxSize().padding(horizontal = 16.dp, vertical = 8.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
        SingleChoiceSegmentedButtonRow(Modifier.fillMaxWidth()) {
            val modes = listOf(RecordMode.PARAGRAPH to "Абзацы", RecordMode.WHOLE to "Весь текст", RecordMode.SENTENCE to "Предложения")
            modes.forEachIndexed { i, (m, label) ->
                SegmentedButton(s.mode == m, { vm.setMode(m) }, SegmentedButtonDefaults.itemShape(i, modes.size),
                    enabled = idle && (m != RecordMode.PARAGRAPH || s.paragraphs.size > 1)) { Text(label) }
            }
        }
        Row(verticalAlignment = Alignment.CenterVertically) {
            Text("Прочитано ${pct.roundToInt()}%", style = MaterialTheme.typography.headlineSmall, fontWeight = FontWeight.Bold,
                color = scoreColor(if (pct >= 90) 100.0 else if (pct >= 50) 70.0 else 40.0))
            Spacer(Modifier.weight(1f))
            if (s.mode == RecordMode.PARAGRAPH) {
                IconButton({ vm.setParagraph(s.paragraphIdx - 1) }, enabled = s.paragraphIdx > 0 && idle) { Icon(Icons.Filled.ChevronLeft, "Предыдущий абзац") }
                Text("Абзац ${s.paragraphIdx + 1}/${s.paragraphs.size}", style = MaterialTheme.typography.labelLarge)
                IconButton({ vm.setParagraph(s.paragraphIdx + 1) }, enabled = s.paragraphIdx < lastPar && idle) { Icon(Icons.Filled.ChevronRight, "Следующий абзац") }
            }
            if (s.mode == RecordMode.SENTENCE) {
                IconButton({ vm.setSentence(s.sentenceIdx - 1) }, enabled = s.sentenceIdx > 0 && idle) { Icon(Icons.Filled.ChevronLeft, "Предыдущее") }
                Text("Предл. ${s.sentenceIdx + 1}/${s.sentences.size}", style = MaterialTheme.typography.labelLarge)
                IconButton({ vm.setSentence(s.sentenceIdx + 1) }, enabled = s.sentenceIdx < s.sentences.size - 1 && idle) { Icon(Icons.Filled.ChevronRight, "Следующее") }
            }
        }
        LinearProgressIndicator(progress = { anim }, Modifier.fillMaxWidth().height(4.dp).clip(RoundedCornerShape(2.dp)))
        Card(Modifier.weight(1f).fillMaxWidth()) {
            if (s.mode == RecordMode.PARAGRAPH) {
                ParagraphReadingText(s.paragraphs, s.paragraphIdx, if (idle) null else s.live,
                    s.paragraphReads.filter { it.value > 0 }.keys, idle, style,
                    onTapWord = { vm.liveSetCursor(it) }, onSelect = { vm.setParagraph(it) }, modifier = Modifier.fillMaxSize())
            } else {
                LiveReadingText(s.referenceNow, if (idle) null else s.live, onTapWord = { vm.liveSetCursor(it) },
                    modifier = Modifier.fillMaxSize(), style = style)
            }
        }
        if (s.status == RecStatus.RECORDING && s.liveUnavailable)
            Text("Подсветка по ходу чтения недоступна (нет модели) - читайте, оценка будет готова позже.",
                style = MaterialTheme.typography.bodySmall, color = MaterialTheme.colorScheme.onSurfaceVariant)
        if (s.status == RecStatus.RECORDING && s.live?.done == true)
            Text(if (s.mode == RecordMode.PARAGRAPH && settings.autoAdvance && s.paragraphIdx < lastPar) "Абзац дочитан - перейду к следующему после паузы."
                else "Текст дочитан - запись остановится сама после паузы.", style = MaterialTheme.typography.bodySmall,
                color = MaterialTheme.colorScheme.primary)
        LinearProgressIndicator(progress = { level }, Modifier.fillMaxWidth().height(8.dp).clip(RoundedCornerShape(4.dp)))
        s.error?.let { Text(it, color = MaterialTheme.colorScheme.error) }
        if (s.permissionDenied) Text("Нужен доступ к микрофону (Настройки Android → Приложения → разрешения).", color = MaterialTheme.colorScheme.error)
        Text(
            when (s.status) {
                RecStatus.IDLE -> "Нажмите на микрофон и читайте вслух. Оценка посчитается в фоне."
                RecStatus.RECORDING -> "Идёт запись… нажмите, чтобы закончить"
                RecStatus.PROCESSING -> "Сохраняю запись…"
            }, Modifier.align(Alignment.CenterHorizontally), style = MaterialTheme.typography.bodyMedium
        )
        Box(Modifier.fillMaxWidth(), Alignment.Center) {
            if (s.status == RecStatus.PROCESSING) CircularProgressIndicator()
            else Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(16.dp)) {
                LargeFloatingActionButton(onClick = ::toggle,
                    containerColor = if (s.status == RecStatus.RECORDING) MaterialTheme.colorScheme.error else MaterialTheme.colorScheme.primary) {
                    Icon(if (s.status == RecStatus.RECORDING) Icons.Filled.Stop else Icons.Filled.Mic,
                        if (s.status == RecStatus.RECORDING) "Стоп" else "Запись", Modifier.size(36.dp))
                }
                if (s.mode == RecordMode.PARAGRAPH && s.paragraphIdx < lastPar)
                    FilledTonalButton({ vm.nextParagraph(onResult) }) { Text("Дальше"); Spacer(Modifier.width(4.dp)); Icon(Icons.Filled.SkipNext, null) }
            }
        }
    }
}
