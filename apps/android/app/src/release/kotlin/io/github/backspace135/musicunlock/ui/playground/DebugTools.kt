package io.github.backspace135.musicunlock.ui.playground

import androidx.compose.runtime.Composable

/** Release builds have no playground; the settings entry is hidden. */
object DebugTools {
    const val available = false

    @Composable
    fun Playground() {}
}
