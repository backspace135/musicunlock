package io.github.backspace135.musicunlock.ui.lyrics

import androidx.activity.compose.BackHandler
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.itemsIndexed
import androidx.compose.material3.Icon
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.input.nestedscroll.nestedScroll
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import androidx.lifecycle.viewmodel.compose.viewModel
import io.github.backspace135.musicunlock.R
import io.github.backspace135.musicunlock.core.AccountStore
import io.github.backspace135.musicunlock.core.LyricFormat
import io.github.backspace135.musicunlock.core.LyricSettings
import io.github.backspace135.musicunlock.core.LyricSong
import io.github.backspace135.musicunlock.core.LyricWorkspace
import io.github.backspace135.musicunlock.core.MusicCollection
import io.github.backspace135.musicunlock.core.MusicProvider
import io.github.backspace135.musicunlock.core.SearchKind
import io.github.backspace135.musicunlock.ui.kit.ActionFooter
import io.github.backspace135.musicunlock.ui.kit.EmptyState
import io.github.backspace135.musicunlock.ui.kit.IconAction
import io.github.backspace135.musicunlock.ui.kit.LazyGroupItem
import io.github.backspace135.musicunlock.ui.kit.ListRow
import io.github.backspace135.musicunlock.ui.kit.MenuItem
import io.github.backspace135.musicunlock.ui.kit.MuBottomSheet
import io.github.backspace135.musicunlock.ui.kit.MuCheckbox
import io.github.backspace135.musicunlock.ui.kit.MuIcons
import io.github.backspace135.musicunlock.ui.kit.MuSpinner
import io.github.backspace135.musicunlock.ui.kit.MuTopBar
import io.github.backspace135.musicunlock.ui.kit.NoticeBanner
import io.github.backspace135.musicunlock.ui.kit.OverflowMenu
import io.github.backspace135.musicunlock.ui.kit.PlainButton
import io.github.backspace135.musicunlock.ui.kit.PrimaryButton
import io.github.backspace135.musicunlock.ui.kit.RowIcon
import io.github.backspace135.musicunlock.ui.kit.SearchField
import io.github.backspace135.musicunlock.ui.kit.SecondaryButton
import io.github.backspace135.musicunlock.ui.kit.SegmentedControl
import io.github.backspace135.musicunlock.ui.kit.pressable
import io.github.backspace135.musicunlock.ui.kit.rememberTopBarState
import io.github.backspace135.musicunlock.ui.nav.LocalNavigator
import io.github.backspace135.musicunlock.ui.nav.LocalPageActive
import io.github.backspace135.musicunlock.ui.nav.Route
import io.github.backspace135.musicunlock.ui.theme.MuMotion
import io.github.backspace135.musicunlock.ui.theme.MuSpacing
import io.github.backspace135.musicunlock.ui.theme.MuTheme

@Composable
fun LyricsScreen(vm: LyricsViewModel = viewModel()) {
    val workspace = vm.workspace
    // 读一次版本号，工作区每次变化都重组。版本从 0 开始，不会小于 0。
    val revision = workspace.changes.collectAsStateWithLifecycle().value
    val navigator = LocalNavigator.current
    val topBar = rememberTopBarState()
    var showSettings by rememberSaveable { mutableStateOf(false) }
    var showFailures by rememberSaveable { mutableStateOf(false) }
    val folderFallback = stringResource(R.string.lyrics_folder_fallback)
    val unsupportedFile = stringResource(R.string.lyrics_unsupported_file)
    val pickLyrics = rememberLauncherForActivityResult(ActivityResultContracts.OpenDocument()) { uri ->
        if (uri != null) vm.importLocal(uri, unsupportedFile) { navigator.push(Route.LyricsEditor(it)) }
    }
    val exporting = workspace.exporting && revision >= 0L
    val hasResults = workspace.songs.isNotEmpty() || workspace.collections.isNotEmpty()

    BackHandler(enabled = LocalPageActive.current && vm.hasKeywordParent && !exporting) { vm.returnToKeyword() }

    Box(Modifier.fillMaxSize()) {
        Column(Modifier.fillMaxSize()) {
            MuTopBar(
                title = stringResource(R.string.lyrics_title),
                state = topBar,
                subtitle = { LoginStatus(workspace.provider) { navigator.push(Route.Accounts) } },
                actions = {
                    OverflowMenu(
                        listOf(
                            MenuItem(stringResource(R.string.lyrics_open_local_action), enabled = !exporting) {
                                pickLyrics.launch(arrayOf("text/*", "application/octet-stream"))
                            },
                            MenuItem(stringResource(R.string.lyrics_export_settings_action)) { showSettings = true },
                        ),
                    )
                    IconAction(MuIcons.Settings, stringResource(R.string.nav_settings_description), { navigator.push(Route.Settings) })
                },
            )
            SearchPanel(vm, workspace)
            NoticeBanner(workspace.notice, vm::dismissNotice)
            if (hasResults) ResultsHeader(vm, workspace)
            when {
                workspace.searching && !hasResults -> LoadingBody(workspace.loadingText, Modifier.weight(1f))
                !hasResults -> EmptyBody(Modifier.weight(1f), enabled = !exporting) {
                    pickLyrics.launch(arrayOf("text/*", "application/octet-stream"))
                }
                else -> ResultsList(vm, workspace, Modifier.weight(1f).nestedScroll(topBar.nestedScrollConnection)) { songId ->
                    navigator.push(Route.LyricsEditor(songId))
                }
            }
        }
        Column(Modifier.align(Alignment.BottomCenter)) {
            if (workspace.failures.isNotEmpty()) {
                FailuresEntry(workspace.failuresTitle) { showFailures = true }
            }
            ExportFooter(vm, workspace, folderFallback)
        }
    }

    MuBottomSheet(showSettings, { showSettings = false }, title = stringResource(R.string.lyrics_export_settings_title)) {
        LyricsExportSettingsContent()
    }
    MuBottomSheet(showFailures, { showFailures = false }, title = workspace.failuresTitle.ifEmpty { null }) {
        FailuresList(workspace.failures)
    }
    LaunchedEffect(Unit) { vm.notifyFolderFallback(folderFallback) }
}

@Composable
private fun LoginStatus(provider: MusicProvider, onClick: () -> Unit) {
    val signedIn by AccountStore.signedInState.collectAsStateWithLifecycle()
    val saved = signedIn[provider] == true
    Row(
        Modifier.pressable(onClick, pressedScale = 0.97f).padding(vertical = MuSpacing.xxs),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Icon(if (saved) MuIcons.Shield else MuIcons.Globe, null, Modifier.size(14.dp), tint = MuTheme.colors.secondary)
        Text(
            stringResource(if (saved) R.string.lyrics_login_saved else R.string.lyrics_login_public),
            style = MuTheme.type.caption,
            color = MuTheme.colors.secondary,
            modifier = Modifier.padding(start = MuSpacing.xs),
        )
        Icon(MuIcons.Chevron, null, Modifier.size(14.dp), tint = MuTheme.colors.secondary)
    }
}

@Composable
private fun SearchPanel(vm: LyricsViewModel, workspace: LyricWorkspace) {
    val exporting = workspace.exporting
    val canSearch = !exporting && workspace.query.trim().isNotEmpty()
    Column(Modifier.padding(horizontal = MuSpacing.page, vertical = MuSpacing.s), verticalArrangement = Arrangement.spacedBy(MuSpacing.s)) {
        Row(horizontalArrangement = Arrangement.spacedBy(MuSpacing.s)) {
            SegmentedControl(
                MusicProvider.entries.map { it.title },
                workspace.provider.ordinal,
                { vm.setProvider(MusicProvider.entries[it]) },
                Modifier.weight(1.15f),
            )
            SegmentedControl(
                SearchKind.entries.map { it.label },
                workspace.kind.ordinal,
                { vm.setKind(SearchKind.entries[it]) },
                Modifier.weight(1f),
            )
        }
        Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(MuSpacing.s)) {
            SearchField(
                query = workspace.query,
                onQueryChange = vm::setQuery,
                placeholder = stringResource(R.string.lyrics_search_placeholder),
                onSearch = { if (canSearch) vm.search() },
                enabled = !exporting,
                modifier = Modifier.weight(1f),
            )
            PrimaryButton(
                text = stringResource(if (workspace.searching) R.string.common_cancel else R.string.common_search),
                onClick = { if (workspace.searching) vm.cancelSearch() else vm.search() },
                enabled = workspace.searching || canSearch,
                small = true,
            )
        }
    }
}

@Composable
private fun ResultsHeader(vm: LyricsViewModel, workspace: LyricWorkspace) {
    Row(
        Modifier.padding(start = MuSpacing.page, end = MuSpacing.s, top = MuSpacing.s, bottom = MuSpacing.s),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Column(Modifier.weight(1f)) {
            Text(workspace.resultsTitle, style = MuTheme.type.headline)
            Text(workspace.resultsSubtitle, style = MuTheme.type.caption, color = MuTheme.colors.secondary)
        }
        if (workspace.songs.isNotEmpty()) {
            PlainButton(
                stringResource(if (workspace.allChecked) R.string.lyrics_deselect_all_action else R.string.lyrics_select_all_action),
                vm::selectAll,
                enabled = !workspace.exporting,
            )
        }
    }
}

@Composable
private fun LoadingBody(text: String, modifier: Modifier = Modifier) {
    Column(modifier.fillMaxWidth(), horizontalAlignment = Alignment.CenterHorizontally, verticalArrangement = Arrangement.Center) {
        MuSpinner()
        Text(text, style = MuTheme.type.body, color = MuTheme.colors.secondary, modifier = Modifier.padding(top = MuSpacing.m))
    }
}

@Composable
private fun EmptyBody(modifier: Modifier = Modifier, enabled: Boolean, onOpenLocal: () -> Unit) {
    EmptyState(
        title = stringResource(R.string.lyrics_empty_title),
        body = stringResource(R.string.lyrics_empty_body),
        icon = MuIcons.Search,
        modifier = modifier.padding(top = MuSpacing.xl),
    ) {
        PlainButton(stringResource(R.string.lyrics_open_local_action), onOpenLocal, enabled = enabled)
    }
}

@Composable
private fun ResultsList(vm: LyricsViewModel, workspace: LyricWorkspace, modifier: Modifier = Modifier, onOpenSong: (String) -> Unit) {
    val collections = workspace.collections
    val songs = workspace.songs
    val exporting = workspace.exporting
    LazyColumn(modifier.fillMaxSize(), contentPadding = PaddingValues(bottom = 160.dp)) {
        itemsIndexed(collections, key = { _, item -> "collection:${item.id}" }) { index, collection ->
            LazyGroupItem(index, collections.size, Modifier.animateItem(fadeInSpec = MuMotion.fadeIn(), placementSpec = MuMotion.list(), fadeOutSpec = MuMotion.fadeOut())) {
                CollectionRow(collection, enabled = !exporting) { vm.open(collection) }
            }
        }
        if (collections.isNotEmpty() && songs.isNotEmpty()) {
            item(key = "gap") { Spacer(Modifier.height(MuSpacing.l)) }
        }
        itemsIndexed(songs, key = { _, song -> "song:${song.id}" }) { index, song ->
            LazyGroupItem(index, songs.size, Modifier.animateItem(fadeInSpec = MuMotion.fadeIn(), placementSpec = MuMotion.list(), fadeOutSpec = MuMotion.fadeOut()), dividerInset = 64.dp) {
                SongRow(song, workspace, enabled = !exporting, onToggle = { vm.toggle(song.id) }) {
                    vm.select(song.id)
                    onOpenSong(song.id)
                }
            }
        }
        if (workspace.hasMore) {
            item(key = "more") {
                Box(Modifier.fillMaxWidth().padding(MuSpacing.l), contentAlignment = Alignment.Center) {
                    if (workspace.searching) {
                        MuSpinner()
                    } else {
                        SecondaryButton(
                            stringResource(R.string.lyrics_load_more_action),
                            vm::loadMore,
                            enabled = !exporting,
                        )
                    }
                }
            }
        }
    }
}

@Composable
private fun CollectionRow(collection: MusicCollection, enabled: Boolean, onClick: () -> Unit) {
    val icon = if (collection.kind == SearchKind.ALBUM) MuIcons.Album else MuIcons.Playlist
    ListRow(
        title = collection.title,
        subtitle = collection.subtitle.ifEmpty { null },
        titleMaxLines = 2,
        leading = { RowIcon(icon) },
        trailing = { Icon(MuIcons.Chevron, null, Modifier.size(20.dp), tint = MuTheme.colors.secondary) },
        onClick = onClick,
        enabled = enabled,
    )
}

@Composable
private fun SongRow(song: LyricSong, workspace: LyricWorkspace, enabled: Boolean, onToggle: () -> Unit, onOpen: () -> Unit) {
    val selectLabel = stringResource(R.string.lyrics_select_description, song.title)
    ListRow(
        title = song.title,
        subtitle = LyricWorkspace.songArtist(song),
        titleMaxLines = 2,
        current = workspace.isSelected(song.id),
        enabled = enabled,
        onClick = onOpen,
        leading = {
            MuCheckbox(
                checked = workspace.isChecked(song.id),
                onCheckedChange = { onToggle() },
                enabled = enabled,
                modifier = Modifier.semantics { contentDescription = selectLabel },
            )
        },
        extra = {
            Row(verticalAlignment = Alignment.CenterVertically) {
                Text(
                    song.album,
                    style = MuTheme.type.caption,
                    color = MuTheme.colors.secondary,
                    maxLines = 1,
                    overflow = TextOverflow.Ellipsis,
                    modifier = Modifier.weight(1f),
                )
                Text(song.duration, style = MuTheme.type.caption, color = MuTheme.colors.secondary, modifier = Modifier.padding(start = MuSpacing.s))
            }
        },
    )
}

@Composable
private fun FailuresEntry(title: String, onClick: () -> Unit) {
    Row(
        Modifier
            .fillMaxWidth()
            .padding(horizontal = MuSpacing.page)
            .pressable(onClick, pressedScale = 0.98f)
            .padding(vertical = MuSpacing.s),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Icon(MuIcons.Warning, null, Modifier.size(16.dp), tint = MuTheme.colors.secondary)
        Text(title, style = MuTheme.type.caption, modifier = Modifier.padding(start = MuSpacing.s).weight(1f))
        Icon(MuIcons.Chevron, null, Modifier.size(16.dp), tint = MuTheme.colors.secondary)
    }
}

@Composable
private fun FailuresList(failures: List<String>) {
    val context = LocalContext.current
    Column(Modifier.padding(bottom = MuSpacing.l)) {
        failures.forEach { line ->
            Text(
                line,
                style = MuTheme.type.body,
                modifier = Modifier
                    .fillMaxWidth()
                    .pressable(onClick = {}, onLongClick = { copyText(context, line) })
                    .padding(horizontal = MuSpacing.page, vertical = MuSpacing.s),
            )
        }
    }
}

@Composable
private fun ExportFooter(vm: LyricsViewModel, workspace: LyricWorkspace, folderFallback: String) {
    val context = LocalContext.current
    val settings by LyricSettings.values.collectAsStateWithLifecycle()
    val exporting = workspace.exporting
    ActionFooter(
        title = workspace.progressText,
        progress = if (exporting && workspace.exportTotal > 0) workspace.progress.toFloat() / workspace.exportTotal else if (exporting) 0f else null,
        actionLabel = when {
            workspace.stopRequested -> stringResource(R.string.lyrics_stopping_action)
            exporting -> stringResource(R.string.lyrics_stop_export_action)
            else -> workspace.exportCheckedLabel
        },
        onAction = { if (exporting) vm.stopExport() else vm.exportChecked(context, folderFallback) },
        actionEnabled = !workspace.stopRequested,
        visible = workspace.checkedCount > 0 || exporting,
        leading = if (exporting) {
            null
        } else {
            {
                SegmentedControl(
                    LyricFormat.entries.map { it.label },
                    settings.options.format.ordinal,
                    { LyricsSession.setOptions(settings.options.copy(format = LyricFormat.entries[it])) },
                )
            }
        },
    )
}
