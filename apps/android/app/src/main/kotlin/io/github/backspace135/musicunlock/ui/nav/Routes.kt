package io.github.backspace135.musicunlock.ui.nav

import androidx.compose.runtime.Composable
import io.github.backspace135.musicunlock.core.MusicProvider
import io.github.backspace135.musicunlock.ui.accounts.AccountsScreen
import io.github.backspace135.musicunlock.ui.accounts.WebLoginScreen
import io.github.backspace135.musicunlock.ui.lyrics.LyricsEditorScreen
import io.github.backspace135.musicunlock.ui.playground.DebugTools
import io.github.backspace135.musicunlock.ui.settings.AboutScreen
import io.github.backspace135.musicunlock.ui.settings.LicenseTextScreen
import io.github.backspace135.musicunlock.ui.settings.LicensesScreen
import io.github.backspace135.musicunlock.ui.settings.SettingsScreen
import io.github.backspace135.musicunlock.ui.shell.AppShell
import java.io.Serializable

/**
 * Every page of the app, registered in one place. To add a page: add a route here, add its line to
 * [RouteContent], then open it with `LocalNavigator.current.push(Route.X)`. Routes are saved when the
 * process is killed, so their fields must be Serializable (String, Int, Long, enums…).
 * Keep each issue's routes in its own block and only append.
 */
sealed interface Route : Serializable {
    // #30: shell, settings, about.
    /** Root page: bottom navigation between the 解封装 and 歌词 workspaces. */
    data object Shell : Route
    data object Settings : Route
    data object About : Route
    data object Licenses : Route
    /** Full text of one license; [asset] is a path under assets/. */
    data class LicenseText(val asset: String, val title: String) : Route
    /** Component playground, only in debug builds (see DebugTools). */
    data object Playground : Route

    // #31 解封装: the workspace itself lives in the shell; add pushed pages here.

    // #32 歌词.
    /** Lyrics editor for one song, pushed from the results list. */
    data class LyricsEditor(val songId: String) : Route

    // #33 账号.
    data object Accounts : Route

    /** Official login page. [relogin] drops the platform's music-key cookies before the page loads. */
    data class WebLogin(val provider: MusicProvider, val relogin: Boolean) : Route
}

@Composable
fun RouteContent(route: Route) {
    when (route) {
        Route.Shell -> AppShell()
        Route.Settings -> SettingsScreen()
        Route.About -> AboutScreen()
        Route.Licenses -> LicensesScreen()
        is Route.LicenseText -> LicenseTextScreen(route.asset, route.title)
        Route.Playground -> DebugTools.Playground()
        is Route.LyricsEditor -> LyricsEditorScreen(route.songId)
        Route.Accounts -> AccountsScreen()
        is Route.WebLogin -> WebLoginScreen(route.provider, route.relogin)
    }
}
