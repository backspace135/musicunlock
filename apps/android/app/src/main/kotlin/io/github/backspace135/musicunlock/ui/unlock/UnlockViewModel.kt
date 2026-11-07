package io.github.backspace135.musicunlock.ui.unlock

import android.content.Context
import android.content.Intent
import android.net.Uri
import androidx.lifecycle.ViewModel
import io.github.backspace135.musicunlock.core.AudioQueue
import io.github.backspace135.musicunlock.core.QueueFilter

/**
 * Forwards the unlock screen to [UnlockSession]. The queue itself is not kept here: the activity can
 * be destroyed mid-batch and the session has to keep running.
 */
class UnlockViewModel : ViewModel() {
    val queue: AudioQueue get() = UnlockSession.queue

    var filter: QueueFilter
        get() = UnlockSession.filter
        set(value) {
            UnlockSession.filter = value
        }

    val notice: String get() = UnlockSession.notice
    val scanning: Boolean get() = UnlockSession.scanning
    val notificationsAsked: Boolean get() = UnlockSession.notificationsAsked

    fun dismissNotice() {
        UnlockSession.notice = ""
    }

    fun markNotificationsAsked() {
        UnlockSession.notificationsAsked = true
    }

    fun importDocuments(uris: List<Uri>) = UnlockSession.importDocuments(uris)

    fun importTree(tree: Uri) = UnlockSession.importTree(tree)

    fun importIntent(intent: Intent) = UnlockSession.importIntent(intent)

    fun start(context: Context) = UnlockSession.start(context)

    fun stop() = UnlockSession.stop()

    fun retryFailed(context: Context) = UnlockSession.retryFailed(context)

    fun remove(id: Long) = UnlockSession.remove(id)

    fun clearFinished() = UnlockSession.clearFinished()

    fun clear() = UnlockSession.clear()
}
