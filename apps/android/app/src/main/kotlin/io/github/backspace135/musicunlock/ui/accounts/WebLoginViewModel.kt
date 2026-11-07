package io.github.backspace135.musicunlock.ui.accounts

import android.os.Handler
import android.os.Looper
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import androidx.lifecycle.ViewModel
import androidx.lifecycle.viewModelScope
import io.github.backspace135.musicunlock.core.AccountStore
import io.github.backspace135.musicunlock.core.MusicProvider
import kotlinx.coroutines.Job
import kotlinx.coroutines.delay
import kotlinx.coroutines.isActive
import kotlinx.coroutines.launch

/**
 * Official login page. Cookie reads never close the page themselves: acceptance posts the close onto the next
 * turn of the main looper and checks [LoginGeneration] there (WebView must not be destroyed inside its callback).
 */
class WebLoginViewModel : ViewModel() {
    var loading by mutableStateOf(true)
        private set
    var failed by mutableStateOf(false)
        private set
    var pageVisible by mutableStateOf(false)
        private set
    var reload by mutableIntStateOf(0)
        private set

    /** Set by the screen; cleared when the page leaves. Invoked only from a posted main-thread runnable. */
    var onClose: (() -> Unit)? = null

    private val generation = LoginGeneration()
    private val gate = SessionReadGate()
    private var poll: Job? = null
    private var token = 0
    private var provider: MusicProvider? = null
    private var everShown = false
    private var accepted = false
    private var closePosted = false
    private var prepared = false

    val loadToken: Int get() = token

    /**
     * Called once, on the main thread, before the WebView's first load. A re-login expires the music keys first.
     */
    fun prepare(provider: MusicProvider, relogin: Boolean) {
        if (prepared) return
        prepared = true
        this.provider = provider
        token = generation.next()
        if (relogin) WebSession.expireMusicKeys(provider)
        poll = viewModelScope.launch {
            while (isActive) {
                delay(1_000)
                read(token)
            }
        }
    }

    fun pageFinished(started: Int) {
        if (!generation.isCurrent(started)) return
        everShown = true
        loading = false
        failed = false
        pageVisible = true
        read(started)
    }

    fun mainFrameError(started: Int, errorCode: Int, description: CharSequence?) {
        if (!generation.isCurrent(started) || everShown) return
        if (WebLoginPolicy.isCancelledLoad(errorCode, description)) return
        loading = false
        failed = true
        pageVisible = false
    }

    fun refresh() {
        token = generation.next()
        gate.reset()
        accepted = false
        closePosted = false
        failed = false
        loading = !everShown
        pageVisible = everShown
        reload += 1
    }

    fun cancel() {
        generation.next()
        poll?.cancel()
        poll = null
        onClose = null
    }

    override fun onCleared() {
        cancel()
    }

    private fun read(started: Int) {
        val provider = provider ?: return
        if (!generation.isCurrent(started) || accepted) return
        val cookies = runCatching { WebSession.read(provider) }.getOrNull() ?: return
        val valid = runCatching { AccountStore.isMusicSession(provider, cookies) }.getOrDefault(false)
        if (!gate.observe(valid)) return
        val message = runCatching { AccountStore.accept(provider, cookies) }.getOrNull() ?: return
        accepted = true
        poll?.cancel()
        AccountFeedback.show(message)
        val expected = started
        Handler(Looper.getMainLooper()).post {
            if (!generation.isCurrent(expected) || closePosted) return@post
            closePosted = true
            onClose?.invoke()
        }
    }
}
