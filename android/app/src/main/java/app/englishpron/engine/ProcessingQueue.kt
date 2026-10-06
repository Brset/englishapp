package app.englishpron.engine

import android.content.Intent
import androidx.core.content.ContextCompat
import app.englishpron.EnglishApp
import app.englishpron.audio.SAMPLE_RATE
import app.englishpron.audio.Wav
import app.englishpron.data.AssessmentUi
import app.englishpron.data.JobRow
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.channels.Channel
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.first
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import java.io.File
import java.util.concurrent.ConcurrentHashMap

/** One entry of the "Обработка" panel. */
data class JobUi(
    val id: Long, val textId: String, val title: String, val running: Boolean,
    val progress: Float, val etaSec: Double, val estimateSec: Double, val audioSeconds: Double, val stage: String,
)

fun formatMmSs(sec: Double): String {
    val s = Math.round(sec.coerceAtLeast(0.0)).toInt()
    return "%d:%02d".format(s / 60, s % 60)
}

/**
 * Persistent background queue of heavy assessments. The truth lives in the `processing_jobs` table (so it
 * survives rotation and process death); this class runs one job at a time on the engine dispatcher and
 * publishes progress through [jobs]. The foreground [ProcessingService] only keeps the process alive and
 * shows the notification while [jobs] is non-empty.
 */
class ProcessingQueue(private val app: EnglishApp) {
    private val scope = CoroutineScope(SupervisorJob() + Dispatchers.Default)
    private val kick = Channel<Unit>(Channel.CONFLATED)

    private val _jobs = MutableStateFlow<List<JobUi>>(emptyList())
    /** Queued + running jobs, oldest first; the running one (if any) is first. */
    val jobs: StateFlow<List<JobUi>> = _jobs

    private val _version = MutableStateFlow(0)
    /** Bumped on every status change (enqueue/start/done/failed/cancelled): lists refresh from it. */
    val version: StateFlow<Int> = _version

    @Volatile private var rows: List<JobRow> = emptyList()
    private val estimates = ConcurrentHashMap<Long, Double>()
    @Volatile private var runId = -1L
    @Volatile private var runFraction = 0f
    @Volatile private var runEta = -1.0
    @Volatile private var runStage = ""
    @Volatile private var cancelRequested = -1L

    fun start() {
        scope.launch {
            val db = app.db()
            reload(db)
            val st = app.engine.state.first { it.phase == EnginePhase.READY || it.phase == EnginePhase.FAILED }
            if (st.phase != EnginePhase.READY) return@launch  // jobs stay queued for the next start
            while (true) {
                val next = rows.firstOrNull { it.status == "queued" }
                if (next == null) { kick.receive(); reload(db); continue }
                runJob(db, next)
            }
        }
    }

    /** Called after a job row was inserted. */
    fun onEnqueued() {
        scope.launch { reload(app.db()); kick.trySend(Unit) }
    }

    fun cancel(id: Long) {
        scope.launch {
            if (id == runId) {
                cancelRequested = id
                app.engine.cancelHeavy()
            } else {
                withContext(Dispatchers.IO) { app.db().jobCancel(id) }
                reload(app.db())
            }
        }
    }

    private suspend fun reload(db: app.englishpron.data.AppDatabase) {
        rows = withContext(Dispatchers.IO) { db.activeJobs() }
        _version.value++
        publish()
        if (rows.isNotEmpty()) ensureService()
        refreshEstimates()
    }

    private fun publish() {
        val r = rows
        val rid = runId
        _jobs.value = r.map { j ->
            val running = j.id == rid
            JobUi(j.id, j.textId, j.title, running,
                if (running) runFraction else 0f, if (running) runEta else -1.0,
                estimates[j.id] ?: -1.0, j.audioSeconds, if (running) runStage else "")
        }.sortedByDescending { it.running }
    }

    /** Estimates for queued items; asks the engine only when it is idle (otherwise the last known ratio). */
    private suspend fun refreshEstimates() {
        var changed = false
        for (j in rows) {
            if (j.id == runId || estimates.containsKey(j.id)) continue
            val e = try { app.engine.estimateSeconds(j.audioSeconds) } catch (_: Throwable) { continue }
            if (e > 0) { estimates[j.id] = e; changed = true }
        }
        if (changed) publish()
    }

    private fun ensureService() {
        try { ContextCompat.startForegroundService(app, Intent(app, ProcessingService::class.java)) }
        catch (_: Throwable) { /* not allowed from background: the queue still runs while the process lives */ }
    }

    private suspend fun runJob(db: app.englishpron.data.AppDatabase, job: JobRow) {
        cancelRequested = -1L
        withContext(Dispatchers.IO) { db.jobStart(job.id) }
        runId = job.id; runFraction = 0f; runEta = -1.0; runStage = ""
        _version.value++
        publish()
        ensureService()
        val ticker = scope.launch {
            var n = 0
            while (true) {
                delay(500)
                if (cancelRequested == job.id) app.engine.cancelHeavy()  // covers a cancel that raced the native start
                if (++n % 4 == 0) try { withContext(Dispatchers.IO) { db.jobProgress(job.id, runFraction.toDouble(), runEta) } } catch (_: Throwable) {}
            }
        }
        try {
            val pcm = withContext(Dispatchers.IO) {
                val f = File(job.wavPath)
                if (!f.isFile) throw IllegalStateException("файл записи не найден")
                Wav.read(f)
            }
            if (cancelRequested == job.id) throw AssessCancelled()
            val json = app.engine.assessProgress(pcm, SAMPLE_RATE, job.reference) { stage, fraction, eta ->
                runStage = stage
                runFraction = fraction.toFloat().coerceIn(0f, 1f)
                runEta = eta
                publish()
            }
            if (cancelRequested == job.id) throw AssessCancelled()
            val parsed = withContext(Dispatchers.Default) { AssessmentUi.parse(json) }
            ticker.cancel()
            withContext(Dispatchers.IO) { db.jobDone(job, json, parsed) }
        } catch (e: AssessCancelled) {
            withContext(Dispatchers.IO) { db.jobCancel(job.id) }
        } catch (e: CancellationException) {
            withContext(kotlinx.coroutines.NonCancellable + Dispatchers.IO) { db.jobRequeue(job.id) }
            throw e
        } catch (e: Throwable) {
            val msg = e.message ?: e.toString()
            withContext(Dispatchers.IO) { if (cancelRequested == job.id) db.jobCancel(job.id) else db.jobFail(job.id, msg) }
        } finally {
            ticker.cancel()
            runId = -1L; cancelRequested = -1L
            estimates.clear()  // the engine has fresh timings now
            reload(db)
        }
    }
}
