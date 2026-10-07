package app.englishpron.ui

import android.Manifest
import android.app.Application
import android.content.pm.PackageManager
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.animation.animateContentSize
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.VolumeUp
import androidx.compose.material.icons.filled.*
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.core.content.ContextCompat
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import androidx.lifecycle.viewModelScope
import androidx.lifecycle.viewmodel.compose.viewModel
import app.englishpron.EnglishApp
import app.englishpron.audio.SAMPLE_RATE
import app.englishpron.data.AssessmentUi
import app.englishpron.data.SavedWord
import app.englishpron.data.soundIdForIpa
import app.englishpron.engine.WordState
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.launch
import kotlinx.coroutines.withTimeoutOrNull
import org.json.JSONObject

data class DrillResult(val score: Double, val coverage: Double)

/**
 * Records one short utterance for a known [reference] (word, minimal-pair word, sentence) with the live engine
 * and returns an instant score: a quick assessment when the engine is idle, otherwise a score from the live
 * coverage. Needs RECORD_AUDIO granted.
 */
class DrillScorer(private val app: EnglishApp) {
    val level: StateFlow<Float> get() = app.recorder.level

    suspend fun listen(reference: String, maxMs: Long = 8000, stop: () -> Boolean = { false }, onRecorded: () -> Unit = {}): DrillResult? {
        app.tts.stop(); app.player.stop()
        val sess = try { app.engine.liveStart(reference) } catch (_: Throwable) { null }
        val ok = app.recorder.start(sess?.let { l -> { f: ShortArray -> l.offer(f) } })
        if (!ok) { sess?.release(); return null }
        val t0 = System.currentTimeMillis()
        var quiet = 0L
        var heard = false
        while (true) {
            delay(100)
            val now = System.currentTimeMillis()
            if (now - t0 >= maxMs || stop()) break
            val lvl = app.recorder.level.value
            if (lvl >= 0.06f) heard = true
            val finished = if (sess != null) sess.state.value?.done == true else heard
            if (finished && lvl < 0.04f) {
                if (quiet == 0L) quiet = now else if (now - quiet >= (if (sess != null) 700 else 1200)) break
            } else quiet = 0L
        }
        val pcm = app.recorder.stop()
        onRecorded()
        val live = try { if (sess != null) withTimeoutOrNull(3000) { sess.finish() } ?: run { sess.release(); null } else null } catch (_: Throwable) { null }
        if (pcm.size < SAMPLE_RATE / 4) return null
        val total = live?.words?.size ?: 0
        val read = live?.words?.count { it.state == WordState.READ } ?: 0
        val cov = if (total > 0) read * 100.0 / total else 0.0
        var score: Double? = null
        if (app.queue.jobs.value.isEmpty()) {
            score = try { withTimeoutOrNull(20000) { AssessmentUi.parse(app.engine.assess(pcm, SAMPLE_RATE, reference)).overall } } catch (_: Throwable) { null }
        }
        if (score == null && total > 0) score = 40 + cov * 0.45
        return score?.let { DrillResult(it, cov) }
    }
}

/** Returns a function that runs an action once the microphone permission is granted (asking for it if needed). */
@Composable
fun rememberMicGate(): (() -> Unit) -> Unit {
    val ctx = LocalContext.current
    var pending by remember { mutableStateOf<(() -> Unit)?>(null) }
    val launcher = rememberLauncherForActivityResult(ActivityResultContracts.RequestPermission()) { ok ->
        if (ok) pending?.invoke()
        pending = null
    }
    return { action ->
        if (ContextCompat.checkSelfPermission(ctx, Manifest.permission.RECORD_AUDIO) == PackageManager.PERMISSION_GRANTED) action()
        else { pending = action; launcher.launch(Manifest.permission.RECORD_AUDIO) }
    }
}

private enum class DrillPhase { IDLE, RECORDING, SCORING }

/** Mic button + instant score chip for one reference text. [onResult] gets every score. */
@Composable
fun SayPanel(scorer: DrillScorer, reference: String, resetKey: Any?, maxMs: Long = 6000, onResult: (DrillResult) -> Unit = {}) {
    val gate = rememberMicGate()
    val scope = rememberCoroutineScope()
    var phase by remember(resetKey) { mutableStateOf(DrillPhase.IDLE) }
    var result by remember(resetKey) { mutableStateOf<DrillResult?>(null) }
    var failed by remember(resetKey) { mutableStateOf(false) }
    var stopFlag by remember(resetKey) { mutableStateOf(false) }
    val level by scorer.level.collectAsStateWithLifecycle()
    Column(Modifier.fillMaxWidth().animateContentSize(), horizontalAlignment = Alignment.CenterHorizontally, verticalArrangement = Arrangement.spacedBy(8.dp)) {
        when (phase) {
            DrillPhase.IDLE -> FilledTonalButton({
                gate {
                    result = null; failed = false; stopFlag = false; phase = DrillPhase.RECORDING
                    scope.launch {
                        val r = scorer.listen(reference, maxMs, { stopFlag }, { phase = DrillPhase.SCORING })
                        phase = DrillPhase.IDLE
                        if (r == null) failed = true else { result = r; onResult(r) }
                    }
                }
            }) { Icon(Icons.Filled.Mic, null); Spacer(Modifier.width(6.dp)); Text(if (result == null) "Сказать" else "Ещё раз") }
            DrillPhase.RECORDING -> {
                Button({ stopFlag = true }, colors = ButtonDefaults.buttonColors(containerColor = MaterialTheme.colorScheme.error)) {
                    Icon(Icons.Filled.Stop, null); Spacer(Modifier.width(6.dp)); Text("Говорите… (стоп)")
                }
                LinearProgressIndicator(progress = { level }, Modifier.fillMaxWidth(0.6f))
            }
            DrillPhase.SCORING -> Row(verticalAlignment = Alignment.CenterVertically) {
                CircularProgressIndicator(Modifier.size(20.dp), strokeWidth = 2.dp); Spacer(Modifier.width(8.dp)); Text("Оцениваю…")
            }
        }
        result?.let { r ->
            val c = scoreColor(r.score)
            Text("%.0f".format(r.score), fontSize = 40.sp, fontWeight = FontWeight.Bold, color = c)
            Text(levelLabel(r.score), color = c, style = MaterialTheme.typography.titleSmall)
        }
        if (failed) Text("Не расслышал. Попробуйте ещё раз.", color = MaterialTheme.colorScheme.error, style = MaterialTheme.typography.bodySmall)
    }
}

// ---- Word drill (spaced repetition): listen -> say -> instant score -> again / hard / good / easy ---------
@Composable
fun WordDrillCard(w: SavedWord, vm: DictionaryViewModel) {
    var score by remember(w.id, w.dueAt) { mutableStateOf<Double?>(null) }
    val suggested = when { score == null -> -1; score!! >= 80 -> 2; score!! >= 60 -> 1; else -> 0 }
    Column(Modifier.fillMaxSize().verticalScroll(rememberScrollState()).padding(24.dp), horizontalAlignment = Alignment.CenterHorizontally,
        verticalArrangement = Arrangement.spacedBy(12.dp, Alignment.CenterVertically)) {
        Text(w.word, style = MaterialTheme.typography.displaySmall)
        if (w.ipa.isNotEmpty()) Text("/${w.ipa}/", style = MaterialTheme.typography.titleLarge, color = MaterialTheme.colorScheme.primary)
        Text(w.translationRu.ifEmpty { "—" }, style = MaterialTheme.typography.headlineSmall)
        FilledTonalButton({ vm.speak(w.word) }) { Icon(Icons.AutoMirrored.Filled.VolumeUp, null); Spacer(Modifier.width(6.dp)); Text("Послушать") }
        SayPanel(vm.scorer, w.word, w.id to w.dueAt, 5000) { score = it.score }
        Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            listOf("Снова", "Трудно", "Хорошо", "Легко").forEachIndexed { g, label ->
                if (g == suggested) Button({ vm.review(w, g) }) { Text(label) } else OutlinedButton({ vm.review(w, g) }) { Text(label) }
            }
        }
    }
}

// ---- Sound drill: articulation tips + minimal pairs ------------------------------------------------------
class SoundDrillViewModel(app: Application) : BaseVm(app) {
    val scorer = DrillScorer(ctx)
    private val _card = MutableStateFlow<JSONObject?>(null)
    val card: StateFlow<JSONObject?> = _card
    private val _missing = MutableStateFlow(false)
    val missing: StateFlow<Boolean> = _missing

    fun open(key: String) {
        viewModelScope.launch {
            val c = try { io { db -> (db.soundIdForIpa(key) ?: key).let { db.soundCard(it) } } } catch (e: Exception) { null }
            _card.value = c
            _missing.value = c == null
        }
    }
    fun speak(text: String) = ctx.tts.speak(text, 1f)
}

@Composable
fun SoundDrillScreen(key: String, vm: SoundDrillViewModel = viewModel()) {
    LaunchedEffect(key) { vm.open(key) }
    val card by vm.card.collectAsStateWithLifecycle()
    val missing by vm.missing.collectAsStateWithLifecycle()
    val c = card
    if (missing) { EmptyState(Icons.Filled.Hearing, "Для этого звука нет карточки", "Выберите звук на вкладке «Тренировка»"); return }
    if (c == null) { SkeletonList(3); return }
    val pairs = c.optJSONArray("minimal_pairs").objects()
    var idx by rememberSaveable(key) { mutableIntStateOf(0) }
    var done by remember(key) { mutableStateOf(0) }
    Column(Modifier.fillMaxSize().verticalScroll(rememberScrollState()).padding(16.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
        Text("/${c.optString("ipa")}/  ${c.optString("name_ru")}", style = MaterialTheme.typography.headlineSmall)
        val tips = c.optJSONArray("articulation_ru").texts()
        if (tips.isNotEmpty()) ElevatedCard(Modifier.fillMaxWidth()) {
            Column(Modifier.padding(16.dp), verticalArrangement = Arrangement.spacedBy(4.dp)) {
                Text("Как произносить", style = MaterialTheme.typography.titleMedium)
                tips.forEachIndexed { i, t -> Text("${i + 1}. $t", style = MaterialTheme.typography.bodyMedium) }
            }
        }
        if (pairs.isEmpty()) { Text("Минимальных пар для этого звука нет."); return@Column }
        val p = pairs[idx.coerceIn(0, pairs.size - 1)]
        val target = p.optString("target")
        val contrast = p.optString("contrast")
        val say = if (idx % 2 == 0) target else contrast
        SectionTitle("Пара ${idx + 1} из ${pairs.size}")
        ElevatedCard(Modifier.fillMaxWidth()) {
            Column(Modifier.padding(20.dp), horizontalAlignment = Alignment.CenterHorizontally, verticalArrangement = Arrangement.spacedBy(10.dp)) {
                Row(horizontalArrangement = Arrangement.spacedBy(16.dp), verticalAlignment = Alignment.CenterVertically) {
                    Text(target, fontSize = 28.sp, fontWeight = if (say == target) FontWeight.Bold else FontWeight.Normal)
                    Text("≠", fontSize = 22.sp)
                    Text(contrast, fontSize = 28.sp, fontWeight = if (say == contrast) FontWeight.Bold else FontWeight.Normal)
                }
                Text("Скажите: «$say»", style = MaterialTheme.typography.titleMedium)
                FilledTonalButton({ vm.speak(say) }) { Icon(Icons.AutoMirrored.Filled.VolumeUp, null); Spacer(Modifier.width(6.dp)); Text("Послушать") }
                SayPanel(vm.scorer, say, key to idx, 5000) { done++ }
            }
        }
        Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceBetween) {
            OutlinedButton({ idx = (idx - 1).coerceAtLeast(0) }, enabled = idx > 0) { Text("Назад") }
            Button({ idx = (idx + 1).coerceAtMost(pairs.size - 1) }, enabled = idx < pairs.size - 1) { Text("Следующая пара") }
        }
        if (done > 0) Text("Попыток в этой сессии: $done", style = MaterialTheme.typography.labelMedium, color = MaterialTheme.colorScheme.onSurfaceVariant)
    }
}

// ---- Shadowing: per sentence listen -> repeat -> score + coverage ------------------------------------------
@Composable
fun ShadowScreen(vm: PracticeViewModel) {
    val s by vm.state.collectAsStateWithLifecycle()
    if (s.text == null || s.sentences.isEmpty()) { EmptyState(Icons.Filled.RecordVoiceOver, "Нет текста", "Откройте текст в библиотеке и выберите «Повторять за диктором»"); return }
    var idx by rememberSaveable { mutableIntStateOf(0) }
    val results = remember { mutableStateMapOf<Int, DrillResult>() }
    DisposableEffect(Unit) { onDispose { vm.stopSpeaking() } }
    val sentence = s.sentences[idx.coerceIn(0, s.sentences.size - 1)]
    Column(Modifier.fillMaxSize().verticalScroll(rememberScrollState()).padding(16.dp), verticalArrangement = Arrangement.spacedBy(12.dp)) {
        LinearProgressIndicator(progress = { (idx + 1f) / s.sentences.size }, Modifier.fillMaxWidth())
        Text("Предложение ${idx + 1} из ${s.sentences.size}", style = MaterialTheme.typography.labelLarge)
        ElevatedCard(Modifier.fillMaxWidth()) {
            Text(sentence, Modifier.padding(20.dp), style = MaterialTheme.typography.titleLarge)
        }
        Row(horizontalArrangement = Arrangement.spacedBy(8.dp), modifier = Modifier.align(Alignment.CenterHorizontally)) {
            FilledTonalButton({ vm.speak(sentence) }) { Icon(Icons.AutoMirrored.Filled.VolumeUp, null); Spacer(Modifier.width(6.dp)); Text("Послушать") }
            OutlinedButton({ vm.stopSpeaking() }) { Text("Стоп") }
        }
        SayPanel(vm.scorer, sentence, idx, 20000) { r -> results[idx] = r; vm.shadowDone(sentence) }
        results[idx]?.let { Text("Покрытие: %.0f%%".format(it.coverage), style = MaterialTheme.typography.bodyMedium) }
        Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceBetween) {
            OutlinedButton({ idx = (idx - 1).coerceAtLeast(0) }, enabled = idx > 0) { Text("Назад") }
            Button({ idx = (idx + 1).coerceAtMost(s.sentences.size - 1) }, enabled = idx < s.sentences.size - 1) { Text("Дальше") }
        }
        if (results.isNotEmpty()) {
            val avg = results.values.map { it.score }.average()
            Text("Среднее по ${results.size} предл.: %.0f".format(avg), style = MaterialTheme.typography.titleMedium, color = scoreColor(avg))
        }
    }
}
