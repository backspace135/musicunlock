package io.github.backspace135.musicunlock.ui.settings

import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.WindowInsets
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.navigationBars
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.windowInsetsBottomHeight
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Rect
import androidx.compose.ui.input.nestedscroll.nestedScroll
import androidx.compose.ui.layout.boundsInRoot
import androidx.compose.ui.layout.onGloballyPositioned
import androidx.compose.ui.res.stringResource
import io.github.backspace135.musicunlock.R
import io.github.backspace135.musicunlock.ui.accounts.AccountSettingsSection
import io.github.backspace135.musicunlock.ui.kit.ListGroup
import io.github.backspace135.musicunlock.ui.kit.MuIcons
import io.github.backspace135.musicunlock.ui.kit.MuTopBar
import io.github.backspace135.musicunlock.ui.kit.NavigationRow
import io.github.backspace135.musicunlock.ui.kit.SegmentedControl
import io.github.backspace135.musicunlock.ui.kit.rememberTopBarState
import io.github.backspace135.musicunlock.ui.lyrics.LyricsExportSettingsSection
import io.github.backspace135.musicunlock.ui.nav.LocalNavigator
import io.github.backspace135.musicunlock.ui.nav.Route
import io.github.backspace135.musicunlock.ui.playground.DebugTools
import io.github.backspace135.musicunlock.ui.theme.LocalThemeController
import io.github.backspace135.musicunlock.ui.theme.MuSpacing
import io.github.backspace135.musicunlock.ui.theme.ThemeMode
import io.github.backspace135.musicunlock.ui.unlock.OutputSettingsSection

/**
 * Settings page. Each section is its own composable in the package of the issue that owns it, so the
 * parallel sessions never edit the same file:
 *
 * | Section | File | Owner |
 * | --- | --- | --- |
 * | 输出（文件夹、命名） | ui/unlock/OutputSettingsSection.kt | #31 |
 * | 歌词导出 | ui/lyrics/LyricsExportSettingsSection.kt | #32 |
 * | 账号 | ui/accounts/AccountSettingsSection.kt | #33 |
 * | 外观、关于、开发 | this file | #30 |
 */
@Composable
fun SettingsScreen() {
    val navigator = LocalNavigator.current
    val topBar = rememberTopBarState()
    Column(Modifier.fillMaxSize()) {
        MuTopBar(stringResource(R.string.settings_title), state = topBar, onBack = navigator::pop)
        Column(Modifier.nestedScroll(topBar.nestedScrollConnection).verticalScroll(rememberScrollState())) {
            OutputSettingsSection()
            LyricsExportSettingsSection()
            AccountSettingsSection()
            AppearanceSection()
            ListGroup(header = stringResource(R.string.settings_about_header)) {
                NavigationRow(stringResource(R.string.settings_about_entry), onClick = { navigator.push(Route.About) }, icon = MuIcons.Info)
            }
            if (DebugTools.available) {
                ListGroup(header = stringResource(R.string.settings_debug_header)) {
                    NavigationRow(stringResource(R.string.settings_playground_entry), onClick = { navigator.push(Route.Playground) }, icon = MuIcons.Settings)
                }
            }
            Spacer(Modifier.height(MuSpacing.xxl))
            Spacer(Modifier.windowInsetsBottomHeight(WindowInsets.navigationBars))
        }
    }
}

/** 外观: theme mode. The reveal grows from the tapped segment. */
@Composable
private fun AppearanceSection() {
    val theme = LocalThemeController.current
    var bounds by remember { mutableStateOf(Rect.Zero) }
    val modes = ThemeMode.entries
    ListGroup(header = stringResource(R.string.settings_appearance_header), footer = stringResource(R.string.settings_theme_footer)) {
        Box(Modifier.padding(MuSpacing.m).onGloballyPositioned { bounds = it.boundsInRoot() }) {
            SegmentedControl(
                listOf(
                    stringResource(R.string.settings_theme_system),
                    stringResource(R.string.settings_theme_light),
                    stringResource(R.string.settings_theme_dark),
                ),
                theme.mode.ordinal,
                { index ->
                    val x = bounds.left + bounds.width * (index + 0.5f) / modes.size
                    theme.setMode(modes[index], Offset(x, bounds.center.y))
                },
            )
        }
    }
}
