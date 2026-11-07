package io.github.backspace135.musicunlock.core

import java.util.concurrent.CancellationException

/**
 * An error from the shared core. [message] is the user-facing Chinese text exactly as the core wrote it, so it can
 * be shown as is (it matches the Windows and macOS apps).
 */
open class CoreException(message: String) : Exception(message) {
    override val message: String get() = super.message.orEmpty()
}

/** The core's `DecodeError`: a file that could not be converted. */
class DecodeException(val kind: Kind, message: String) : CoreException(message) {
    enum class Kind {
        /** The file is damaged, truncated or not what its extension says. */
        INVALID,

        /** A format or encryption variant this version cannot convert. */
        UNSUPPORTED,
    }

    // Called from JNI with Kind.ordinal.
    internal constructor(kind: Int, message: String) : this(Kind.entries[kind], message)

    // Read from JNI when a Kotlin callback throws this back into the core.
    @JvmName("kindCode")
    internal fun kindCode(): Int = kind.ordinal
}

/** The core's `LyricError`: a network, platform or lyric problem the user can act on. */
class LyricException(message: String) : CoreException(message)

/**
 * The core's `Cancelled` ("已取消"). It is a [CancellationException], so coroutines treat it as cancellation:
 * when a request was cancelled on purpose there is nothing to show.
 */
class CoreCancelledException : CancellationException("已取消")
