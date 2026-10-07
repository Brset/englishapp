package app.englishpron

import android.app.Application
import app.englishpron.audio.Player
import app.englishpron.audio.Recorder
import app.englishpron.audio.TtsSpeaker
import app.englishpron.data.AppDatabase
import app.englishpron.engine.EngineHost
import app.englishpron.engine.ProcessingQueue
import app.englishpron.engine.Speaker
import kotlinx.coroutines.CompletableDeferred
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import org.json.JSONObject

data class UserSettings(
    val british: Boolean = false, val strictness: Int = 1,
    /** false until user.db settings are read (the UI shows nothing instead of flashing onboarding). */
    val loaded: Boolean = false, val onboarded: Boolean = false,
    val level: String = "A2", val dailyGoal: Int = 10,
    /** Reading text: size 0..3 (S/M/L/XL), line spacing 0..2, serif face, auto-advance to the next paragraph. */
    val fontSize: Int = 1, val lineSpacing: Int = 1, val serif: Boolean = false, val autoAdvance: Boolean = true,
)

/** Process-wide singletons (poor man's DI). */
class EnglishApp : Application() {
    private val dbDeferred = CompletableDeferred<AppDatabase>()
    private val _settings = MutableStateFlow(UserSettings())
    val settings: StateFlow<UserSettings> = _settings

    lateinit var tts: Speaker
    lateinit var engine: EngineHost
    val queue = ProcessingQueue(this)
    val recorder = Recorder()
    val player = Player()

    suspend fun db(): AppDatabase = dbDeferred.await()

    override fun onCreate() {
        super.onCreate()
        engine = EngineHost(this)
        tts = Speaker(engine, player, TtsSpeaker(this))
        engine.start()
        CoroutineScope(Dispatchers.IO).launch {
            try {
                val db = AppDatabase.open(this@EnglishApp)
                try { loadCore() } catch (e: Throwable) { android.util.Log.w("EnglishApp", "core assets not loaded", e) }
                var onboarded = db.setting("onboarded", "")
                if (onboarded.isEmpty()) {  // existing users (before onboarding existed) skip it
                    onboarded = if (db.progress().attempts > 0) "1" else "0"
                    if (onboarded == "1") db.putSetting("onboarded", "1")
                }
                val s = UserSettings(
                    british = db.setting("accent", "us") == "uk", strictness = db.setting("strictness", "1").toIntOrNull() ?: 1,
                    loaded = true, onboarded = onboarded == "1", level = db.setting("level", "A2"),
                    dailyGoal = db.setting("daily_goal", "10").toIntOrNull() ?: 10,
                    fontSize = db.setting("font_size", "1").toIntOrNull()?.coerceIn(0, 3) ?: 1,
                    lineSpacing = db.setting("line_spacing", "1").toIntOrNull()?.coerceIn(0, 2) ?: 1,
                    serif = db.setting("serif", "0") == "1", autoAdvance = db.setting("auto_advance", "1") == "1",
                )
                applySettings(s)
                dbDeferred.complete(db)
                queue.start()  // re-queued jobs (processing -> queued) continue from here
            } catch (e: Throwable) {
                dbDeferred.completeExceptionally(e)
            }
        }
    }

    /** Optional assets: cmudict.dict (CMUdict text), phoneme_vocab.json ({"labels":[...],"blank":N}). */
    private fun loadCore() {
        val names = assets.list("")?.toSet() ?: emptySet()
        if ("cmudict.dict" in names) PronCore.loadCmudict(assets.open("cmudict.dict").use { it.readBytes() })
        if ("phoneme_vocab.json" in names) {
            val o = JSONObject(assets.open("phoneme_vocab.json").bufferedReader().readText())
            val arr = o.getJSONArray("labels")
            PronCore.setVocab(List(arr.length()) { arr.getString(it) }, o.optInt("blank", 0))
        }
    }

    private fun applySettings(s: UserSettings) {
        _settings.value = s
        PronCore.setStrictness(s.strictness)
        engine.setStrictness(s.strictness)
        tts.setBritish(s.british)
    }

    suspend fun updateSettings(s: UserSettings) {
        applySettings(s)
        val db = db()
        withContext(Dispatchers.IO) {
            db.putSetting("accent", if (s.british) "uk" else "us")
            db.putSetting("strictness", s.strictness.toString())
            db.putSetting("onboarded", if (s.onboarded) "1" else "0")
            db.putSetting("level", s.level)
            db.putSetting("daily_goal", s.dailyGoal.toString())
            db.putSetting("font_size", s.fontSize.toString())
            db.putSetting("line_spacing", s.lineSpacing.toString())
            db.putSetting("serif", if (s.serif) "1" else "0")
            db.putSetting("auto_advance", if (s.autoAdvance) "1" else "0")
        }
    }
}
