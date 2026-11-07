package io.github.backspace135.musicunlock.ui.unlock

import android.content.Context
import android.content.Intent
import android.net.Uri
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import io.github.backspace135.musicunlock.R
import io.github.backspace135.musicunlock.core.AudioConverter
import io.github.backspace135.musicunlock.core.AudioQueue
import io.github.backspace135.musicunlock.core.CoreException
import io.github.backspace135.musicunlock.core.DocumentImporter
import io.github.backspace135.musicunlock.core.ImportScan
import io.github.backspace135.musicunlock.core.JobState
import io.github.backspace135.musicunlock.core.OutputFolders
import io.github.backspace135.musicunlock.core.ProcessingService
import io.github.backspace135.musicunlock.core.QueueFilter
import io.github.backspace135.musicunlock.core.UnlockSettings
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.MainScope
import kotlinx.coroutines.launch

/**
 * App-scoped unlock queue. The activity (and its ViewModel) can be destroyed while a batch runs —
 * "不保留活动" — so the queue, the batch and the notice live here, on the main thread.
 *
 * Wording, counts and job state all come from [AudioQueue]. This object only walks the batch.
 */
object UnlockSession {
    val queue = AudioQueue()
    private val scope = MainScope()

    /** Banner text: the value returned by [AudioQueue.add] or [AudioQueue.endBatch], or a folder warning. */
    var notice by mutableStateOf("")

    var scanning by mutableStateOf(false)
    var filter by mutableStateOf(QueueFilter.ALL)

    /** Bumped when a share / view intent should bring the unlock workspace to the front. */
    var openRequest by mutableIntStateOf(0)
        private set

    /** Notification permission is asked at most once per process; a refusal still converts. */
    var notificationsAsked by mutableStateOf(false)

    init {
        scope.launch {
            ProcessingService.stopRequests.collect { task ->
                if (task == ProcessingService.Task.UNLOCK) stop()
            }
        }
    }

    fun importDocuments(uris: List<Uri>) {
        if (uris.isEmpty() || scanning) return
        scope.launch {
            scanning = true
            try {
                add(DocumentImporter.scanDocuments(uris))
            } catch (error: CancellationException) {
                throw error
            } catch (error: CoreException) {
                notice = error.message.orEmpty()
            } finally {
                scanning = false
            }
        }
    }

    fun importTree(tree: Uri) {
        if (scanning) return
        scope.launch {
            scanning = true
            try {
                add(DocumentImporter.scanTree(tree))
            } catch (error: CancellationException) {
                throw error
            } catch (error: CoreException) {
                notice = error.message.orEmpty()
            } finally {
                scanning = false
            }
        }
    }

    /** Share / view intent. Null when the intent carries no file. Runs on the app scope so a
     *  recreated activity does not drop the copy into the cache. */
    fun importIntent(intent: Intent) {
        scope.launch {
            val scan = try {
                DocumentImporter.scanIntent(intent)
            } catch (error: CancellationException) {
                throw error
            } catch (error: CoreException) {
                notice = error.message.orEmpty()
                null
            } ?: return@launch
            add(scan)
            openRequest++
        }
    }

    fun start(context: Context) {
        if (queue.running || scanning || queue.count(JobState.WAITING) == 0) return
        if (UnlockSettings.fallBackIfUnusable()) {
            notice = context.getString(
                R.string.unlock_output_fallback_format,
                OutputFolders.label(UnlockSettings.values.value.outputFolder).name,
            )
            return
        }
        val (folder, naming) = UnlockSettings.values.value
        if (!OutputFolders.isUsable(folder)) {
            notice = context.getString(R.string.unlock_output_unwritable_format, OutputFolders.label(folder).name)
            return
        }
        val ids = queue.beginBatch()
        if (ids.isEmpty()) return
        notice = ""
        val task = ProcessingService.Task.UNLOCK
        val app = context.applicationContext
        ProcessingService.start(app, task, ProcessingService.Progress(queue.statusTitle, 0, ids.size))
        scope.launch {
            try {
                for (id in ids) {
                    if (queue.stopRequested) break
                    val job = queue.find(id) ?: continue
                    queue.jobStarted(id)
                    ProcessingService.update(
                        task,
                        ProcessingService.Progress(queue.statusTitle, queue.batchFinished, queue.batchCount),
                    )
                    try {
                        queue.jobSucceeded(id, AudioConverter.convert(job.source, folder, naming))
                    } catch (error: CancellationException) {
                        throw error
                    } catch (error: Exception) {
                        queue.jobFailed(id, AudioQueue.failureMessage(error))
                    }
                }
            } finally {
                if (queue.running) notice = queue.endBatch()
                ProcessingService.stop(app, task)
            }
        }
    }

    fun stop() {
        if (queue.running) queue.requestStop()
    }

    fun retryFailed(context: Context) {
        if (!queue.retryFailed()) return
        filter = QueueFilter.ALL
        start(context)
    }

    fun remove(id: Long) {
        queue.remove(id)
    }

    fun clearFinished() {
        queue.clearCompleted()
    }

    fun clear() {
        queue.clear()
        notice = ""
    }

    /** Call when the output section is shown, so a revoked folder is replaced before the user starts. */
    fun fallBackIfOutputUnusable(context: Context) {
        if (!UnlockSettings.fallBackIfUnusable()) return
        notice = context.getString(
            R.string.unlock_output_fallback_format,
            OutputFolders.label(UnlockSettings.values.value.outputFolder).name,
        )
    }

    private fun add(scan: ImportScan) {
        notice = queue.add(scan)
        filter = QueueFilter.ALL
    }
}
