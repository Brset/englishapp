package app.englishpron.ui

import android.app.Application
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.viewModelScope
import app.englishpron.AsrWord
import app.englishpron.EnglishApp
import app.englishpron.PronCore
import app.englishpron.UserSettings
import app.englishpron.audio.SAMPLE_RATE
import app.englishpron.data.*
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.ExperimentalCoroutinesApi
import kotlinx.coroutines.FlowPreview
import kotlinx.coroutines.flow.*
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import org.json.JSONObject

abstract class BaseVm(app: Application) : AndroidViewModel(app) {
    protected val ctx get() = getApplication<EnglishApp>()
    protected suspend fun <T> io(block: suspend (AppDatabase) -> T): T = withContext(Dispatchers.IO) { block(ctx.db()) }
}

// ---- Home -----------------------------------------------------------------------------------
data class HomeState(val progress: ProgressSummary? = null, val suggested: List<TextItem> = emptyList(), val dueCount: Int = 0)

class HomeViewModel(app: Application) : BaseVm(app) {
    private val _state = MutableStateFlow(HomeState())
    val state: StateFlow<HomeState> = _state
    fun refresh() = viewModelScope.launch {
        _state.value = try {
            io { db ->
                HomeState(db.progress(), db.texts(null, null, "").filter { it.status != "done" }.take(3), db.savedWords(true).size)
            }
        } catch (e: Exception) { HomeState() }
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

    val state: StateFlow<LibraryState> = combine(level, genre, query.debounce(250), tick) { l, g, q, _ -> Triple(l, g, q) }
        .mapLatest { (l, g, q) ->
            io { db ->
                if (_filters.value.first.isEmpty()) _filters.value = db.levels() to db.genres()
                LibraryState(_filters.value.first, _filters.value.second, db.texts(l, g, q))
            }
        }
        .catch { emit(LibraryState()) }
        .stateIn(viewModelScope, SharingStarted.WhileSubscribed(5000), LibraryState())

    fun refresh() { tick.value++ }
}

// ---- Practice (reading -> recording -> result) ------------------------------------------------
enum class RecordMode { WHOLE, SENTENCE }
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
    val reference: String = "",
    val result: AssessmentUi? = null,
    val lastPcm: ShortArray = ShortArray(0),
    val wordInfo: WordInfo? = null,
    val error: String? = null,
) {
    val referenceNow: String get() = if (mode == RecordMode.WHOLE) (text?.body ?: "") else sentences.getOrElse(sentenceIdx) { "" }
}

class PracticeViewModel(app: Application) : BaseVm(app) {
    private val _s = MutableStateFlow(PracticeState())
    val state: StateFlow<PracticeState> = _s
    val level: StateFlow<Float> = ctx.recorder.level
    private var startedAt = 0L

    fun load(textId: String) {
        if (_s.value.text?.item?.id == textId) return
        viewModelScope.launch {
            val data = io { db ->
                val t = db.text(textId)
                db.markOpened(textId)
                Triple(t, db.vocabulary(textId), db.focusSounds(textId))
            }
            val t = data.first ?: return@launch
            _s.value = PracticeState(text = t, vocab = data.second, focus = data.third, sentences = splitSentences(t.body))
        }
    }

    private fun splitSentences(body: String): List<String> =
        body.split(Regex("(?<=[.!?…])\\s+|\\n{2,}")).map { it.trim() }.filter { it.isNotEmpty() }

    fun setSpeed(v: Float) { _s.update { it.copy(speed = v) } }
    fun setMode(m: RecordMode) { _s.update { it.copy(mode = m, sentenceIdx = 0) } }
    fun setSentence(i: Int) { _s.update { it.copy(sentenceIdx = i.coerceIn(0, (it.sentences.size - 1).coerceAtLeast(0))) } }

    fun speak(text: String) = ctx.tts.speak(text, _s.value.speed)
    fun stopSpeaking() = ctx.tts.stop()

    fun onPermissionResult(granted: Boolean) { _s.update { it.copy(permissionDenied = !granted) } }

    fun showWord(word: String) {
        val clean = word.trim('.', ',', '!', '?', ';', ':', '"', '“', '”', '(', ')').lowercase()
        if (clean.isEmpty()) return
        viewModelScope.launch {
            val st = _s.value
            val v = st.vocab.firstOrNull { it.word.equals(clean, true) }
            val british = ctx.settings.value.british
            val ipa = v?.let { if (british) it.ipaUk else it.ipaUs }
                ?: withContext(Dispatchers.IO) { PronCore.lookup(clean)?.optString("ipa") }.orEmpty()
            val saved = io { it.isSaved(clean) }
            _s.update { it.copy(wordInfo = WordInfo(clean, ipa, v?.translationRu ?: "", saved)) }
        }
    }

    fun dismissWord() { _s.update { it.copy(wordInfo = null) } }

    fun saveCurrentWord() {
        val st = _s.value
        val w = st.wordInfo ?: return
        viewModelScope.launch {
            io { it.saveWord(w.word, w.ipa, w.translation, st.text?.item?.id) }
            _s.update { it.copy(wordInfo = w.copy(saved = true)) }
        }
    }

    fun startRecording(): Boolean {
        ctx.tts.stop(); ctx.player.stop()
        val ok = ctx.recorder.start()
        if (ok) {
            startedAt = System.currentTimeMillis()
            _s.update { it.copy(status = RecStatus.RECORDING, error = null) }
        } else _s.update { it.copy(error = "Не удалось начать запись") }
        return ok
    }

    /** Stops recording and runs ASR + phoneme model + scoring. onDone fires on success. */
    fun stopAndAssess(onDone: () -> Unit) {
        val st = _s.value
        val textId = st.text?.item?.id ?: return
        val reference = st.referenceNow
        val kind = if (st.mode == RecordMode.WHOLE) "reading" else "sentence"
        _s.update { it.copy(status = RecStatus.PROCESSING) }
        viewModelScope.launch {
            val durationMs = System.currentTimeMillis() - startedAt
            val pcm = ctx.recorder.stop()
            val parsed = withContext(Dispatchers.Default) {
                try {
                    val words: List<AsrWord> = ctx.asr.transcribe(pcm, SAMPLE_RATE, reference)
                    val post = ctx.phonemes.posteriors(pcm, SAMPLE_RATE)
                    val json = PronCore.assess(reference, words, post?.logPosteriors, post?.nFrames ?: 0,
                        post?.nClasses ?: 0, post?.frameSeconds ?: 0.02) ?: error(PronCore.lastError())
                    AssessmentUi.parse(json)
                } catch (e: Exception) { e }
            }
            if (parsed is AssessmentUi) {
                try { io { it.saveResult(textId, kind, durationMs, parsed) } } catch (_: Exception) {}
                _s.update { it.copy(status = RecStatus.IDLE, result = parsed, reference = reference, lastPcm = pcm) }
                onDone()
            } else {
                _s.update { it.copy(status = RecStatus.IDLE, error = "Ошибка оценки: ${(parsed as Exception).message}") }
            }
        }
    }

    fun playLast() = ctx.player.play(_s.value.lastPcm)
    fun nextSentence() { _s.update { it.copy(sentenceIdx = (it.sentenceIdx + 1).coerceAtMost((it.sentences.size - 1).coerceAtLeast(0))) } }
    override fun onCleared() { ctx.player.stop() }
}

// ---- Sounds ---------------------------------------------------------------------------------
class SoundsViewModel(app: Application) : BaseVm(app) {
    private val _list = MutableStateFlow<List<SoundSummary>>(emptyList())
    val list: StateFlow<List<SoundSummary>> = _list
    private val _card = MutableStateFlow<JSONObject?>(null)
    val card: StateFlow<JSONObject?> = _card

    init { viewModelScope.launch { _list.value = try { io { it.sounds() } } catch (e: Exception) { emptyList() } } }
    fun open(id: String) { viewModelScope.launch { _card.value = io { it.soundCard(id) } } }
    fun speak(text: String) = ctx.tts.speak(text, 1f)
}

// ---- Dictionary -----------------------------------------------------------------------------
data class DictState(val due: List<SavedWord> = emptyList(), val all: List<SavedWord> = emptyList())

class DictionaryViewModel(app: Application) : BaseVm(app) {
    private val _s = MutableStateFlow(DictState())
    val state: StateFlow<DictState> = _s
    fun refresh() = viewModelScope.launch {
        _s.value = try { io { DictState(it.savedWords(true), it.savedWords(false)) } } catch (e: Exception) { DictState() }
    }
    fun review(w: SavedWord, grade: Int) = viewModelScope.launch { io { it.review(w, grade) }; refresh() }
    fun delete(w: SavedWord) = viewModelScope.launch { io { it.deleteWord(w.id) }; refresh() }
    fun speak(text: String) = ctx.tts.speak(text, 1f)
}

// ---- Progress / Settings --------------------------------------------------------------------
class ProgressViewModel(app: Application) : BaseVm(app) {
    private val _s = MutableStateFlow<ProgressSummary?>(null)
    val state: StateFlow<ProgressSummary?> = _s
    fun refresh() = viewModelScope.launch { _s.value = try { io { it.progress() } } catch (e: Exception) { null } }
}

class SettingsViewModel(app: Application) : BaseVm(app) {
    val settings: StateFlow<UserSettings> = ctx.settings
    fun update(s: UserSettings) { viewModelScope.launch { ctx.updateSettings(s) } }
}
