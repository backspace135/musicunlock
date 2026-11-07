package io.github.backspace135.musicunlock.core

import android.Manifest
import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.app.Service
import android.content.Context
import android.content.Intent
import android.content.pm.PackageManager
import android.content.pm.ServiceInfo
import android.os.Build
import android.os.IBinder
import androidx.core.app.NotificationCompat
import androidx.core.app.NotificationManagerCompat
import androidx.core.app.ServiceCompat
import androidx.core.content.ContextCompat
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.cancel
import kotlinx.coroutines.flow.MutableSharedFlow
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.SharedFlow
import kotlinx.coroutines.flow.asSharedFlow
import kotlinx.coroutines.flow.update
import kotlinx.coroutines.launch

/**
 * Keeps conversions and lyric exports running while the app is in the background or the screen is locked: a
 * foreground service with one notification per running task, showing a title, a progress bar and a "停止" button.
 * The work itself runs in the caller's (app-scoped) coroutine; the service only keeps the process in the
 * foreground. Type `mediaProcessing` from Android 15, `dataSync` before.
 *
 * ```
 * ProcessingService.start(context, Task.UNLOCK, Progress(queue.statusTitle, 0, ids.size))
 * ProcessingService.update(Task.UNLOCK, Progress(queue.statusTitle, queue.batchFinished, queue.batchCount))
 * ProcessingService.stop(context, Task.UNLOCK)
 * // The notification's "停止" button:
 * ProcessingService.stopRequests.collect { task -> if (task == Task.UNLOCK) queue.requestStop() }
 * ```
 *
 * From Android 13 the notification needs the POST_NOTIFICATIONS permission ([notificationPermission]); without it
 * the work still runs, the notification is just not shown.
 */
class ProcessingService : Service() {
    /** What runs; each has its own notification and stop button. */
    enum class Task(internal val notificationId: Int) {
        UNLOCK(1001),
        LYRICS(1002),
    }

    /** What a task's notification shows: the core's status text and the batch progress. */
    data class Progress(val title: String, val done: Int, val total: Int)

    private val scope = CoroutineScope(SupervisorJob() + Dispatchers.Main.immediate)
    private var foregroundTask: Task? = null

    override fun onBind(intent: Intent?): IBinder? = null

    override fun onCreate() {
        super.onCreate()
        createChannel(this)
        scope.launch {
            tasks.collect { current -> show(current) }
        }
    }

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        intent?.getStringExtra(EXTRA_STOP)?.let { name ->
            Task.entries.firstOrNull { it.name == name }?.let { stops.tryEmit(it) }
        }
        show(tasks.value)
        return START_NOT_STICKY
    }

    override fun onDestroy() {
        scope.cancel()
        val manager = NotificationManagerCompat.from(this)
        Task.entries.forEach { manager.cancel(it.notificationId) }
        super.onDestroy()
    }

    private fun show(current: Map<Task, Progress>) {
        if (current.isEmpty()) {
            // A service started with startForegroundService must enter the foreground even when the work is
            // already over, or the system treats it as a failure.
            if (foregroundTask == null) {
                enterForeground(Task.UNLOCK, Progress("", 0, 0))
            }
            ServiceCompat.stopForeground(this, ServiceCompat.STOP_FOREGROUND_REMOVE)
            foregroundTask = null
            stopSelf()
            return
        }
        // One task carries the foreground state (and is updated through startForeground, which keeps it marked as
        // the service's); the others are plain ongoing notifications.
        val primary = foregroundTask?.takeIf { it in current } ?: current.keys.first()
        enterForeground(primary, current.getValue(primary))
        foregroundTask = primary
        val manager = NotificationManagerCompat.from(this)
        for (task in Task.entries) {
            val progress = current[task]
            if (progress == null) {
                manager.cancel(task.notificationId)
            } else if (task != primary && canNotify(this)) {
                @Suppress("MissingPermission")  // checked by canNotify
                manager.notify(task.notificationId, notification(task, progress))
            }
        }
    }

    private fun enterForeground(task: Task, progress: Progress) {
        // Not ServiceCompat.startForeground: it drops types its version does not know, such as mediaProcessing.
        val notification = notification(task, progress)
        when {
            Build.VERSION.SDK_INT >= 35 ->
                startForeground(task.notificationId, notification, ServiceInfo.FOREGROUND_SERVICE_TYPE_MEDIA_PROCESSING)
            Build.VERSION.SDK_INT >= 29 ->
                startForeground(task.notificationId, notification, ServiceInfo.FOREGROUND_SERVICE_TYPE_DATA_SYNC)
            else -> startForeground(task.notificationId, notification)
        }
    }

    private fun notification(task: Task, progress: Progress): Notification {
        val stop = PendingIntent.getService(this, task.notificationId,
            Intent(this, ProcessingService::class.java).putExtra(EXTRA_STOP, task.name),
            PendingIntent.FLAG_IMMUTABLE or PendingIntent.FLAG_UPDATE_CURRENT)
        val open = packageManager.getLaunchIntentForPackage(packageName)?.let {
            PendingIntent.getActivity(this, 0, it, PendingIntent.FLAG_IMMUTABLE)
        }
        return NotificationCompat.Builder(this, CHANNEL)
            .setSmallIcon(R.drawable.musicunlock_notification)
            .setContentTitle(progress.title)
            .setProgress(maxOf(1, progress.total), progress.done.coerceAtMost(maxOf(1, progress.total)), false)
            .setOngoing(true)
            .setOnlyAlertOnce(true)
            .setSilent(true)
            .setContentIntent(open)
            .addAction(0, "停止", stop)
            .setForegroundServiceBehavior(NotificationCompat.FOREGROUND_SERVICE_IMMEDIATE)
            .build()
    }

    companion object {
        private const val CHANNEL = "musicunlock-processing"
        private const val EXTRA_STOP = "io.github.backspace135.musicunlock.core.STOP"

        private val tasks = MutableStateFlow<Map<Task, Progress>>(emptyMap())
        private val stops = MutableSharedFlow<Task>(extraBufferCapacity = 8)

        /** Emits a task when its notification's "停止" button is pressed. Collect it where the task runs. */
        val stopRequests: SharedFlow<Task> = stops.asSharedFlow()

        /**
         * The runtime permission for the notification (Android 13+), or null when none is needed. Ask for it the first
         * time before starting; if the user declines, keep working without a notification.
         */
        val notificationPermission: String?
            get() = if (Build.VERSION.SDK_INT >= 33) Manifest.permission.POST_NOTIFICATIONS else null

        /** Starts the service (or adds the task to it). Call while the app is in the foreground, when work starts. */
        fun start(context: Context, task: Task, progress: Progress) {
            tasks.update { it + (task to progress) }
            ContextCompat.startForegroundService(context, Intent(context, ProcessingService::class.java))
        }

        /** Updates the task's notification. Ignored when the task was not started or has stopped. */
        fun update(task: Task, progress: Progress) {
            tasks.update { if (task in it) it + (task to progress) else it }
        }

        /** Removes the task's notification; the service stops when no task is left. */
        fun stop(context: Context, task: Task) {
            tasks.update { it - task }
            if (tasks.value.isEmpty()) context.stopService(Intent(context, ProcessingService::class.java))
        }

        internal fun canNotify(context: Context): Boolean =
            Build.VERSION.SDK_INT < 33 ||
                ContextCompat.checkSelfPermission(context, Manifest.permission.POST_NOTIFICATIONS) ==
                PackageManager.PERMISSION_GRANTED

        private fun createChannel(context: Context) {
            if (Build.VERSION.SDK_INT < 26) return
            val channel = NotificationChannel(CHANNEL, "转换与导出", NotificationManager.IMPORTANCE_LOW).apply {
                description = "转换音乐或导出歌词时显示进度"
                setShowBadge(false)
            }
            context.getSystemService(NotificationManager::class.java).createNotificationChannel(channel)
        }
    }
}
