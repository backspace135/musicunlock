package io.github.backspace135.musicunlock

import android.content.Intent
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
import io.github.backspace135.musicunlock.ui.unlock.UnlockSession

class MainActivity : ComponentActivity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        enableEdgeToEdge()
        super.onCreate(savedInstanceState)
        val theme = ThemeController(getSharedPreferences("settings", MODE_PRIVATE))
        // A recreated activity must not import the same share again.
        if (savedInstanceState == null) importShared(intent)
        setContent { MusicUnlockApp(theme) }
    }

    override fun onNewIntent(intent: Intent) {
        super.onNewIntent(intent)
        setIntent(intent)
        importShared(intent)
    }

    private fun importShared(intent: Intent?) {
        if (intent == null) return
        UnlockSession.importIntent(intent)
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
