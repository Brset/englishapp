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
        if (granted) vm.startRecording(onResult)
    }
    fun toggle() {
        when (s.status) {
            RecStatus.IDLE -> {
                if (ContextCompat.checkSelfPermission(ctx, Manifest.permission.RECORD_AUDIO) == PackageManager.PERMISSION_GRANTED) vm.startRecording(onResult)
                else launcher.launch(Manifest.permission.RECORD_AUDIO)
            }
            RecStatus.RECORDING -> vm.stopAndSave(onResult)
            RecStatus.PROCESSING -> {}
        }
    }
    DisposableEffect(Unit) {
        onDispose { if ((ctx as? android.app.Activity)?.isChangingConfigurations != true) vm.cancelRecording() }
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
            LiveReadingText(s.referenceNow, if (s.status == RecStatus.IDLE) null else s.live,
                onTapWord = { vm.liveSetCursor(it) }, modifier = Modifier.fillMaxSize())
        }
        if (s.status == RecStatus.RECORDING && s.liveUnavailable)
            Text("Подсветка по ходу чтения недоступна (нет модели) - читайте, оценка будет готова позже.",
                style = MaterialTheme.typography.bodySmall, color = MaterialTheme.colorScheme.onSurfaceVariant)
        if (s.status == RecStatus.RECORDING && s.live?.done == true)
            Text("Текст дочитан - запись остановится сама после паузы.", style = MaterialTheme.typography.bodySmall,
                color = MaterialTheme.colorScheme.primary)
        LinearProgressIndicator(progress = { level }, Modifier.fillMaxWidth().height(10.dp).clip(RoundedCornerShape(5.dp)))
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
            else LargeFloatingActionButton(onClick = ::toggle,
                containerColor = if (s.status == RecStatus.RECORDING) MaterialTheme.colorScheme.error else MaterialTheme.colorScheme.primary) {
                Icon(if (s.status == RecStatus.RECORDING) Icons.Filled.Stop else Icons.Filled.Mic,
                    if (s.status == RecStatus.RECORDING) "Стоп" else "Запись", Modifier.size(36.dp))
            }
        }
    }
}
