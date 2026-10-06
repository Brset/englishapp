package app.englishpron.engine

import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.app.Service
import android.content.Intent
import android.content.pm.ServiceInfo
import android.os.Build
import android.os.IBinder
import androidx.core.app.NotificationCompat
import androidx.core.app.ServiceCompat
import app.englishpron.EnglishApp
import app.englishpron.MainActivity
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.cancel
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.collectLatest
import kotlinx.coroutines.launch

/**
 * Foreground service (type dataSync) that keeps the process alive and shows
 * "Обработка: <название> — 42% · осталось 0:35" while the queue is non-empty; it stops itself when
 * the queue is empty. The work itself runs in [ProcessingQueue].
 */
class ProcessingService : Service() {
    private val scope = CoroutineScope(SupervisorJob() + Dispatchers.Main.immediate)
    private var watcher: Job? = null
    private var lastStartId = 0

    override fun onBind(intent: Intent?): IBinder? = null

    override fun onCreate() {
        super.onCreate()
        if (Build.VERSION.SDK_INT >= 26) {
            val ch = NotificationChannel(CHANNEL, "Обработка записей", NotificationManager.IMPORTANCE_LOW)
            getSystemService(NotificationManager::class.java).createNotificationChannel(ch)
        }
    }

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        lastStartId = startId
        val queue = (application as EnglishApp).queue
        val first = queue.jobs.value
        try {
            ServiceCompat.startForeground(this, NOTIF_ID, build(first.firstOrNull()),
                if (Build.VERSION.SDK_INT >= 29) ServiceInfo.FOREGROUND_SERVICE_TYPE_DATA_SYNC else 0)
        } catch (_: Throwable) { stopSelf(); return START_NOT_STICKY }
        if (watcher == null) watcher = scope.launch {
            queue.jobs.collectLatest { list ->
                if (list.isEmpty()) {
                    delay(1500)  // grace: the next job may be about to appear
                    ServiceCompat.stopForeground(this@ProcessingService, ServiceCompat.STOP_FOREGROUND_REMOVE)
                    stopSelf(lastStartId)  // no-op if a newer start command arrived meanwhile
                } else {
                    getSystemService(NotificationManager::class.java).notify(NOTIF_ID, build(list.first()))
                    delay(700)  // throttle notification updates
                }
            }
        }
        return START_NOT_STICKY
    }

    // Android 15: dataSync services have a time limit; the queue keeps running while the app lives.
    override fun onTimeout(startId: Int, fgsType: Int) { stopSelf() }

    override fun onDestroy() { scope.cancel(); super.onDestroy() }

    private fun build(job: JobUi?): Notification {
        val text = when {
            job == null -> "Обработка записей"
            job.running -> {
                val pct = (job.progress * 100).toInt()
                val eta = if (job.etaSec >= 0) job.etaSec else job.estimateSec
                "Обработка: ${job.title} — $pct%" + if (eta >= 0) " · осталось ${formatMmSs(eta)}" else ""
            }
            else -> "Обработка: ${job.title} — в очереди"
        }
        val open = PendingIntent.getActivity(this, 0, Intent(this, MainActivity::class.java)
            .addFlags(Intent.FLAG_ACTIVITY_SINGLE_TOP or Intent.FLAG_ACTIVITY_CLEAR_TOP), PendingIntent.FLAG_IMMUTABLE or PendingIntent.FLAG_UPDATE_CURRENT)
        val b = NotificationCompat.Builder(this, CHANNEL)
            .setSmallIcon(android.R.drawable.stat_notify_sync)
            .setContentTitle("English Pron")
            .setContentText(text)
            .setStyle(NotificationCompat.BigTextStyle().bigText(text))
            .setContentIntent(open)
            .setOngoing(true)
            .setOnlyAlertOnce(true)
            .setSilent(true)
            .setCategory(NotificationCompat.CATEGORY_PROGRESS)
            .setForegroundServiceBehavior(NotificationCompat.FOREGROUND_SERVICE_IMMEDIATE)
        if (job != null && job.running) b.setProgress(100, (job.progress * 100).toInt(), false)
        else b.setProgress(0, 0, true)
        return b.build()
    }

    private companion object {
        const val CHANNEL = "processing"
        const val NOTIF_ID = 4711
    }
}
