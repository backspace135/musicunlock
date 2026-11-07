package io.github.backspace135.musicunlock.ui.lyrics

import androidx.activity.compose.BackHandler
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.itemsIndexed
import androidx.compose.material3.Icon
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.input.nestedscroll.nestedScroll
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.unit.dp
import androidx.lifecycle.viewmodel.compose.viewModel
import io.github.backspace135.musicunlock.R
import io.github.backspace135.musicunlock.ui.kit.ActionFooter
import io.github.backspace135.musicunlock.ui.kit.EmptyState
import io.github.backspace135.musicunlock.ui.kit.IconAction
import io.github.backspace135.musicunlock.ui.kit.LazyGroupItem
import io.github.backspace135.musicunlock.ui.kit.ListRow
import io.github.backspace135.musicunlock.ui.kit.MenuItem
import io.github.backspace135.musicunlock.ui.kit.MuBottomSheet
import io.github.backspace135.musicunlock.ui.kit.MuIcons
import io.github.backspace135.musicunlock.ui.kit.MuTopBar
import io.github.backspace135.musicunlock.ui.kit.OverflowMenu
import io.github.backspace135.musicunlock.ui.kit.PlainButton
import io.github.backspace135.musicunlock.ui.kit.PrimaryButton
import io.github.backspace135.musicunlock.ui.kit.SegmentedControl
import io.github.backspace135.musicunlock.ui.kit.TopBarSearch
import io.github.backspace135.musicunlock.ui.kit.pressable
import io.github.backspace135.musicunlock.ui.kit.rememberTopBarState
import io.github.backspace135.musicunlock.ui.nav.LocalNavigator
import io.github.backspace135.musicunlock.ui.nav.LocalPageActive
import io.github.backspace135.musicunlock.ui.nav.Route
import io.github.backspace135.musicunlock.ui.theme.MuMotion
import io.github.backspace135.musicunlock.ui.theme.MuSpacing
import io.github.backspace135.musicunlock.ui.theme.MuTheme

// PLACEHOLDER (#30) built from the kit with fake data. #32 replaces this file (page one in #32).

internal val ExportFormats = listOf("LRC", "SRT", "TXT", "VTT")

@Composable
fun LyricsScreen(vm: LyricsViewModel = viewModel()) {
    val navigator = LocalNavigator.current
    val topBar = rememberTopBarState()
    var searching by rememberSaveable { mutableStateOf(false) }
    var showExportSettings by rememberSaveable { mutableStateOf(false) }
    BackHandler(enabled = LocalPageActive.current && vm.selectionMode) { vm.endSelection() }

    Box(Modifier.fillMaxSize()) {
        Column(Modifier.fillMaxSize()) {
            MuTopBar(
                title = stringResource(R.string.lyrics_title),
                state = topBar,
                subtitle = { LoginStatus { navigator.push(Route.Accounts) } },
                search = TopBarSearch(
                    query = vm.query,
                    onQueryChange = { vm.query = it },
                    placeholder = stringResource(R.string.lyrics_search_placeholder),
                    expanded = searching,
                    onExpandedChange = { searching = it },
                    onSearch = {
                        vm.search()
                        searching = false
                    },
                ),
                actions = {
                    OverflowMenu(
                        listOf(
                            MenuItem(stringResource(R.string.lyrics_open_local_action)) { navigator.push(Route.LyricsEditor(SampleSongs.first().id)) },
                            MenuItem(stringResource(R.string.lyrics_export_settings_action)) { showExportSettings = true },
                        ),
                    )
                    IconAction(MuIcons.Settings, stringResource(R.string.nav_settings_description), { navigator.push(Route.Settings) })
                },
            )
            LazyColumn(
                Modifier.fillMaxSize().nestedScroll(topBar.nestedScrollConnection),
                contentPadding = PaddingValues(bottom = 112.dp),
            ) {
                if (vm.results.isEmpty()) {
                    item(key = "empty") {
                        EmptyState(
                            title = stringResource(R.string.lyrics_empty_title),
                            body = stringResource(R.string.lyrics_empty_body),
                            icon = MuIcons.Search,
                            modifier = Modifier.padding(top = MuSpacing.xl),
                        ) {
                            PrimaryButton(stringResource(R.string.common_search), { searching = true }, icon = MuIcons.Search)
                            Spacer(Modifier.height(MuSpacing.s))
                            PlainButton(stringResource(R.string.lyrics_open_local_action), { navigator.push(Route.LyricsEditor(SampleSongs.first().id)) })
                        }
                    }
                } else {
                    item(key = "header") {
                        Row(Modifier.padding(start = MuSpacing.page, end = MuSpacing.s, top = MuSpacing.s, bottom = MuSpacing.s), verticalAlignment = Alignment.CenterVertically) {
                            Column(Modifier.weight(1f)) {
                                Text(stringResource(R.string.lyrics_results_title), style = MuTheme.type.headline)
                                Text(
                                    stringResource(R.string.lyrics_results_subtitle_format, vm.results.size, vm.checked.size),
                                    style = MuTheme.type.caption,
                                    color = MuTheme.colors.secondary,
                                )
                            }
                            val all = vm.checked.size == vm.results.size
                            PlainButton(stringResource(if (all) R.string.lyrics_deselect_all_action else R.string.lyrics_select_all_action), vm::toggleAll, enabled = !vm.exporting)
                        }
                    }
                    itemsIndexed(vm.results, key = { _, song -> song.id }) { index, song ->
                        LazyGroupItem(
                            index,
                            vm.results.size,
                            Modifier.animateItem(fadeInSpec = MuMotion.fadeIn(), placementSpec = MuMotion.list(), fadeOutSpec = MuMotion.fadeOut()),
                        ) {
                            ListRow(
                                title = song.title,
                                subtitle = "${song.artist} · ${song.album} · ${song.duration}",
                                selectionMode = vm.selectionMode,
                                selected = song.id in vm.checked,
                                current = song.id == vm.currentId,
                                enabled = !vm.exporting,
                                onClick = {
                                    if (vm.selectionMode) {
                                        vm.toggle(song.id)
                                    } else {
                                        vm.currentId = song.id
                                        navigator.push(Route.LyricsEditor(song.id))
                                    }
                                },
                                onLongClick = { vm.beginSelection(song.id) },
                            )
                        }
                    }
                }
            }
        }

        ActionFooter(
            title = stringResource(R.string.lyrics_exporting_format, vm.exported, vm.checked.size),
            progress = if (vm.exporting) vm.exported / vm.checked.size.coerceAtLeast(1).toFloat() else null,
            actionLabel = if (vm.exporting) stringResource(R.string.lyrics_stop_export_action) else stringResource(R.string.lyrics_export_selected_format, vm.checked.size),
            onAction = vm::exportSelected,
            visible = vm.checked.isNotEmpty() || vm.exporting,
            leading = if (vm.exporting) null else ({ SegmentedControl(ExportFormats, vm.format, { vm.format = it }) }),
            modifier = Modifier.align(Alignment.BottomCenter),
        )
    }

    MuBottomSheet(showExportSettings, { showExportSettings = false }, title = stringResource(R.string.lyrics_export_settings_title)) {
        LyricsExportSettingsContent()
    }
}

@Composable
private fun LoginStatus(onClick: () -> Unit) {
    Row(
        Modifier.pressable(onClick, pressedScale = 0.97f).padding(vertical = MuSpacing.xxs),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Icon(MuIcons.Globe, null, Modifier.size(14.dp), tint = MuTheme.colors.secondary)
        Text(
            stringResource(R.string.lyrics_login_public),
            style = MuTheme.type.caption,
            color = MuTheme.colors.secondary,
            modifier = Modifier.padding(start = MuSpacing.xs),
        )
        Icon(MuIcons.Chevron, null, Modifier.size(14.dp), tint = MuTheme.colors.secondary)
    }
}
