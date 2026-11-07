package io.github.backspace135.musicunlock

import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.enableEdgeToEdge
import androidx.compose.runtime.Composable
import io.github.backspace135.musicunlock.ui.kit.OverlayHostBox
import io.github.backspace135.musicunlock.ui.nav.PageStackHost
import io.github.backspace135.musicunlock.ui.nav.Route
import io.github.backspace135.musicunlock.ui.nav.RouteContent
import io.github.backspace135.musicunlock.ui.nav.rememberNavigator
import io.github.backspace135.musicunlock.ui.theme.MuThemeHost
import io.github.backspace135.musicunlock.ui.theme.ThemeController

class MainActivity : ComponentActivity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        enableEdgeToEdge()
        super.onCreate(savedInstanceState)
        val theme = ThemeController(getSharedPreferences("settings", MODE_PRIVATE))
        setContent { MusicUnlockApp(theme) }
    }
}

/** Theme (with the reveal) → overlay layer for sheets and dialogs → page stack starting at the shell. */
@Composable
fun MusicUnlockApp(theme: ThemeController) {
    MuThemeHost(theme) {
        OverlayHostBox {
            PageStackHost(rememberNavigator(Route.Shell)) { route -> RouteContent(route) }
        }
    }
}
