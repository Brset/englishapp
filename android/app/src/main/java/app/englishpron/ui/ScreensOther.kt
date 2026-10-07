package app.englishpron.ui

import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.horizontalScroll
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.*
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import androidx.lifecycle.viewmodel.compose.viewModel
import app.englishpron.PronCore
import app.englishpron.UserSettings
import app.englishpron.data.ALL_ACHIEVEMENTS
import app.englishpron.data.levelFor

// ---- Sound cards ----------------------------------------------------------------------------
@Composable
fun SoundsScreen(onOpen: (String) -> Unit, vm: SoundsViewModel = viewModel()) {
    val list by vm.list.collectAsStateWithLifecycle()
    if (list.isEmpty()) { EmptyState(Icons.Filled.Hearing, "Карточки звуков не найдены", "Они появятся после обновления контента"); return }
    LazyColumn(contentPadding = PaddingValues(16.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
        item { Text("Артикуляция и минимальные пары для русскоговорящих", style = MaterialTheme.typography.bodyMedium) }
        items(list, key = { it.id }) { s ->
            ElevatedCard(Modifier.fillMaxWidth().clickable { onOpen(s.id) }) {
                Row(Modifier.padding(16.dp), verticalAlignment = Alignment.CenterVertically) {
                    Text("/${s.ipa}/", fontSize = 24.sp, fontWeight = FontWeight.Bold, modifier = Modifier.width(88.dp))
                    Text(s.nameRu, Modifier.weight(1f))
                    Text("★".repeat(s.difficulty.coerceIn(1, 3)), color = MaterialTheme.colorScheme.tertiary)
                }
            }
        }
    }
}

@Composable
fun SoundDetailScreen(id: String, onDrill: (String) -> Unit, vm: SoundsViewModel = viewModel()) {
    LaunchedEffect(id) { vm.open(id) }
    val card by vm.card.collectAsStateWithLifecycle()
    val c = card
    if (c == null || c.optString("id") != id) { SkeletonList(3); return }
    Column(Modifier.fillMaxSize().verticalScroll(rememberScrollState()).padding(16.dp), verticalArrangement = Arrangement.spacedBy(6.dp)) {
        Text("/${c.optString("ipa")}/  ${c.optString("name_ru")}", style = MaterialTheme.typography.headlineSmall)
        FilledTonalButton({ onDrill(id) }, Modifier.fillMaxWidth()) { Icon(Icons.Filled.Mic, null); Spacer(Modifier.width(6.dp)); Text("Тренировать звук") }
        c.optString("us_uk_note_ru").takeIf { it.isNotEmpty() && it != "null" }?.let { Text("US/UK: $it", style = MaterialTheme.typography.bodyMedium) }

        SectionTitle("Артикуляция")
        c.optJSONArray("articulation_ru").texts().forEachIndexed { i, t -> Text("${i + 1}. $t") }

        SectionTitle("Типичные ошибки")
        c.optJSONArray("typical_errors").objects().forEach { e ->
            Card(Modifier.fillMaxWidth()) {
                Column(Modifier.padding(12.dp)) {
                    Text("Вместо этого звучит «${e.optString("substitute")}» (как в ${e.optString("heard_as_example")})", fontWeight = FontWeight.Medium)
                    Text(e.optString("tip_ru"))
                }
            }
        }

        val pairs = c.optJSONArray("minimal_pairs").objects()
        if (pairs.isNotEmpty()) {
            SectionTitle("Минимальные пары (нажмите, чтобы послушать)")
            pairs.forEach { p ->
                Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                    FilledTonalButton({ vm.speak(p.optString("target")) }, Modifier.weight(1f)) { Text(p.optString("target")) }
                    Text("≠", Modifier.align(Alignment.CenterVertically))
                    OutlinedButton({ vm.speak(p.optString("contrast")) }, Modifier.weight(1f)) { Text(p.optString("contrast")) }
                }
            }
        }

        val stress = c.optJSONArray("stress_pairs").objects()
        if (stress.isNotEmpty()) {
            SectionTitle("Пары с разным ударением")
            stress.forEach { p ->
                Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                    FilledTonalButton({ vm.speak(p.optString("first_stressed")) }, Modifier.weight(1f)) {
                        Text("${p.optString("first_stressed")}\n${p.optString("translation_first_ru")}")
                    }
                    OutlinedButton({ vm.speak(p.optString("second_stressed")) }, Modifier.weight(1f)) {
                        Text("${p.optString("second_stressed")}\n${p.optString("translation_second_ru")}")
                    }
                }
            }
        }

        SectionTitle("Примеры слов")
        c.optJSONArray("example_words").objects().forEach { w ->
            Row(Modifier.fillMaxWidth().clickable { vm.speak(w.optString("word")) }.padding(vertical = 4.dp), verticalAlignment = Alignment.CenterVertically) {
                Icon(Icons.Filled.VolumeUp, null, Modifier.size(18.dp))
                Text("  ${w.optString("word")}", Modifier.width(120.dp), fontWeight = FontWeight.Medium)
                Text("/${w.optString("ipa_us")}/", Modifier.width(110.dp), color = MaterialTheme.colorScheme.primary)
                Text(w.optString("translation_ru"), style = MaterialTheme.typography.bodySmall)
            }
        }
        for ((title, key) in listOf("Фразы" to "phrases", "Скороговорки" to "tongue_twisters")) {
            val items = c.optJSONArray(key).texts()
            if (items.isNotEmpty()) {
                SectionTitle(title)
                items.forEach { t -> Text(t, Modifier.clickable { vm.speak(t) }.padding(vertical = 4.dp)) }
            }
        }
    }
}

// ---- Dictionary -----------------------------------------------------------------------------
@Composable
fun DictionaryScreen(vm: DictionaryViewModel = viewModel()) {
    val s by vm.state.collectAsStateWithLifecycle()
    var tab by remember { mutableIntStateOf(0) }
    LaunchedEffect(Unit) { vm.refresh() }
    Column(Modifier.fillMaxSize()) {
        TabRow(tab) {
            Tab(tab == 0, { tab = 0 }, text = { Text("К повторению (${s.due.size})") })
            Tab(tab == 1, { tab = 1 }, text = { Text("Все слова (${s.all.size})") })
        }
        if (tab == 0) {
            val w = s.due.firstOrNull()
            if (w == null) EmptyState(Icons.Filled.Book, "Повторять нечего", "Слова со слабой оценкой добавляются сами; ещё можно нажать слово в тексте → «В словарь».")
            else WordDrillCard(w, vm)
        } else {
            if (s.all.isEmpty()) EmptyState(Icons.Filled.Book, "Словарь пуст", "Слова появятся после чтения текстов")
            else LazyColumn(contentPadding = PaddingValues(16.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
                items(s.all, key = { it.id }) { w ->
                    Card(Modifier.fillMaxWidth()) {
                        Row(Modifier.padding(12.dp), verticalAlignment = Alignment.CenterVertically) {
                            Column(Modifier.weight(1f)) {
                                Text(w.word, style = MaterialTheme.typography.titleMedium)
                                Text(listOf(w.ipa.takeIf { it.isNotEmpty() }?.let { "/$it/" }, w.translationRu.takeIf { it.isNotEmpty() })
                                    .filterNotNull().joinToString("  "), style = MaterialTheme.typography.bodySmall)
                            }
                            IconButton({ vm.speak(w.word) }) { Icon(Icons.Filled.VolumeUp, "Озвучить") }
                            IconButton({ vm.delete(w) }) { Icon(Icons.Filled.Delete, "Удалить") }
                        }
                    }
                }
            }
        }
    }
}

// ---- Progress -------------------------------------------------------------------------------
@OptIn(ExperimentalLayoutApi::class)
@Composable
fun ProgressScreen(vm: ProgressViewModel = viewModel()) {
    val p by vm.state.collectAsStateWithLifecycle()
    LaunchedEffect(Unit) { vm.refresh() }
    val all = p ?: run { SkeletonList(4); return }
    val s = all.summary
    val v = all.v2
    val lvl = levelFor(v.xp)
    Column(Modifier.fillMaxSize().verticalScroll(rememberScrollState()).padding(16.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
        Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            Tile("Серия", "${s.streakDays} дн.", Modifier.weight(1f))
            Tile("Слов прочитано", "${v.totalWords}", Modifier.weight(1f))
            Tile("Средний балл", s.avgScore?.let { "%.0f".format(it) } ?: "—", Modifier.weight(1f))
        }
        Text("${lvl.name} · ${v.xp} XP", style = MaterialTheme.typography.titleMedium)
        SectionTitle("Точность во времени")
        ElevatedCard(Modifier.fillMaxWidth()) {
            if (v.accuracy.isEmpty()) Text("Появится после первых оценённых чтений.", Modifier.padding(16.dp), color = MaterialTheme.colorScheme.onSurfaceVariant)
            else LineChart(v.accuracy, Modifier.padding(16.dp))
        }
        SectionTitle("Минуты по неделям")
        ElevatedCard(Modifier.fillMaxWidth()) {
            BarChart(v.weeks.mapIndexed { i, m -> (if (i == v.weeks.size - 1) "эта" else "-${v.weeks.size - 1 - i}") to m }, Modifier.padding(16.dp))
        }
        SectionTitle("Тексты по уровням")
        if (v.levels.isEmpty()) Text("Нет данных.") else FlowRow(horizontalArrangement = Arrangement.spacedBy(12.dp), verticalArrangement = Arrangement.spacedBy(12.dp)) {
            v.levels.forEach { (level, done, total) ->
                Column(horizontalAlignment = Alignment.CenterHorizontally) {
                    ProgressRing(if (total > 0) done.toFloat() / total else 0f, Modifier.size(64.dp), 7.dp) {
                        Text("$done/$total", style = MaterialTheme.typography.labelMedium)
                    }
                    Text(level, style = MaterialTheme.typography.labelLarge)
                }
            }
        }
        SectionTitle("Сложные звуки")
        if (s.weakSounds.isEmpty()) Text("Данных пока мало — прочитайте несколько текстов.")
        s.weakSounds.forEach { (ipa, errors, avg) ->
            Row(Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically) {
                Text("/$ipa/", Modifier.width(70.dp), fontSize = 20.sp, fontWeight = FontWeight.Bold)
                LinearProgressIndicator(progress = { (avg / 100).toFloat().coerceIn(0f, 1f) }, Modifier.weight(1f), color = scoreColor(avg))
                Text("  %.0f, ошибок: $errors".format(avg), style = MaterialTheme.typography.labelMedium)
            }
        }
        SectionTitle("Достижения")
        FlowRow(horizontalArrangement = Arrangement.spacedBy(8.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
            ALL_ACHIEVEMENTS.forEach { a ->
                val on = a.id in v.achievements
                AssistChip(onClick = {}, label = { Text(a.title) },
                    leadingIcon = { Icon(if (on) Icons.Filled.EmojiEvents else Icons.Filled.Lock, null, Modifier.size(18.dp)) },
                    colors = AssistChipDefaults.assistChipColors(
                        labelColor = if (on) MaterialTheme.colorScheme.onSurface else MaterialTheme.colorScheme.onSurfaceVariant.copy(alpha = 0.6f)))
            }
        }
        SectionTitle("Последние попытки")
        if (s.recent.isEmpty()) Text("Пока пусто.")
        s.recent.forEach { (title, score) ->
            Row(Modifier.fillMaxWidth()) {
                Text(title, Modifier.weight(1f)); Text("%.0f".format(score), fontWeight = FontWeight.Bold, color = scoreColor(score))
            }
        }
    }
}

@Composable
private fun Tile(label: String, value: String, modifier: Modifier) = ElevatedCard(modifier) {
    Column(Modifier.padding(12.dp)) {
        Text(value, style = MaterialTheme.typography.titleLarge, fontWeight = FontWeight.Bold)
        Text(label, style = MaterialTheme.typography.labelMedium)
    }
}

// ---- Settings -------------------------------------------------------------------------------
@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun SettingsScreen(vm: SettingsViewModel = viewModel()) {
    val s by vm.settings.collectAsStateWithLifecycle()
    Column(Modifier.fillMaxSize().verticalScroll(rememberScrollState()).padding(16.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
        SectionTitle("Произношение")
        SingleChoiceSegmentedButtonRow(Modifier.fillMaxWidth()) {
            SegmentedButton(!s.british, { vm.update(s.copy(british = false)) }, SegmentedButtonDefaults.itemShape(0, 2)) { Text("US (американский)") }
            SegmentedButton(s.british, { vm.update(s.copy(british = true)) }, SegmentedButtonDefaults.itemShape(1, 2)) { Text("UK (британский)") }
        }
        SectionTitle("Строгость оценки")
        SingleChoiceSegmentedButtonRow(Modifier.fillMaxWidth()) {
            listOf("Мягко", "Обычно", "Строго").forEachIndexed { i, label ->
                SegmentedButton(s.strictness == i, { vm.update(s.copy(strictness = i)) }, SegmentedButtonDefaults.itemShape(i, 3)) { Text(label) }
            }
        }
        SectionTitle("Чтение")
        Text("Размер текста", style = MaterialTheme.typography.labelLarge)
        SingleChoiceSegmentedButtonRow(Modifier.fillMaxWidth()) {
            listOf("S", "M", "L", "XL").forEachIndexed { i, label ->
                SegmentedButton(s.fontSize == i, { vm.update(s.copy(fontSize = i)) }, SegmentedButtonDefaults.itemShape(i, 4)) { Text(label) }
            }
        }
        Text("Межстрочный интервал", style = MaterialTheme.typography.labelLarge)
        SingleChoiceSegmentedButtonRow(Modifier.fillMaxWidth()) {
            listOf("Плотно", "Обычно", "Свободно").forEachIndexed { i, label ->
                SegmentedButton(s.lineSpacing == i, { vm.update(s.copy(lineSpacing = i)) }, SegmentedButtonDefaults.itemShape(i, 3)) { Text(label) }
            }
        }
        Row(verticalAlignment = Alignment.CenterVertically) {
            Text("Шрифт с засечками", Modifier.weight(1f)); Switch(s.serif, { vm.update(s.copy(serif = it)) })
        }
        Row(verticalAlignment = Alignment.CenterVertically) {
            Text("Автопереход к следующему абзацу", Modifier.weight(1f)); Switch(s.autoAdvance, { vm.update(s.copy(autoAdvance = it)) })
        }
        Text("Образец: The quick brown fox jumps over the lazy dog.", style = s.readingStyle())
        SectionTitle("Цели")
        Text("Цель дня: ${s.dailyGoal} мин", style = MaterialTheme.typography.labelLarge)
        Slider(s.dailyGoal.toFloat(), { vm.update(s.copy(dailyGoal = it.toInt())) }, valueRange = 5f..60f, steps = 10)
        Text("Уровень", style = MaterialTheme.typography.labelLarge)
        Row(Modifier.horizontalScroll(rememberScrollState()), horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            listOf("A1", "A2", "B1", "B2", "C1", "C2").forEach { l -> FilterChip(s.level == l, { vm.update(s.copy(level = l)) }, { Text(l) }) }
        }
        SectionTitle("О приложении")
        Text("pron_core ${runCatching { PronCore.version }.getOrDefault("?")}", style = MaterialTheme.typography.bodyMedium)
        val es by vm.engineStatus.collectAsStateWithLifecycle()
        SectionTitle("Движок")
        Text(es.describe(), style = MaterialTheme.typography.bodySmall)
    }
}
