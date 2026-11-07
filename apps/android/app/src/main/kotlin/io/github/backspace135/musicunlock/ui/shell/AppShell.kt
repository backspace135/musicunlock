package io.github.backspace135.musicunlock.ui.shell

import androidx.activity.compose.BackHandler
import androidx.compose.animation.AnimatedContent
import androidx.compose.animation.fadeIn
import androidx.compose.animation.fadeOut
import androidx.compose.animation.scaleIn
import androidx.compose.animation.togetherWith
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.runtime.Composable
import androidx.compose.runtime.CompositionLocalProvider
import androidx.compose.runtime.Stable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.saveable.Saver
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.saveable.rememberSaveableStateHolder
import androidx.compose.runtime.setValue
import androidx.compose.runtime.staticCompositionLocalOf
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.stringResource
import io.github.backspace135.musicunlock.R
import io.github.backspace135.musicunlock.ui.kit.BottomNavigation
import io.github.backspace135.musicunlock.ui.kit.MuIcons
import io.github.backspace135.musicunlock.ui.kit.NavItem
import io.github.backspace135.musicunlock.ui.lyrics.LyricsScreen
import io.github.backspace135.musicunlock.ui.nav.LocalPageActive
import io.github.backspace135.musicunlock.ui.theme.MuMotion
import io.github.backspace135.musicunlock.ui.unlock.UnlockScreen

enum class Workspace { Unlock, Lyrics }

/** Which workspace the shell shows. `LocalShell.current.open(Workspace.Lyrics)` switches from anywhere. */
@Stable
class ShellState(initial: Workspace) {
    var workspace by mutableStateOf(initial)
        private set

    fun open(workspace: Workspace) {
        this.workspace = workspace
    }

    internal companion object {
        val Saver = Saver<ShellState, String>(save = { it.workspace.name }, restore = { ShellState(Workspace.valueOf(it)) })
    }
}

val LocalShell = staticCompositionLocalOf<ShellState> { error("No ShellState outside AppShell") }

/**
 * Root page: the current workspace above the bottom navigation. Workspaces cross-fade while growing
 * from 98 %, like the Windows workspace switch; each keeps its scroll position and state. Back on the
 * 歌词 workspace returns to 解封装 first.
 */
@Composable
fun AppShell() {
    val shell = rememberSaveable(saver = ShellState.Saver) { ShellState(Workspace.Unlock) }
    val holder = rememberSaveableStateHolder()
    BackHandler(enabled = LocalPageActive.current && shell.workspace != Workspace.Unlock) { shell.open(Workspace.Unlock) }
    CompositionLocalProvider(LocalShell provides shell) {
        Column(Modifier.fillMaxSize()) {
            AnimatedContent(
                shell.workspace,
                Modifier.weight(1f),
                transitionSpec = {
                    (fadeIn(MuMotion.fadeIn()) + scaleIn(MuMotion.fadeIn(), initialScale = 0.98f)) togetherWith fadeOut(MuMotion.fadeOut())
                },
                label = "workspace",
            ) { workspace ->
                holder.SaveableStateProvider(workspace.name) {
                    when (workspace) {
                        Workspace.Unlock -> UnlockScreen()
                        Workspace.Lyrics -> LyricsScreen()
                    }
                }
            }
            BottomNavigation(
                items = listOf(
                    NavItem(stringResource(R.string.nav_unlock), MuIcons.MusicNote),
                    NavItem(stringResource(R.string.nav_lyrics), MuIcons.Lyrics),
                ),
                selectedIndex = shell.workspace.ordinal,
                onSelect = { shell.open(Workspace.entries[it]) },
            )
        }
    }
}
