package io.github.backspace135.musicunlock.core

import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.coroutineScope
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import okhttp3.Call
import java.util.concurrent.atomic.AtomicBoolean

/**
 * Cancels a running core request (music_api.h `CancelToken`): the core stops between requests, and the HTTP request
 * in flight is cancelled too. Use one per request, as the Windows app does: cancel the old token and make a new one
 * before each new search, lyrics fetch or export. Cancelling the coroutine that runs a request cancels it as well.
 * A cancelled request throws [CoreCancelledException]; show nothing for it.
 */
class CancelToken {
    private val lock = Any()
    private val cancelled = AtomicBoolean(false)
    private val natives = mutableSetOf<Long>()  // guarded by lock
    private val calls = mutableSetOf<Call>()    // guarded by lock

    val isCancelled: Boolean get() = cancelled.get()

    fun cancel() {
        if (!cancelled.compareAndSet(false, true)) return
        val (tokens, inFlight) = synchronized(lock) { natives.toList() to calls.toList() }
        tokens.forEach(::nativeCancel)
        inFlight.forEach(Call::cancel)
    }

    /** Runs [block] with a native token that follows this one, current on this thread for the HTTP transport. */
    internal fun <T> run(block: (Long) -> T): T {
        val native = nativeCreate()
        synchronized(lock) { natives += native }
        if (cancelled.get()) nativeCancel(native)
        val previous = current.get()
        current.set(this)
        try {
            return block(native)
        } finally {
            current.set(previous)
            synchronized(lock) { natives -= native }
            nativeDestroy(native)
        }
    }

    internal fun register(call: Call) {
        synchronized(lock) { calls += call }
        if (cancelled.get()) call.cancel()
    }

    internal fun unregister(call: Call) {
        synchronized(lock) { calls -= call }
    }

    internal companion object {
        init {
            MusicUnlockCore.load()
        }

        /** The token of the request running on this thread, for OkHttpTransport. */
        val current = ThreadLocal<CancelToken?>()

        @JvmStatic private external fun nativeCreate(): Long
        @JvmStatic private external fun nativeCancel(token: Long)
        @JvmStatic private external fun nativeDestroy(token: Long)
    }
}

/**
 * Runs a blocking core call on [Dispatchers.IO] with [token]; cancelling the calling coroutine cancels the token,
 * so the core and the HTTP request stop instead of running to the end.
 */
internal suspend fun <T> blockingCall(token: CancelToken, block: (Long) -> T): T = coroutineScope {
    val finished = AtomicBoolean(false)
    val watcher = launch {
        try {
            kotlinx.coroutines.awaitCancellation()
        } finally {
            if (!finished.get()) token.cancel()
        }
    }
    try {
        withContext(Dispatchers.IO) { token.run(block) }
    } catch (error: CancellationException) {
        throw error
    } finally {
        finished.set(true)
        watcher.cancel()
    }
}
