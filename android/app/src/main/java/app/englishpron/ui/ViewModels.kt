package app.englishpron.ui

import android.app.Application
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.viewModelScope
import app.englishpron.EnglishApp
import app.englishpron.PronCore
import app.englishpron.UserSettings
import app.englishpron.audio.SAMPLE_RATE
import app.englishpron.data.*
import app.englishpron.audio.Wav
import app.englishpron.engine.JobUi
import app.englishpron.engine.LiveSession
import app.englishpron.engine.LiveState
import app.englishpron.engine.WordState
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.delay
import kotlinx.coroutines.ExperimentalCoroutinesApi
import kotlinx.coroutines.FlowPreview
import kotlinx.coroutines.flow.*
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import kotlinx.coroutines.withTimeoutOrNull
import java.io.File
import kotlin.math.max
import kotlin.math.min
import org.json.JSONObject

abstract class BaseVm(app: Application) : AndroidViewModel(app) {
    protected val ctx get() = getApplication<EnglishApp>()
    protected suspend fun <T> io(block: suspend (AppDatabase) -> T): T = withContext(Dispatchers.IO) { block(ctx.db()) }
}

// ---- Home -----------------------------------------------------------------------------------
data class HomeState(val data: HomeData? = null, val loading: Boolean = true)

class HomeViewModel(app: Application) : BaseVm(app) {
    private val _state = MutableStateFlow(HomeState())
    val state: StateFlow<HomeState> = _state
    fun refresh() = viewModelScope.launch {
        val st = ctx.settings.value
        _state.value = try {
            HomeState(io { db -> db.homeData(st.level, st.dailyGoal) }, false)
        } catch (e: Exception) { HomeState(null, false) }
    }
}

// ---- Library --------------------------------------------------------------------------------
data class LibraryState(val levels: List<String> = emptyList(), val genres: List<String> = emptyList(), val items: List<TextItem> = emptyList())

@OptIn(FlowPreview::class, ExperimentalCoroutinesApi::class)
class LibraryViewModel(app: Application) : BaseVm(app) {
    val level = MutableStateFlow<String?>(null)
    val genre = MutableStateFlow<String?>(null)
    val query = MutableStateFlow("")
    private val tick = MutableStateFlow(0)
    private val _filters = MutableStateFlow(Pair(emptyList<String>(), emptyList<String>()))

    val state: StateFlow<LibraryState> = combine(level, genre, query.debounce(250), tick, ctx.queue.version) { l, g, q, _, _ -> Triple(l, g, q) }
        .mapLatest { (l, g, q) ->
            io { db ->
                if (_filters.value.first.isEmpty()) _filters.value = db.levels() to db.genres()
                LibraryState(_filters.value.first, _filters.value.second, db.texts(l, g, q))
            }
        }
        .catch { emit(LibraryState()) }
        .stateIn(viewModelScope, SharingStarted.WhileSubscribed(5000), LibraryState())

    fun refresh() { tick.value++ }

    private val _history = MutableStateFlow<List<AttemptItem>>(emptyList())
    val history: StateFlow<List<AttemptItem>> = _history
    fun loadHistory(textId: String?) {
        if (textId == null) { _history.value = emptyList(); return }
        viewModelScope.launch { _history.value = try { io { it.attempts(textId) } } catch (e: Exception) { emptyList() } }
    }
    fun playAttempt(a: AttemptItem) {
        viewModelScope.launch {
            val (pcm, rate) = withContext(Dispatchers.IO) {
                try { Wav.range(java.io.File(a.wavPath), 0.0, Double.MAX_VALUE) } catch (e: Exception) { ShortArray(0) to SAMPLE_RATE }
            }
            if (pcm.isNotEmpty()) ctx.player.play(pcm, rate)
        }
    }
    override fun onCleared() { ctx.player.stop() }
}

// ---- Practice (reading -> recording -> saved result) ------------------------------------------
enum class RecordMode { WHOLE, PARAGRAPH, SENTENCE }
/** PROCESSING here only means "saving the recording" (fast); the heavy assessment runs in the queue. */
enum class RecStatus { IDLE, RECORDING, PROCESSING }

data class WordInfo(val word: String, val ipa: String, val translation: String, val saved: Boolean)

data class PracticeState(
    val text: TextFull? = null,
    val vocab: List<VocabItem> = emptyList(),
    val focus: List<FocusSound> = emptyList(),
    val speed: Float = 1f,
    val mode: RecordMode = RecordMode.WHOLE,
    val sentences: List<String> = emptyList(),
    val sentenceIdx: Int = 0,
    val status: RecStatus = RecStatus.IDLE,
    val permissionDenied: Boolean = false,
    val wordInfo: WordInfo? = null,
    val error: String? = null,
    val live: LiveState? = null,
    val liveUnavailable: Boolean = false,
    /** Latest saved reading of this text (live marks, then the full assessment) and the attempt history. */
    val reading: TextReading? = null,
    val attempts: List<AttemptItem> = emptyList(),
    val wordSheet: WordResult? = null,
    val paragraphs: List<String> = emptyList(),
    val paragraphIdx: Int = 0,
    /** Words read per paragraph (latest attempt of each) for the live text-level coverage. */
    val paragraphReads: Map<Int, Int> = emptyMap(),
    val position: ReadingPos? = null,
    val resumeWord: Int = 0,
    val celebration: Celebration? = null,
    val sessionXp: Int = 0,
    val sessionAchievements: List<AchievementDef> = emptyList(),
) {
    val referenceNow: String get() = when (mode) {
        RecordMode.WHOLE -> text?.body ?: ""
        RecordMode.PARAGRAPH -> paragraphs.getOrElse(paragraphIdx) { "" }
        RecordMode.SENTENCE -> sentences.getOrElse(sentenceIdx) { "" }
    }

    /** Live "Прочитано N%": share of words in state `read` over the whole text. */
    fun coveragePct(): Double {
        val t = text ?: return 0.0
        val readNow = live?.words?.count { it.state == WordState.READ } ?: 0
        return when (mode) {
            RecordMode.PARAGRAPH -> min(100.0, (paragraphReads.filterKeys { it != paragraphIdx }.values.sum() + readNow) * 100.0 / max(1, t.item.wordCount))
            else -> coverageOf(live)
        }
    }
}

/** Shown after a text is finished. */
data class Celebration(val title: String, val coverage: Double, val xp: Int, val achievements: List<AchievementDef>, val level: LevelInfo, val totalXp: Int)

class PracticeViewModel(app: Application) : BaseVm(app) {
    private val _s = MutableStateFlow(PracticeState())
    val state: StateFlow<PracticeState> = _s
    val level: StateFlow<Float> = ctx.recorder.level
    /** Queued/running background jobs (progress of this text's job is shown on the reading screen). */
    val jobs: StateFlow<List<JobUi>> = ctx.queue.jobs
    private var startedAt = 0L
    val scorer = DrillScorer(ctx)
    val settings: StateFlow<UserSettings> = ctx.settings

    init {
        // Any queue status change (job finished, ...) refreshes the saved result of the open text.
        viewModelScope.launch { ctx.queue.version.collect { reloadReading() } }
    }

    fun load(textId: String) {
        if (_s.value.text?.item?.id == textId) { reloadReading(); return }
        viewModelScope.launch {
            val data = try {
                io { db ->
                    val t = db.text(textId)
                    db.markOpened(textId)
                    Triple(t, db.vocabulary(textId), db.focusSounds(textId))
                }
            } catch (e: Exception) { return@launch }
            val t = data.first ?: return@launch
            val paragraphs = paragraphsOf(t.body)
            val mode = if (t.item.wordCount > 300 && paragraphs.size > 1) RecordMode.PARAGRAPH else RecordMode.WHOLE
            _s.value = PracticeState(text = t, vocab = data.second, focus = data.third, sentences = splitSentences(t.body),
                paragraphs = paragraphs, mode = mode)
            reloadReading()
        }
    }

    private fun reloadReading() {
        val id = _s.value.text?.item?.id ?: return
        viewModelScope.launch {
            val r = try { io { Triple(it.latestReading(id), it.attempts(id), it.readingPosition(id) to it.paragraphReads(id)) } } catch (e: Exception) { return@launch }
            _s.update {
                if (it.text?.item?.id == id) it.copy(reading = r.first, attempts = r.second, position = r.third.first, paragraphReads = r.third.second) else it
            }
        }
    }

    private fun splitSentences(body: String): List<String> =
        body.split(Regex("(?<=[.!?…])\\s+|\\n{2,}")).map { it.trim() }.filter { it.isNotEmpty() }

    fun setSpeed(v: Float) { _s.update { it.copy(speed = v) } }
    fun setMode(m: RecordMode) { _s.update { it.copy(mode = m, sentenceIdx = 0) } }
    fun setParagraph(i: Int) { _s.update { it.copy(paragraphIdx = i.coerceIn(0, (it.paragraphs.size - 1).coerceAtLeast(0))) } }

    /** "Продолжить с места": paragraph mode jumps to the saved paragraph, whole-text mode to the saved word. */
    fun resume() {
        val p = _s.value.position ?: return
        _s.update {
            if (p.wordIndex > 0) it.copy(mode = RecordMode.WHOLE, resumeWord = p.wordIndex)
            else it.copy(mode = if (it.paragraphs.size > 1) RecordMode.PARAGRAPH else RecordMode.WHOLE,
                paragraphIdx = p.paragraphIndex.coerceIn(0, (it.paragraphs.size - 1).coerceAtLeast(0)), resumeWord = 0)
        }
    }

    fun dismissCelebration() { _s.update { it.copy(celebration = null, sessionXp = 0, sessionAchievements = emptyList()) } }

    /** "Дальше": records the current paragraph (if recording) and moves on; otherwise just moves on. */
    fun nextParagraph(onDone: () -> Unit) {
        if (_s.value.status == RecStatus.RECORDING) stopAndSave(forceAdvance = true, onDone = onDone)
        else setParagraph(_s.value.paragraphIdx + 1)
    }

    /** Shadowing: a repeated sentence earns XP for its words. */
    fun shadowDone(sentence: String) {
        val n = Regex("[\\p{L}\\p{N}]+").findAll(sentence).count()
        viewModelScope.launch { try { io { it.addXp(n, "shadow") } } catch (_: Exception) {} }
    }

    fun setSentence(i: Int) { _s.update { it.copy(sentenceIdx = i.coerceIn(0, (it.sentences.size - 1).coerceAtLeast(0))) } }

    fun speak(text: String) = ctx.tts.speak(text, _s.value.speed)
    fun stopSpeaking() { ctx.tts.stop(); ctx.player.stop() }

    fun onPermissionResult(granted: Boolean) { _s.update { it.copy(permissionDenied = !granted) } }

    fun showWord(word: String) {
        val clean = word.trim('.', ',', '!', '?', ';', ':', '"', '“', '”', '(', ')').lowercase()
        if (clean.isEmpty()) return
        viewModelScope.launch {
            val st = _s.value
            val v = st.vocab.firstOrNull { it.word.equals(clean, true) }
            val british = ctx.settings.value.british
            val ipa = v?.let { if (british) it.ipaUk else it.ipaUs }
                ?: try {
                    val viaEngine = ctx.engine.lookup(clean)
                    (viaEngine ?: withContext(Dispatchers.IO) { PronCore.lookup(clean) })?.optString("ipa").orEmpty()
                } catch (e: Exception) { "" }
            val saved = try { io { it.isSaved(clean) } } catch (e: Exception) { false }
            _s.update { it.copy(wordInfo = WordInfo(clean, ipa, v?.translationRu ?: "", saved)) }
        }
    }

    fun dismissWord() { _s.update { it.copy(wordInfo = null) } }

    fun saveCurrentWord() {
        val st = _s.value
        val w = st.wordInfo ?: return
        viewModelScope.launch {
            try { io { it.saveWord(w.word, w.ipa, w.translation, st.text?.item?.id) } } catch (e: Exception) { return@launch }
            _s.update { it.copy(wordInfo = w.copy(saved = true)) }
        }
    }

    // ---- scored-word sheet (result of the background assessment) ------------------------------
    fun selectWord(w: WordResult?) { _s.update { it.copy(wordSheet = w) } }

    /** "Эталон": engine TTS (system voice while the engine is busy). */
    fun playReference(w: WordResult) = ctx.tts.speak(w.text, 1f)

    /** "Моя запись": the word's time span from the saved WAV. */
    fun playMyWord(w: WordResult) {
        val path = w.wavPath.ifEmpty { _s.value.reading?.wavPath ?: "" }
        if (path.isEmpty()) return
        if (w.startSec < 0 || w.endSec <= w.startSec) { _s.update { it.copy(error = "Для этого слова нет фрагмента записи") }; return }
        viewModelScope.launch {
            val (pcm, rate) = withContext(Dispatchers.IO) {
                try { Wav.range(File(path), w.startSec - 0.12, w.endSec + 0.12) } catch (e: Exception) { ShortArray(0) to SAMPLE_RATE }
            }
            ctx.tts.stop()
            if (pcm.isEmpty()) _s.update { it.copy(error = "Запись не найдена") } else ctx.player.play(pcm, rate)
        }
    }

    fun playAttempt(a: AttemptItem) {
        viewModelScope.launch {
            val (pcm, rate) = withContext(Dispatchers.IO) {
                try { Wav.range(File(a.wavPath), 0.0, Double.MAX_VALUE) } catch (e: Exception) { ShortArray(0) to SAMPLE_RATE }
            }
            ctx.tts.stop()
            if (pcm.isNotEmpty()) ctx.player.play(pcm, rate)
        }
    }

    fun clearError() { _s.update { it.copy(error = null) } }

    private var session: LiveSession? = null
    private var liveJob: Job? = null
    private var silenceJob: Job? = null
    private var autoDone: () -> Unit = {}
    private var starting = false

    fun startRecording(onAutoDone: () -> Unit = {}) {
        if (starting || _s.value.status != RecStatus.IDLE) return
        starting = true
        autoDone = onAutoDone
        ctx.tts.stop(); ctx.player.stop()
        val reference = _s.value.referenceNow
        viewModelScope.launch {
            try {
                val sess = if (reference.isNotBlank()) ctx.engine.liveStart(reference) else null
                val sink: ((ShortArray) -> Unit)? = sess?.let { l -> { frame: ShortArray -> l.offer(frame) } }
                val ok = ctx.recorder.start(sink)
                if (!ok) {
                    sess?.release()
                    _s.update { it.copy(error = "Не удалось начать запись") }
                } else {
                    startedAt = System.currentTimeMillis()
                    session = sess
                    val rw = _s.value.resumeWord
                    if (sess != null && rw > 0 && _s.value.mode == RecordMode.WHOLE) { sess.setCursor(rw); _s.update { it.copy(resumeWord = 0) } }
                    liveJob?.cancel()
                    liveJob = sess?.let { l ->
                        viewModelScope.launch {
                            l.state.collect { st ->
                                _s.update { it.copy(live = st) }
                                if (st?.done == true && silenceJob?.isActive != true) watchSilence()
                            }
                        }
                    }
                    _s.update { it.copy(status = RecStatus.RECORDING, error = null, live = null, liveUnavailable = sess == null) }
                }
            } finally { starting = false }
        }
    }

    /** Reading finished (live tracker saw the last word): stop automatically after 1.5 s of silence. */
    private fun watchSilence() {
        silenceJob = viewModelScope.launch {
            var quietSince = 0L
            while (_s.value.status == RecStatus.RECORDING) {
                delay(100)
                val now = System.currentTimeMillis()
                if (level.value < 0.04f) {
                    if (quietSince == 0L) quietSince = now
                    else if (now - quietSince >= 1500) { stopAndSave(auto = true, onDone = autoDone); return@launch }
                } else quietSince = 0L
            }
        }
    }

    /** Screen left while recording (not a rotation): drop the recording and release the mic and the live tracker. */
    fun cancelRecording() {
        if (_s.value.status != RecStatus.RECORDING) return
        silenceJob?.cancel(); liveJob?.cancel()
        session?.release(); session = null
        _s.update { it.copy(status = RecStatus.PROCESSING) }
        viewModelScope.launch {
            try { ctx.recorder.stop() } finally { _s.update { it.copy(status = RecStatus.IDLE, live = null) } }
        }
    }

    /** User tapped word [index] while recording: restart live tracking from it. */
    fun liveSetCursor(index: Int) {
        if (_s.value.status == RecStatus.RECORDING) session?.setCursor(index)
    }

    /**
     * Stops recording, saves the WAV + the quick live result and coverage, queues the heavy assessment (one job per
     * paragraph in paragraph mode) and returns immediately ([onDone] fires once the text/sentence list is finished).
     * [auto]: stopped by the silence watcher (paragraph mode then continues with the next paragraph if enabled);
     * [forceAdvance]: the user pressed "Дальше".
     */
    fun stopAndSave(auto: Boolean = false, forceAdvance: Boolean = false, onDone: () -> Unit) {
        val st = _s.value
        if (st.status != RecStatus.RECORDING) return
        val textId = st.text?.item?.id ?: return
        val reference = st.referenceNow
        val mode = st.mode
        val kind = when (mode) { RecordMode.WHOLE -> "reading"; RecordMode.PARAGRAPH -> "paragraph"; RecordMode.SENTENCE -> "sentence" }
        silenceJob?.cancel()
        _s.update { it.copy(status = RecStatus.PROCESSING) }
        viewModelScope.launch {
            val durationMs = System.currentTimeMillis() - startedAt
            val pcm = ctx.recorder.stop()
            val sess = session; session = null
            liveJob?.cancel()
            val fin = try { if (sess != null) withTimeoutOrNull(3000) { sess.finish() } ?: run { sess.release(); null } else null } catch (_: Throwable) { null }
            val live = fin ?: _s.value.live
            if (pcm.size < SAMPLE_RATE / 2) {
                _s.update { it.copy(status = RecStatus.IDLE, live = null, error = "Запись слишком короткая") }
                return@launch
            }
            val seconds = pcm.size.toDouble() / SAMPLE_RATE
            val total = live?.words?.size ?: Regex("[\\p{L}\\p{N}]+").findAll(reference).count()
            val read = live?.words?.count { it.state == WordState.READ } ?: total   // no live model: nothing to measure, assume read
            val skipped = live?.words?.count { it.state == WordState.SKIPPED } ?: 0
            val cov = if (total > 0) read * 100.0 / total else 0.0
            val pCount = st.paragraphs.size
            val advance = mode == RecordMode.PARAGRAPH && (forceAdvance || live == null || live.done)
            val next = if (advance) st.paragraphIdx + 1 else st.paragraphIdx
            val meta = ReadingMeta(cov, skipped, read, total, seconds, if (seconds > 0) read / (seconds / 60.0) else 0.0,
                if (mode == RecordMode.PARAGRAPH) st.paragraphIdx else null, pCount, live?.cursor ?: 0, next)
            try {
                val out = io { db ->
                    val file = File(ctx.filesDir, "recordings/${textId}-${System.currentTimeMillis()}.wav")
                    Wav.write(file, pcm, SAMPLE_RATE)
                    try {
                        db.saveReading(textId, kind, durationMs, file.path, seconds, reference, live?.toJson(), true, meta)
                    } catch (e: Exception) { file.delete(); throw e }
                }
                ctx.queue.onEnqueued()
                reloadReading()
                val morePar = mode == RecordMode.PARAGRAPH && next < pCount
                val moreSent = mode == RecordMode.SENTENCE && st.sentenceIdx < st.sentences.size - 1
                val finished = when (mode) {
                    RecordMode.WHOLE -> true
                    RecordMode.PARAGRAPH -> advance && !morePar
                    RecordMode.SENTENCE -> !moreSent
                }
                val xp = st.sessionXp + out.xp
                val ach = st.sessionAchievements + out.achievements
                var celebration: Celebration? = null
                if (finished && mode != RecordMode.SENTENCE && out.textCoverage >= 50.0) {
                    val totalXp = try { io { it.xpTotal() } } catch (_: Exception) { xp }
                    celebration = Celebration(st.text?.item?.titleEn ?: "", out.textCoverage, xp, ach, levelFor(totalXp), totalXp)
                }
                _s.update {
                    it.copy(status = RecStatus.IDLE, live = null,
                        sentenceIdx = if (moreSent) it.sentenceIdx + 1 else it.sentenceIdx,
                        paragraphIdx = if (mode == RecordMode.PARAGRAPH) min(next, max(0, pCount - 1)) else it.paragraphIdx,
                        celebration = celebration ?: it.celebration,
                        sessionXp = if (finished) 0 else xp, sessionAchievements = if (finished) emptyList() else ach)
                }
                if (finished) onDone()
                else if (morePar && (forceAdvance || (auto && ctx.settings.value.autoAdvance))) startRecording(onDone)
            } catch (e: Exception) {
                _s.update { it.copy(status = RecStatus.IDLE, live = null, error = "Не удалось сохранить запись: ${e.message}") }
            }
        }
    }

    override fun onCleared() {
        ctx.player.stop()
        session?.release(); session = null
        if (ctx.recorder.recording.value) kotlinx.coroutines.CoroutineScope(Dispatchers.IO).launch { ctx.recorder.stop() }
    }
}

// ---- Sounds ---------------------------------------------------------------------------------
class SoundsViewModel(app: Application) : BaseVm(app) {
    private val _list = MutableStateFlow<List<SoundSummary>>(emptyList())
    val list: StateFlow<List<SoundSummary>> = _list
    private val _card = MutableStateFlow<JSONObject?>(null)
    val card: StateFlow<JSONObject?> = _card

    init { viewModelScope.launch { _list.value = try { io { it.sounds() } } catch (e: Exception) { emptyList() } } }
    fun open(id: String) { viewModelScope.launch { _card.value = try { io { it.soundCard(id) } } catch (e: Exception) { null } } }
    fun speak(text: String) = ctx.tts.speak(text, 1f)
}

// ---- Dictionary -----------------------------------------------------------------------------
data class DictState(val due: List<SavedWord> = emptyList(), val all: List<SavedWord> = emptyList())

class DictionaryViewModel(app: Application) : BaseVm(app) {
    val scorer = DrillScorer(ctx)
    private val _s = MutableStateFlow(DictState())
    val state: StateFlow<DictState> = _s
    fun refresh() = viewModelScope.launch {
        _s.value = try { io { DictState(it.savedWords(true), it.savedWords(false)) } } catch (e: Exception) { DictState() }
    }
    fun review(w: SavedWord, grade: Int) = viewModelScope.launch { try { io { it.review(w, grade) } } catch (e: Exception) {}; refresh() }
    fun delete(w: SavedWord) = viewModelScope.launch { try { io { it.deleteWord(w.id) } } catch (e: Exception) {}; refresh() }
    fun speak(text: String) = ctx.tts.speak(text, 1f)
}

// ---- Progress / Settings --------------------------------------------------------------------
data class ProgressAll(val summary: ProgressSummary, val v2: ProgressV2)

class ProgressViewModel(app: Application) : BaseVm(app) {
    private val _s = MutableStateFlow<ProgressAll?>(null)
    val state: StateFlow<ProgressAll?> = _s
    fun refresh() = viewModelScope.launch { _s.value = try { io { ProgressAll(it.progress(), it.progressV2()) } } catch (e: Exception) { null } }
}

class SettingsViewModel(app: Application) : BaseVm(app) {
    val settings: StateFlow<UserSettings> = ctx.settings
    val engineStatus = ctx.engine.status
    fun update(s: UserSettings) { viewModelScope.launch { ctx.updateSettings(s) } }
}
