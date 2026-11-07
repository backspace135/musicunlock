package io.github.backspace135.musicunlock.core

import android.content.Context

/**
 * The shared C++ core (libmusicunlock.so) and the app context it works with. Nothing needs to be called here:
 * [CoreInitializer] runs before `Application.onCreate`, and the library loads on first use.
 * See apps/android/README.md for the API overview.
 */
object MusicUnlockCore {
    @Volatile
    private var appContext: Context? = null

    internal val context: Context
        get() = appContext ?: error("MusicUnlockCore 尚未初始化：CoreInitializer 没有运行")

    init {
        System.loadLibrary("musicunlock")
    }

    /** Loads the native library (through `init`); called by every class with native methods. */
    internal fun load() = Unit

    internal fun attach(context: Context) {
        appContext = context.applicationContext
    }
}
