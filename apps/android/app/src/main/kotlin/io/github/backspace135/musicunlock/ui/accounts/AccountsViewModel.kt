package io.github.backspace135.musicunlock.ui.accounts

import android.content.Context
import android.net.Uri
import android.util.Log
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import androidx.lifecycle.ViewModel
import androidx.lifecycle.viewModelScope
import io.github.backspace135.musicunlock.core.AccountStore
import io.github.backspace135.musicunlock.core.MusicProvider
import io.github.backspace135.musicunlock.core.QrLogin
import io.github.backspace135.musicunlock.core.QrLoginState
import io.github.backspace135.musicunlock.core.QrLoginUpdate
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext

/** Accounts page: NetEase QR login, logout, and the notice line. Web login has its own model. */
class AccountsViewModel : ViewModel() {
    var qrVisible by mutableStateOf(false)
        private set
    var qr by mutableStateOf<QrLoginUpdate?>(null)
        private set
    var qrSaved by mutableStateOf(false)
        private set
    var qrNotice by mutableStateOf<String?>(null)
        private set

    private var qrJob: Job? = null
    private val generation = LoginGeneration()
    private var savedQr: Uri? = null
    private var appContext: Context? = null
    private var invalidSession: String = ""
    private var accepted = false

    /** Remembered for the process so a denied storage permission is not requested again. */
    var storageDenied: Boolean
        get() = storageDeniedOnce
        set(value) { storageDeniedOnce = value }

    fun bind(context: Context, invalidSession: String) {
        appContext = context.applicationContext
        this.invalidSession = invalidSession
    }

    fun openQr() {
        qrVisible = true
        startQr()
    }

    fun startQr() {
        val token = generation.next()
        qrJob?.cancel()
        accepted = false
        qrSaved = false
        qrNotice = null
        discardSavedQr()
        qr = QrLoginUpdate(QrLoginState.LOADING, QrLoginState.LOADING.defaultMessage)
        qrJob = viewModelScope.launch {
            QrLogin().start().collect { update ->
                withContext(Dispatchers.Main.immediate) { onUpdate(token, update) }
            }
            withContext(Dispatchers.Main.immediate) {
                if (generation.isCurrent(token) && qr?.state == QrLoginState.CONFIRMED && !accepted) {
                    qrNotice = invalidSession
                    AccountFeedback.show(invalidSession)
                }
            }
        }
    }

    fun closeQr() {
        generation.next()
        qrVisible = false
        qrJob?.cancel()
        qrJob = null
        discardSavedQr()
        qrSaved = false
        qrNotice = null
        Log.i(TAG, "qr login stopped")
    }

    /**
     * Saves the current code (white, with its quiet zone) and remembers the uri so it can be deleted.
     * @return false when the gallery rejected the insert.
     */
    fun saveQr(context: Context): Boolean {
        val matrix = qr?.matrix ?: return false
        val bitmap = matrix.toBitmap()
        val uri = try {
            QrGallery.save(context, bitmap)
        } finally {
            bitmap.recycle()
        } ?: return false
        val previous = savedQr
        savedQr = uri
        if (previous != null && previous != uri) QrGallery.delete(context, previous)
        qrSaved = true
        return true
    }

    fun logout(provider: MusicProvider) {
        WebSession.clearPlatform(provider)
        AccountFeedback.show(AccountStore.logout(provider))
    }

    private fun onUpdate(token: Int, update: QrLoginUpdate) {
        if (!generation.isCurrent(token)) return
        Log.i(TAG, "qr ${update.state}")
        qr = update
        if (update.state == QrLoginState.EXPIRED) discardSavedQr()
        if (update.state != QrLoginState.CONFIRMED || update.session.isEmpty() || accepted) return
        val line = AccountStore.accept(MusicProvider.NETEASE, update.session) ?: invalidSession
        if (line == invalidSession) {
            qrNotice = line
            AccountFeedback.show(line)
            return
        }
        accepted = true
        AccountFeedback.show(line)
        discardSavedQr()
        viewModelScope.launch {
            delay(800)
            if (generation.isCurrent(token)) closeQr()
        }
    }

    private fun discardSavedQr() {
        val uri = savedQr ?: return
        savedQr = null
        qrSaved = false
        appContext?.let { QrGallery.delete(it, uri) }
    }

    private companion object {
        const val TAG = "Accounts"
        var storageDeniedOnce: Boolean = false
    }
}
