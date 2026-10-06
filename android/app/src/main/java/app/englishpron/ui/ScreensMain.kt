package app.englishpron.ui

import androidx.compose.foundation.clickable
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
@Composable
fun HomeScreen(onOpenText: (String) -> Unit, onDictionary: () -> Unit, vm: HomeViewModel = viewModel()) {
    val s by vm.state.collectAsStateWithLifecycle()
    LaunchedEffect(Unit) { vm.refresh() }
    Column(Modifier.fillMaxSize().verticalScroll(rememberScrollState()).padding(16.dp), verticalArrangement = Arrangement.spacedBy(12.dp)) {
        Text("Тренируйте английское произношение", style = MaterialTheme.typography.headlineSmall)
        s.progress?.let { p ->
            Card(Modifier.fillMaxWidth()) {
                Row(Modifier.padding(16.dp).fillMaxWidth(), horizontalArrangement = Arrangement.SpaceAround) {
                    Stat("${p.streakDays}", "дней подряд")
                    Stat("%.0f".format(p.minutesToday), "мин сегодня")
                    Stat("${p.textsDone}", "текстов готово")
                }
            }
        }
        if (s.dueCount > 0) {
            ElevatedCard(Modifier.fillMaxWidth().clickable(onClick = onDictionary)) {
                Row(Modifier.padding(16.dp), verticalAlignment = Alignment.CenterVertically) {
                    Icon(Icons.Filled.Book, null)
                    Text("  Слов к повторению: ${s.dueCount}", style = MaterialTheme.typography.titleMedium)
                }
            }
        }
        SectionTitle("Продолжить")
        if (s.suggested.isEmpty()) Text("Откройте «Библиотеку» и выберите текст.")
        s.suggested.forEach { TextCard(it) { onOpenText(it.id) } }
    }
}

@Composable
private fun Stat(value: String, label: String) = Column(horizontalAlignment = Alignment.CenterHorizontally) {
    Text(value, style = MaterialTheme.typography.headlineMedium, fontWeight = FontWeight.Bold)
    Text(label, style = MaterialTheme.typography.labelMedium)
}

@Composable
private fun TextCard(t: TextItem, onHistory: (() -> Unit)? = null, onClick: () -> Unit) = ElevatedCard(Modifier.fillMaxWidth().clickable(onClick = onClick)) {
    Column(Modifier.padding(16.dp)) {
        Row(verticalAlignment = Alignment.CenterVertically) {
            AssistChip(onClick = onClick, label = { Text(t.level) })
            Spacer(Modifier.width(8.dp))
            Text(t.genre, style = MaterialTheme.typography.labelMedium)
            Spacer(Modifier.weight(1f))
            when (t.status) {
                "done" -> Icon(Icons.Filled.CheckCircle, "Готово", tint = MaterialTheme.colorScheme.secondary)
                "started" -> Icon(Icons.Filled.PlayCircle, "Начато", tint = MaterialTheme.colorScheme.primary)
            }
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
        if (s.items.isEmpty()) Empty("Ничего не найдено")
        else LazyColumn(contentPadding = PaddingValues(16.dp), verticalArrangement = Arrangement.spacedBy(10.dp)) {
            items(s.items, key = { it.id }) { t -> TextCard(t, onHistory = { historyFor = t }) { onOpen(t.id) } }
        }
    }
}

// ---- Reading --------------------------------------------------------------------------------
@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun ReadingScreen(vm: PracticeViewModel, textId: String, onRecord: () -> Unit) {
    val s by vm.state.collectAsStateWithLifecycle()
    val jobs by vm.jobs.collectAsStateWithLifecycle()
    LaunchedEffect(textId) { vm.load(textId) }
    DisposableEffect(Unit) { onDispose { vm.stopSpeaking() } }
    val text = s.text
    if (text == null || text.item.id != textId) { Empty("Загрузка…"); return }
    val linkStyle = TextLinkStyles(SpanStyle(color = MaterialTheme.colorScheme.onSurface, textDecoration = TextDecoration.Underline))

    Column(Modifier.fillMaxSize()) {
        Column(Modifier.weight(1f).verticalScroll(rememberScrollState()).padding(16.dp)) {
            Text(text.item.titleEn, style = MaterialTheme.typography.headlineSmall)
            Text(text.item.titleRu, color = MaterialTheme.colorScheme.onSurfaceVariant)
            Spacer(Modifier.height(12.dp))
            val reading = s.reading
            if (reading != null) {
                val job = jobs.filter { it.textId == textId }.let { l -> l.firstOrNull { it.running } ?: l.firstOrNull() }
                ReadingStatusCard(reading, job)
                Spacer(Modifier.height(12.dp))
            }
            s.error?.let { Text(it, color = MaterialTheme.colorScheme.error, modifier = Modifier.padding(bottom = 8.dp)) }
            val marked = remember(text.body, reading, s.wordSheet) {
                markedBody(text.body, reading, s.wordSheet, onScored = { vm.selectWord(it) }, onPlain = { vm.showWord(it) })
            }
            if (marked != null) {
                Text(marked, fontSize = 20.sp, lineHeight = 30.sp, modifier = Modifier.padding(bottom = 12.dp))
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
                Text(annotated, fontSize = 20.sp, lineHeight = 30.sp, modifier = Modifier.padding(bottom = 12.dp))
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
