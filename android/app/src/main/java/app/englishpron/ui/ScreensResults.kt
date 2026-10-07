package app.englishpron.ui

import androidx.compose.foundation.background
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.VolumeUp
import androidx.compose.material.icons.filled.Close
import androidx.compose.material.icons.filled.PlayArrow
import androidx.compose.material3.*
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.AnnotatedString
import androidx.compose.ui.text.LinkAnnotation
import androidx.compose.ui.text.SpanStyle
import androidx.compose.ui.text.buildAnnotatedString
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextDecoration
import androidx.compose.ui.text.withLink
import androidx.compose.ui.text.withStyle
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import app.englishpron.data.AdviceItem
import app.englishpron.data.AssessmentUi
import app.englishpron.data.AttemptItem
import app.englishpron.data.TextReading
import app.englishpron.data.WordResult
import app.englishpron.data.parseColor
import app.englishpron.engine.JobUi
import app.englishpron.engine.WordState
import app.englishpron.engine.formatMmSs
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale

private val Green = Color(0xFF2E7D32)
private val Amber = Color(0xFFF9A825)
private val Red = Color(0xFFC62828)
private val Orange = Color(0xFFEF6C00)

fun scoreColor(score: Double?, substituted: Boolean = false): Color = when {
    score == null -> Color.Gray
    substituted -> Red
    score >= 80 -> Green
    score >= 60 -> Amber
    else -> Red
}

/** Seconds left for a job: engine ETA, else the estimate (scaled by progress), else null. */
fun remainingSec(j: JobUi): Double? = when {
    j.running && j.etaSec >= 0 -> j.etaSec
    j.running && j.estimateSec >= 0 -> j.estimateSec * (1.0 - j.progress)
    !j.running && j.estimateSec >= 0 -> j.estimateSec
    else -> null
}

private fun stageLabel(stage: String) = when (stage) {
    "vad" -> "поиск речи"
    "asr" -> "распознавание"
    "phoneme" -> "анализ звуков"
    "assess" -> "оценка"
    else -> ""
}

/** "42% · осталось 0:35" for the running job, "в очереди · ≈ 0:45" for a waiting one. */
fun jobLine(j: JobUi): String {
    val rem = remainingSec(j)
    return if (j.running) {
        "${(j.progress * 100).toInt()}%" + (rem?.let { " · осталось ${formatMmSs(it)}" } ?: " · считаю…") +
            stageLabel(j.stage).let { if (it.isEmpty()) "" else " · $it" }
    } else "в очереди" + (rem?.let { " · ≈ ${formatMmSs(it)}" } ?: "")
}

@Composable
fun Legend(c: Color, label: String) = Row(verticalAlignment = Alignment.CenterVertically) {
    Box(Modifier.size(12.dp).clip(RoundedCornerShape(3.dp)).background(c))
    Spacer(Modifier.width(4.dp)); Text(label, style = MaterialTheme.typography.labelSmall)
}

/**
 * The text body with per-word marks: the full assessment colours every word by score band (tap = details);
 * words without a full result yet (still queued, other paragraphs) show the quick live marks (read / skipped).
 * Returns null when there is nothing to mark.
 */
fun markedBody(
    body: String, reading: TextReading?, selected: WordResult?,
    onScored: (WordResult) -> Unit, onPlain: (String) -> Unit,
): AnnotatedString? {
    val r = reading ?: return null
    val result = r.result
    val live = r.live
    if (result == null && live == null) return null
    class Mark(val b: Int, val e: Int, val scored: WordResult?, val state: WordState?, val idx: Int)
    val marks = ArrayList<Mark>()
    val covered = BooleanArray(body.length + 1)
    result?.words?.forEach { w ->
        if (w.u16Begin >= 0 && w.u16End >= w.u16Begin && w.u16End <= body.length) {
            marks += Mark(w.u16Begin, w.u16End, w, null, w.index)
            for (k in w.u16Begin until w.u16End) covered[k] = true
        }
    }
    live?.words?.forEach { w ->
        if (w.u16Begin >= 0 && w.u16End >= w.u16Begin && w.u16End <= body.length && !covered[w.u16Begin]) marks += Mark(w.u16Begin, w.u16End, null, w.state, w.index)
    }
    marks.sortBy { it.b }
    return buildAnnotatedString {
        var last = 0
        for (m in marks) {
            if (m.b < last) continue
            append(body.substring(last, m.b))
            val sc = m.scored
            if (sc != null) {
                val c = parseColor(sc.colorHex)
                withLink(LinkAnnotation.Clickable("w${m.idx}") { onScored(sc) }) {
                    withStyle(SpanStyle(background = c.copy(alpha = if (sc.index == selected?.index) 0.7f else 0.3f), fontWeight = FontWeight.Medium)) {
                        append(body.substring(m.b, m.e))
                    }
                }
            } else {
                val word = body.substring(m.b, m.e)
                val style = when (m.state) {
                    WordState.READ -> SpanStyle(background = Green.copy(alpha = 0.18f))
                    WordState.SKIPPED -> SpanStyle(color = Orange, textDecoration = TextDecoration.Underline)
                    else -> SpanStyle()
                }
                withLink(LinkAnnotation.Clickable("l${m.idx}") { onPlain(word) }) { withStyle(style) { append(word) } }
            }
            last = m.e
        }
        append(body.substring(last))
    }
}

/** Status / score card above the text. */
@Composable
fun ReadingStatusCard(reading: TextReading, job: JobUi?, coverage: Double? = null) {
    val r = reading.result
    when {
        r != null -> Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
            ScoreCard(r, coverage)
            if (reading.pending) Column(verticalArrangement = Arrangement.spacedBy(4.dp)) {
                if (job != null && job.running && job.progress > 0f) LinearProgressIndicator(progress = { job.progress }, Modifier.fillMaxWidth())
                else LinearProgressIndicator(Modifier.fillMaxWidth())
                Text("Остальные абзацы ещё считаются: " + (job?.let { it.label + " · " + jobLine(it) } ?: "в очереди"), style = MaterialTheme.typography.bodySmall)
            }
        }
        reading.pending -> Card(Modifier.fillMaxWidth(), colors = CardDefaults.cardColors(containerColor = MaterialTheme.colorScheme.secondaryContainer)) {
            Column(Modifier.padding(12.dp), verticalArrangement = Arrangement.spacedBy(6.dp)) {
                Text("Оценка готовится…", fontWeight = FontWeight.Medium)
                if (job != null && job.running && job.progress > 0f) LinearProgressIndicator(progress = { job.progress }, Modifier.fillMaxWidth())
                else LinearProgressIndicator(Modifier.fillMaxWidth())
                Text(job?.let { jobLine(it) } ?: "в очереди", style = MaterialTheme.typography.bodySmall)
                Text("Текст уже отмечен прочитанным. Пока можно читать другие тексты.", style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSecondaryContainer)
            }
        }
        reading.failed -> Text("Оценку посчитать не удалось, запись сохранена.", color = MaterialTheme.colorScheme.error)
        else -> Text("Оценка отменена.", style = MaterialTheme.typography.bodySmall, color = MaterialTheme.colorScheme.onSurfaceVariant)
    }
}

@OptIn(ExperimentalLayoutApi::class)
@Composable
private fun ScoreCard(r: AssessmentUi, coverage: Double?) {
    Card(Modifier.fillMaxWidth()) {
        Column(Modifier.padding(16.dp), horizontalAlignment = Alignment.CenterHorizontally) {
            Text("%.0f".format(r.overall), fontSize = 48.sp, fontWeight = FontWeight.Bold)
            Text(levelLabel(r.overall), style = MaterialTheme.typography.titleMedium)
            Spacer(Modifier.height(8.dp))
            Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceAround) {
                Mini("Точность", r.accuracy); Mini("Полнота", r.completeness); Mini("Беглость", r.fluency)
                if (coverage != null) Mini("Покрытие %", coverage)
            }
            if (r.wpm > 0) Text("Темп: %.0f слов/мин, длинных пауз: ${r.longPauses}".format(r.wpm), style = MaterialTheme.typography.labelMedium)
            if (!r.phonemeLevel) Text("Оценка отдельных звуков недоступна: фонемная модель не подключена.",
                style = MaterialTheme.typography.bodySmall, color = MaterialTheme.colorScheme.onSurfaceVariant)
            r.warnings.forEach { Text(it, style = MaterialTheme.typography.bodySmall, color = MaterialTheme.colorScheme.tertiary) }
            Spacer(Modifier.height(8.dp))
            FlowRow(horizontalArrangement = Arrangement.spacedBy(12.dp)) {
                Legend(Green, "хорошо"); Legend(Amber, "средне"); Legend(Red, "слабо"); Legend(Color(0xFF9E9E9E), "пропущено")
            }
            Text("Нажмите на слово, чтобы увидеть разбор.", style = MaterialTheme.typography.bodySmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant)
        }
    }
}

@Composable
private fun Mini(label: String, v: Double) = Column(horizontalAlignment = Alignment.CenterHorizontally) {
    Text("%.0f".format(v), style = MaterialTheme.typography.titleLarge)
    Text(label, style = MaterialTheme.typography.labelMedium)
}

@Composable
fun AdviceList(advice: List<AdviceItem>) {
    if (advice.isEmpty()) return
    SectionTitle("Над чем поработать")
    Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
        advice.forEach { a ->
            Card(Modifier.fillMaxWidth(), colors = CardDefaults.cardColors(containerColor = MaterialTheme.colorScheme.secondaryContainer)) {
                Column(Modifier.padding(12.dp)) {
                    Text("${a.titleRu}  (/${a.expectedIpa}/" + (if (a.actualIpa.isNotEmpty()) " → /${a.actualIpa}/" else "") + ", ×${a.count})",
                        fontWeight = FontWeight.Medium)
                    Text(a.tipRu, style = MaterialTheme.typography.bodyMedium)
                }
            }
        }
    }
}

/** History of attempts of one text (newest first). */
@Composable
fun AttemptsList(attempts: List<AttemptItem>, onPlay: (AttemptItem) -> Unit) {
    val fmt = SimpleDateFormat("dd.MM HH:mm", Locale.getDefault())
    Column {
        attempts.forEach { a ->
            Row(Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically) {
                Column(Modifier.weight(1f)) {
                    Text(fmt.format(Date(a.createdAt * 1000)) + (if (a.kind == "sentence") " · предложение" else "") +
                        " · " + formatMmSs(a.durationMs / 1000.0), style = MaterialTheme.typography.bodyMedium)
                    val (txt, col) = when {
                        a.score != null -> "оценка %.0f".format(a.score) to scoreColor(a.score)
                        a.jobStatus == "queued" || a.jobStatus == "processing" -> "оценка готовится…" to MaterialTheme.colorScheme.primary
                        a.jobStatus == "failed" -> "ошибка оценки" to MaterialTheme.colorScheme.error
                        a.jobStatus == "cancelled" -> "оценка отменена" to MaterialTheme.colorScheme.onSurfaceVariant
                        else -> "без оценки" to MaterialTheme.colorScheme.onSurfaceVariant
                    }
                    Text(txt, style = MaterialTheme.typography.labelMedium, color = col)
                }
                if (a.wavPath.isNotEmpty()) IconButton({ onPlay(a) }) { Icon(Icons.Filled.PlayArrow, "Прослушать") }
            }
        }
    }
}

/** Bottom sheet for a scored word. */
@OptIn(ExperimentalMaterial3Api::class, ExperimentalLayoutApi::class)
@Composable
fun WordScoreSheet(w: WordResult, advice: List<AdviceItem>, onDismiss: () -> Unit, onReference: () -> Unit, onMine: () -> Unit) {
    ModalBottomSheet(onDismissRequest = onDismiss) {
        Column(Modifier.fillMaxWidth().padding(horizontal = 24.dp).padding(bottom = 32.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
            Row(verticalAlignment = Alignment.Bottom) {
                Text(w.text, style = MaterialTheme.typography.headlineMedium)
                Spacer(Modifier.width(10.dp))
                if (w.expectedIpa.isNotEmpty()) Text("/${w.expectedIpa}/", style = MaterialTheme.typography.titleMedium, color = MaterialTheme.colorScheme.primary)
            }
            val omitted = w.status == "omitted" || w.recognized.isBlank()
            Text(if (omitted) "Вы сказали: пропущено" else "Вы сказали: ${w.recognized}", style = MaterialTheme.typography.titleMedium)
            Row(verticalAlignment = Alignment.CenterVertically) {
                Box(Modifier.size(14.dp).clip(RoundedCornerShape(7.dp)).background(parseColor(w.colorHex)))
                Spacer(Modifier.width(8.dp))
                Text("Оценка: %.0f".format(w.score), style = MaterialTheme.typography.titleMedium)
            }
            if (w.phonemes.isNotEmpty()) {
                Text("Звуки: ожидалось → услышано", style = MaterialTheme.typography.labelLarge)
                Column(verticalArrangement = Arrangement.spacedBy(4.dp)) {
                    w.phonemes.forEach { p ->
                        val c = scoreColor(p.score, p.substituted)
                        val heard = if (p.substituted && p.actualIpa.isNotBlank()) p.actualIpa else if (p.score == null) "—" else p.ipa
                        Row(Modifier.fillMaxWidth().clip(RoundedCornerShape(8.dp)).background(c.copy(alpha = 0.18f)).padding(horizontal = 10.dp, vertical = 6.dp),
                            verticalAlignment = Alignment.CenterVertically) {
                            Text("/${p.ipa}/  →  /$heard/", Modifier.weight(1f), fontSize = 16.sp)
                            Text(p.score?.let { "%.0f".format(it) } ?: "—", fontWeight = FontWeight.Bold, color = c)
                        }
                    }
                }
            }
            val ids = w.phonemes.mapNotNull { it.adviceId }.toSet()
            advice.filter { a -> w.index in a.words || a.id in ids }.forEach { a ->
                Text("${a.titleRu}: ${a.tipRu}", style = MaterialTheme.typography.bodyMedium)
            }
            Row(horizontalArrangement = Arrangement.spacedBy(12.dp), modifier = Modifier.padding(top = 4.dp)) {
                OutlinedButton(onReference) { Icon(Icons.AutoMirrored.Filled.VolumeUp, null); Spacer(Modifier.width(6.dp)); Text("Эталон") }
                OutlinedButton(onMine, enabled = w.startSec >= 0 && w.endSec > w.startSec) {
                    Icon(Icons.Filled.PlayArrow, null); Spacer(Modifier.width(6.dp)); Text("Моя запись")
                }
            }
        }
    }
}

/** Top bar indicator: visible while the queue is non-empty; opens the "Обработка" sheet. */
@Composable
fun QueueIndicator(jobs: List<JobUi>, onClick: () -> Unit) {
    if (jobs.isEmpty()) return
    val first = jobs.first()
    IconButton(onClick) {
        Box(Modifier.size(28.dp), Alignment.Center) {
            if (first.running && first.progress > 0f) CircularProgressIndicator(progress = { first.progress }, Modifier.fillMaxSize(), strokeWidth = 2.5.dp)
            else CircularProgressIndicator(Modifier.fillMaxSize(), strokeWidth = 2.5.dp)
            Text("${jobs.size}", style = MaterialTheme.typography.labelSmall)
        }
    }
}

/** "Обработка": current job, progress, remaining time, waiting jobs with estimates, cancel buttons. */
@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun ProcessingSheet(jobs: List<JobUi>, onCancel: (Long) -> Unit, onDismiss: () -> Unit) {
    LaunchedEffect(jobs.isEmpty()) { if (jobs.isEmpty()) onDismiss() }
    ModalBottomSheet(onDismissRequest = onDismiss) {
        Column(Modifier.fillMaxWidth().padding(horizontal = 24.dp).padding(bottom = 32.dp), verticalArrangement = Arrangement.spacedBy(10.dp)) {
            Text("Обработка", style = MaterialTheme.typography.titleLarge)
            if (jobs.isEmpty()) Text("Очередь пуста", color = MaterialTheme.colorScheme.onSurfaceVariant)
            jobs.forEachIndexed { i, j ->
                if (i == 0 && j.running) {
                    Column(verticalArrangement = Arrangement.spacedBy(6.dp)) {
                        Row(verticalAlignment = Alignment.CenterVertically) {
                            Text(j.label, Modifier.weight(1f), style = MaterialTheme.typography.titleMedium)
                            IconButton({ onCancel(j.id) }) { Icon(Icons.Filled.Close, "Отменить") }
                        }
                        if (j.progress > 0f) LinearProgressIndicator(progress = { j.progress }, Modifier.fillMaxWidth())
                        else LinearProgressIndicator(Modifier.fillMaxWidth())
                        Text(jobLine(j), style = MaterialTheme.typography.bodyMedium)
                    }
                    if (jobs.size > 1) SectionTitle("В очереди")
                } else {
                    if (i == 0) SectionTitle("В очереди")
                    Row(verticalAlignment = Alignment.CenterVertically) {
                        Column(Modifier.weight(1f)) {
                            Text(j.label, style = MaterialTheme.typography.bodyLarge)
                            Text(jobLine(j), style = MaterialTheme.typography.labelMedium, color = MaterialTheme.colorScheme.onSurfaceVariant)
                        }
                        IconButton({ onCancel(j.id) }) { Icon(Icons.Filled.Close, "Отменить") }
                    }
                }
            }
        }
    }
}
