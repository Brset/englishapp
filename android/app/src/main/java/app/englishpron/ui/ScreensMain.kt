package app.englishpron.ui

import androidx.compose.animation.animateContentSize
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import app.englishpron.data.levelFor
import java.util.Calendar
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.VolumeUp
import androidx.compose.material.icons.filled.*
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.LinkAnnotation
import androidx.compose.ui.text.SpanStyle
import androidx.compose.ui.text.TextLinkStyles
import androidx.compose.ui.text.buildAnnotatedString
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextDecoration
import androidx.compose.ui.text.withLink
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import androidx.lifecycle.viewmodel.compose.viewModel
import app.englishpron.data.TextItem

// ---- Home -----------------------------------------------------------------------------------
@OptIn(ExperimentalLayoutApi::class)
@Composable
fun HomeScreen(onOpenText: (String) -> Unit, onDictionary: () -> Unit, onDrill: (String) -> Unit, vm: HomeViewModel = viewModel()) {
    val st by vm.state.collectAsStateWithLifecycle()
    LaunchedEffect(Unit) { vm.refresh() }
    val d = st.data
    if (d == null) {
        if (st.loading) SkeletonList(4) else EmptyState(Icons.Filled.Home, "Пока нет данных", "Откройте «Библиотеку» и выберите первый текст")
        return
    }
    val p = d.progress
    val lvl = levelFor(d.xp)
    val hour = Calendar.getInstance().get(Calendar.HOUR_OF_DAY)
    val greeting = when { hour < 5 -> "Доброй ночи"; hour < 12 -> "Доброе утро"; hour < 18 -> "Добрый день"; else -> "Добрый вечер" }
    Column(Modifier.fillMaxSize().verticalScroll(rememberScrollState()).padding(16.dp), verticalArrangement = Arrangement.spacedBy(12.dp)) {
        Row(verticalAlignment = Alignment.CenterVertically) {
            Column(Modifier.weight(1f)) {
                Text(greeting, style = MaterialTheme.typography.headlineSmall)
                Text("${lvl.name} · ${d.xp} XP", style = MaterialTheme.typography.bodyMedium, color = MaterialTheme.colorScheme.onSurfaceVariant)
            }
            Row(Modifier.clip(RoundedCornerShape(20.dp)).background(MaterialTheme.colorScheme.tertiaryContainer).padding(horizontal = 12.dp, vertical = 6.dp),
                verticalAlignment = Alignment.CenterVertically) {
                Icon(Icons.Filled.LocalFireDepartment, "Серия", tint = MaterialTheme.colorScheme.tertiary)
                Spacer(Modifier.width(4.dp))
                Text("${p.streakDays}", style = MaterialTheme.typography.titleMedium, color = MaterialTheme.colorScheme.onTertiaryContainer)
            }
        }
        ElevatedCard(Modifier.fillMaxWidth()) {
            Row(Modifier.padding(16.dp), verticalAlignment = Alignment.CenterVertically) {
                ProgressRing((p.minutesToday / d.goalMinutes.coerceAtLeast(1)).toFloat(), Modifier.size(84.dp), 9.dp,
                    color = if (p.minutesToday >= d.goalMinutes) Color(0xFF2E7D32) else MaterialTheme.colorScheme.primary) {
                    Text("%.0f".format(p.minutesToday), style = MaterialTheme.typography.titleLarge, fontWeight = FontWeight.Bold)
                }
                Spacer(Modifier.width(16.dp))
                Column(Modifier.weight(1f), verticalArrangement = Arrangement.spacedBy(4.dp)) {
                    Text("Цель дня: ${d.goalMinutes} мин", style = MaterialTheme.typography.titleMedium)
                    Text(if (p.minutesToday >= d.goalMinutes) "Цель выполнена!" else "Осталось %.0f мин".format((d.goalMinutes - p.minutesToday).coerceAtLeast(0.0)),
                        style = MaterialTheme.typography.bodyMedium, color = MaterialTheme.colorScheme.onSurfaceVariant)
                    lvl.next?.let { n ->
                        LinearProgressIndicator(progress = { ((d.xp - lvl.floor).toFloat() / (n - lvl.floor)).coerceIn(0f, 1f) },
                            Modifier.fillMaxWidth().clip(RoundedCornerShape(4.dp)))
                        Text("до следующего уровня ${n - d.xp} XP", style = MaterialTheme.typography.labelSmall)
                    }
                }
            }
        }
        d.cont?.let { c ->
            ElevatedCard(Modifier.fillMaxWidth().clickable { onOpenText(c.item.id) }) {
                Row(Modifier.padding(16.dp), verticalAlignment = Alignment.CenterVertically) {
                    Column(Modifier.weight(1f), verticalArrangement = Arrangement.spacedBy(2.dp)) {
                        Text("Продолжить чтение", style = MaterialTheme.typography.labelLarge, color = MaterialTheme.colorScheme.primary)
                        Text(c.item.titleEn, style = MaterialTheme.typography.titleMedium)
                        Text(if (c.paragraphCount > 1) "Абзац ${(c.paragraphIndex + 1).coerceAtMost(c.paragraphCount)}/${c.paragraphCount} · прочитано %.0f%%".format(c.coverage)
                            else "Прочитано %.0f%%".format(c.coverage), style = MaterialTheme.typography.bodyMedium, color = MaterialTheme.colorScheme.onSurfaceVariant)
                    }
                    ProgressRing((c.coverage / 100).toFloat(), Modifier.size(52.dp), 5.dp) { Icon(Icons.Filled.PlayArrow, null) }
                }
            }
        }
        d.textOfDay?.let { t ->
            SectionTitle("Текст дня")
            TextCard(t) { onOpenText(t.id) }
        }
        if (d.dueCount > 0) {
            ElevatedCard(Modifier.fillMaxWidth().clickable(onClick = onDictionary)) {
                Row(Modifier.padding(16.dp), verticalAlignment = Alignment.CenterVertically) {
                    Icon(Icons.Filled.Book, null)
                    Text("  Слов к повторению: ${d.dueCount}", style = MaterialTheme.typography.titleMedium)
                }
            }
        }
        SectionTitle("Слабые звуки")
        if (p.weakSounds.isEmpty()) Text("Данных пока мало — прочитайте несколько текстов.", color = MaterialTheme.colorScheme.onSurfaceVariant)
        else FlowRow(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            p.weakSounds.take(3).forEach { (ipa, _, avg) ->
                AssistChip(onClick = { onDrill(ipa) }, label = { Text("/$ipa/ · %.0f".format(avg)) },
                    colors = AssistChipDefaults.assistChipColors(labelColor = scoreColor(avg)))
            }
        }
        SectionTitle("Минуты за неделю")
        ElevatedCard(Modifier.fillMaxWidth()) { BarChart(d.weekly, Modifier.padding(16.dp)) }
        Spacer(Modifier.height(8.dp))
    }
}

@Composable
private fun TextCard(t: TextItem, onHistory: (() -> Unit)? = null, onClick: () -> Unit) = ElevatedCard(Modifier.fillMaxWidth().clickable(onClick = onClick).animateContentSize()) {
    Row(Modifier.padding(16.dp), verticalAlignment = Alignment.Top) {
        Column(Modifier.weight(1f)) {
            Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                AssistChip(onClick = onClick, label = { Text(t.level) })
                Text(t.genre, style = MaterialTheme.typography.labelMedium)
                CoverageBadge(t.bestCoverage ?: if (t.status == "done") 100.0 else null)
            }
            Text(t.titleEn, style = MaterialTheme.typography.titleMedium)
            Text(t.titleRu, style = MaterialTheme.typography.bodyMedium, color = MaterialTheme.colorScheme.onSurfaceVariant)
            Text(t.descriptionRu, style = MaterialTheme.typography.bodySmall, maxLines = 2)
            Text("${t.wordCount} слов" + (t.bestScore?.let { " · лучший результат %.0f".format(it) } ?: ""),
                style = MaterialTheme.typography.labelSmall)
            if (t.pending) Row(verticalAlignment = Alignment.CenterVertically, modifier = Modifier.padding(top = 6.dp)) {
                CircularProgressIndicator(Modifier.size(14.dp), strokeWidth = 2.dp)
                Spacer(Modifier.width(8.dp))
                Text("оценка готовится…", style = MaterialTheme.typography.labelLarge, color = MaterialTheme.colorScheme.primary)
            } else t.lastScore?.let {
                Text("Оценка: %.0f · %s".format(it, levelLabel(it)), style = MaterialTheme.typography.labelLarge,
                    color = scoreColor(it), modifier = Modifier.padding(top = 6.dp))
            }
            if (onHistory != null && t.status != "new") TextButton(onHistory, contentPadding = PaddingValues(0.dp)) { Text("История попыток") }
        }
        val cov = t.bestCoverage ?: if (t.status == "done") 100.0 else 0.0
        Spacer(Modifier.width(12.dp))
        ProgressRing((cov / 100).toFloat(), Modifier.size(56.dp), 6.dp,
            color = if (cov >= 90) Color(0xFF2E7D32) else MaterialTheme.colorScheme.primary) {
            Text("%.0f".format(cov), style = MaterialTheme.typography.labelLarge, fontWeight = FontWeight.Bold)
        }
    }
}

// ---- Library --------------------------------------------------------------------------------
@OptIn(ExperimentalMaterial3Api::class, ExperimentalLayoutApi::class)
@Composable
fun LibraryScreen(onOpen: (String) -> Unit, vm: LibraryViewModel = viewModel()) {
    val s by vm.state.collectAsStateWithLifecycle()
    val level by vm.level.collectAsStateWithLifecycle()
    val genre by vm.genre.collectAsStateWithLifecycle()
    val query by vm.query.collectAsStateWithLifecycle()
    val history by vm.history.collectAsStateWithLifecycle()
    var historyFor by remember { mutableStateOf<TextItem?>(null) }
    LaunchedEffect(Unit) { vm.refresh() }
    historyFor?.let { t ->
        LaunchedEffect(t.id, s.items) { vm.loadHistory(t.id) }
        ModalBottomSheet(onDismissRequest = { historyFor = null; vm.loadHistory(null) }) {
            Column(Modifier.fillMaxWidth().padding(horizontal = 24.dp).padding(bottom = 32.dp)) {
                Text(t.titleEn, style = MaterialTheme.typography.titleLarge)
                if (history.isEmpty()) Text("Попыток пока нет", color = MaterialTheme.colorScheme.onSurfaceVariant)
                else AttemptsList(history, vm::playAttempt)
            }
        }
    }
    Column(Modifier.fillMaxSize()) {
        OutlinedTextField(
            value = query, onValueChange = { vm.query.value = it }, singleLine = true,
            leadingIcon = { Icon(Icons.Filled.Search, null) }, placeholder = { Text("Поиск по текстам") },
            trailingIcon = { if (query.isNotEmpty()) IconButton({ vm.query.value = "" }) { Icon(Icons.Filled.Clear, "Очистить") } },
            modifier = Modifier.fillMaxWidth().padding(horizontal = 16.dp, vertical = 8.dp),
        )
        FlowRow(Modifier.padding(horizontal = 16.dp), horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            FilterChip(level == null, { vm.level.value = null }, { Text("Все уровни") })
            s.levels.forEach { l -> FilterChip(level == l, { vm.level.value = if (level == l) null else l }, { Text(l) }) }
        }
        FlowRow(Modifier.padding(horizontal = 16.dp), horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            FilterChip(genre == null, { vm.genre.value = null }, { Text("Все жанры") })
            s.genres.forEach { g -> FilterChip(genre == g, { vm.genre.value = if (genre == g) null else g }, { Text(g) }) }
        }
        if (s.items.isEmpty()) EmptyState(Icons.Filled.SearchOff, "Ничего не найдено", "Измените запрос или сбросьте фильтры")
        else LazyColumn(contentPadding = PaddingValues(16.dp), verticalArrangement = Arrangement.spacedBy(10.dp)) {
            items(s.items, key = { it.id }) { t -> TextCard(t, onHistory = { historyFor = t }) { onOpen(t.id) } }
        }
    }
}

// ---- Reading --------------------------------------------------------------------------------
@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun ReadingScreen(vm: PracticeViewModel, textId: String, onRecord: () -> Unit, onShadow: () -> Unit) {
    val s by vm.state.collectAsStateWithLifecycle()
    val jobs by vm.jobs.collectAsStateWithLifecycle()
    LaunchedEffect(textId) { vm.load(textId) }
    DisposableEffect(Unit) { onDispose { vm.stopSpeaking() } }
    val settings by vm.settings.collectAsStateWithLifecycle()
    val bodyStyle = settings.readingStyle()
    val text = s.text
    if (text == null || text.item.id != textId) { SkeletonList(3); return }
    val linkStyle = TextLinkStyles(SpanStyle(color = MaterialTheme.colorScheme.onSurface, textDecoration = TextDecoration.Underline))

    Column(Modifier.fillMaxSize()) {
        Column(Modifier.weight(1f).verticalScroll(rememberScrollState()).padding(16.dp)) {
            Text(text.item.titleEn, style = MaterialTheme.typography.headlineSmall)
            Text(text.item.titleRu, color = MaterialTheme.colorScheme.onSurfaceVariant)
            Spacer(Modifier.height(12.dp))
            val reading = s.reading
            if (reading != null) {
                val job = jobs.filter { it.textId == textId }.let { l -> l.firstOrNull { it.running } ?: l.firstOrNull() }
                ReadingStatusCard(reading, job, text.item.bestCoverage)
                Spacer(Modifier.height(12.dp))
            }
            Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                CoverageBadge(text.item.bestCoverage ?: if (text.item.status == "done") 100.0 else null)
                text.item.bestCoverage?.let { Text("лучшее покрытие %.0f%%".format(it), style = MaterialTheme.typography.labelMedium) }
            }
            val pos = s.position
            if (pos != null && (text.item.bestCoverage ?: 0.0) < 90.0) {
                val label = if (pos.wordIndex > 0) "слово ${pos.wordIndex + 1}" else "абзац ${(pos.paragraphIndex + 1).coerceAtMost(s.paragraphs.size.coerceAtLeast(1))}/${s.paragraphs.size}"
                FilledTonalButton({ vm.resume(); onRecord() }, Modifier.padding(top = 8.dp)) {
                    Icon(Icons.Filled.PlayArrow, null); Spacer(Modifier.width(6.dp)); Text("Продолжить с места ($label)")
                }
            }
            if (s.sentences.isNotEmpty()) TextButton(onShadow, contentPadding = PaddingValues(0.dp)) {
                Icon(Icons.Filled.RecordVoiceOver, null); Spacer(Modifier.width(6.dp)); Text("Повторять за диктором")
            }
            Spacer(Modifier.height(8.dp))
            s.error?.let { Text(it, color = MaterialTheme.colorScheme.error, modifier = Modifier.padding(bottom = 8.dp)) }
            val marked = remember(text.body, reading, s.wordSheet) {
                markedBody(text.body, reading, s.wordSheet, onScored = { vm.selectWord(it) }, onPlain = { vm.showWord(it) })
            }
            if (marked != null) {
                Text(marked, style = bodyStyle, modifier = Modifier.padding(bottom = 12.dp))
            } else text.body.split("\n\n").forEach { para ->
                val annotated = buildAnnotatedString {
                    var last = 0
                    for (m in Regex("[\\p{L}\\p{N}]+(?:['’-][\\p{L}\\p{N}]+)*").findAll(para)) {
                        append(para.substring(last, m.range.first))
                        val w = m.value
                        withLink(LinkAnnotation.Clickable(w, linkStyle) { vm.showWord(w) }) { append(w) }
                        last = m.range.last + 1
                    }
                    append(para.substring(last))
                }
                Text(annotated, style = bodyStyle, modifier = Modifier.padding(bottom = 12.dp))
            }
            s.reading?.result?.let { AdviceList(it.advice) }
            if (s.attempts.isNotEmpty()) {
                SectionTitle("Попытки")
                AttemptsList(s.attempts, vm::playAttempt)
            }
            if (s.focus.isNotEmpty()) {
                SectionTitle("Звуки этого текста")
                s.focus.forEach { f ->
                    Text("/${f.sound}/ — ${f.tipRu}", style = MaterialTheme.typography.bodyMedium)
                    if (f.examples.isNotEmpty()) Text(f.examples.joinToString(", "), style = MaterialTheme.typography.labelMedium)
                    Spacer(Modifier.height(6.dp))
                }
            }
        }
        Surface(tonalElevation = 3.dp) {
            Column(Modifier.fillMaxWidth().padding(horizontal = 16.dp, vertical = 8.dp)) {
                Row(verticalAlignment = Alignment.CenterVertically) {
                    Text("Скорость ×%.1f".format(s.speed), Modifier.width(110.dp))
                    Slider(s.speed, vm::setSpeed, valueRange = 0.5f..1.5f, steps = 9, modifier = Modifier.weight(1f))
                }
                Row(horizontalArrangement = Arrangement.spacedBy(12.dp), modifier = Modifier.fillMaxWidth()) {
                    OutlinedButton({ vm.speak(text.body) }, Modifier.weight(1f)) {
                        Icon(Icons.AutoMirrored.Filled.VolumeUp, null); Spacer(Modifier.width(6.dp)); Text("Слушать")
                    }
                    Button(onRecord, Modifier.weight(1f)) {
                        Icon(Icons.Filled.Mic, null); Spacer(Modifier.width(6.dp)); Text("Читать вслух")
                    }
                }
            }
        }
    }

    s.celebration?.let { c ->
        CelebrationOverlay(c, s.reading?.result?.overall, onDismiss = vm::dismissCelebration)
    }

    s.wordSheet?.let { w ->
        WordScoreSheet(w, s.reading?.result?.advice.orEmpty(), onDismiss = { vm.selectWord(null); vm.stopSpeaking() },
            onReference = { vm.playReference(w) }, onMine = { vm.playMyWord(w) })
    }

    s.wordInfo?.let { w ->
        ModalBottomSheet(onDismissRequest = vm::dismissWord) {
            Column(Modifier.fillMaxWidth().padding(horizontal = 24.dp).padding(bottom = 32.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
                Text(w.word, style = MaterialTheme.typography.headlineMedium)
                Text(if (w.ipa.isNotEmpty()) "/${w.ipa}/" else "транскрипция не найдена", style = MaterialTheme.typography.titleMedium,
                    color = MaterialTheme.colorScheme.primary)
                Text(w.translation.ifEmpty { "перевод недоступен офлайн" }, style = MaterialTheme.typography.bodyLarge)
                Row(horizontalArrangement = Arrangement.spacedBy(12.dp)) {
                    OutlinedButton({ vm.speak(w.word) }) {
                        Icon(Icons.AutoMirrored.Filled.VolumeUp, null); Spacer(Modifier.width(6.dp)); Text("Озвучить")
                    }
                    FilledTonalButton({ vm.saveCurrentWord() }, enabled = !w.saved) {
                        Icon(if (w.saved) Icons.Filled.Check else Icons.Filled.Add, null); Spacer(Modifier.width(6.dp))
                        Text(if (w.saved) "В словаре" else "В словарь")
                    }
                }
            }
        }
    }
}
