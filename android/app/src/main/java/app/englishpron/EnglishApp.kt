package app.englishpron

import android.app.Application
import app.englishpron.audio.Player
import app.englishpron.audio.Recorder
import app.englishpron.audio.TtsSpeaker
import app.englishpron.data.AppDatabase
import app.englishpron.engine.EngineHost
import app.englishpron.engine.Speaker
import kotlinx.coroutines.CompletableDeferred
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.launch
import org.json.JSONObject

data class UserSettings(val british: Boolean = false, val strictness: Int = 1)

/** Process-wide singletons (poor man's DI). */
class EnglishApp : Application() {
    private val dbDeferred = CompletableDeferred<AppDatabase>()
    private val _settings = MutableStateFlow(UserSettings())
    val settings: StateFlow<UserSettings> = _settings

    lateinit var tts: Speaker
    lateinit var engine: EngineHost
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
                loadCore()
                val s = UserSettings(db.setting("accent", "us") == "uk", db.setting("strictness", "1").toIntOrNull() ?: 1)
                applySettings(s)
                dbDeferred.complete(db)
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
        db.putSetting("accent", if (s.british) "uk" else "us")
        db.putSetting("strictness", s.strictness.toString())
    }
}
