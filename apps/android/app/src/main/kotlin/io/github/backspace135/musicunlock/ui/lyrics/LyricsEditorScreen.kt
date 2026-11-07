package io.github.backspace135.musicunlock.ui.lyrics

import android.content.ActivityNotFoundException
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.gestures.detectTapGestures
import androidx.compose.foundation.horizontalScroll
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.navigationBarsPadding
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.verticalScroll
import androidx.compose.foundation.rememberScrollState
import androidx.compose.material3.Icon
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.ui.unit.dp
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import androidx.lifecycle.viewmodel.compose.viewModel
import io.github.backspace135.musicunlock.R
import io.github.backspace135.musicunlock.core.LyricSettings
import io.github.backspace135.musicunlock.core.LyricTab
import io.github.backspace135.musicunlock.core.LyricWorkspace
import io.github.backspace135.musicunlock.core.OutputFolders
import io.github.backspace135.musicunlock.ui.kit.ActionFooter
import io.github.backspace135.musicunlock.ui.kit.ConfirmDialog
import io.github.backspace135.musicunlock.ui.kit.IconAction
import io.github.backspace135.musicunlock.ui.kit.MenuItem
import io.github.backspace135.musicunlock.ui.kit.MuBottomSheet
import io.github.backspace135.musicunlock.ui.kit.MuIcons
import io.github.backspace135.musicunlock.ui.kit.MuSpinner
import io.github.backspace135.musicunlock.ui.kit.MuTextField
import io.github.backspace135.musicunlock.ui.kit.MuTopBar
import io.github.backspace135.musicunlock.ui.kit.NoticeBanner
import io.github.backspace135.musicunlock.ui.kit.OverflowMenu
import io.github.backspace135.musicunlock.ui.kit.PrimaryButton
import io.github.backspace135.musicunlock.ui.kit.SegmentedControl
import io.github.backspace135.musicunlock.ui.kit.pressable
import io.github.backspace135.musicunlock.ui.kit.rememberTopBarState
import io.github.backspace135.musicunlock.ui.nav.LocalNavigator
import io.github.backspace135.musicunlock.ui.theme.MuSpacing
import io.github.backspace135.musicunlock.ui.theme.MuTheme

@Composable
fun LyricsEditorScreen(songId: String, vm: LyricsViewModel = viewModel()) {
    val workspace = vm.workspace
    val revision = workspace.changes.collectAsStateWithLifecycle().value
    val navigator = LocalNavigator.current
    val context = LocalContext.current
    val song = workspace.songs.firstOrNull { it.id == songId } ?: workspace.selected?.takeIf { it.id == songId }
    val title = song?.title.orEmpty()
    var tabIndex by rememberSaveable(songId) { mutableIntStateOf(0) }
    val tab = LyricTab.entries[tabIndex.coerceIn(0, LyricTab.entries.lastIndex)]
    var confirmReload by rememberSaveable { mutableStateOf(false) }
    var showSettings by rememberSaveable { mutableStateOf(false) }
    var showFailures by rememberSaveable { mutableStateOf(false) }
    val folderFallback = stringResource(R.string.lyrics_folder_fallback)
    val exporting = workspace.exporting && revision >= 0L
    val settings by LyricSettings.values.collectAsStateWithLifecycle()
    val pickFolder = rememberLauncherForActivityResult(ActivityResultContracts.OpenDocumentTree()) { uri ->
        if (uri != null) LyricsSession.setOutputFolder(uri)
    }

    LaunchedEffect(songId) {
        if (workspace.selected?.id != songId) vm.select(songId)
    }

    Box(Modifier.fillMaxSize()) {
        Column(Modifier.fillMaxSize()) {
            MuTopBar(
                title = title,
                state = rememberTopBarState(),
                onBack = navigator::pop,
                subtitle = {
                    val detail = song?.let(LyricWorkspace::songDetail).orEmpty()
                    if (detail.isNotEmpty()) {
                        Text(detail, style = MuTheme.type.caption, color = MuTheme.colors.secondary)
                    }
                },
                actions = {
                    IconAction(
                        MuIcons.Refresh,
                        stringResource(R.string.lyrics_reload_description),
                        onClick = { confirmReload = true },
                        enabled = workspace.canRetry && !exporting,
                    )
                    OverflowMenu(
                        listOf(
                            MenuItem(stringResource(R.string.lyrics_copy_action), enabled = workspace.preview.isNotEmpty(), icon = MuIcons.Copy) {
                                vm.copyLyrics(context)
                            },
                            MenuItem(stringResource(R.string.lyrics_view_folder_action), icon = MuIcons.Folder) {
                                val folder = settings.outputFolder
                                try {
                                    context.startActivity(OutputFolders.viewIntent(folder))
                                } catch (_: ActivityNotFoundException) {
                                    workspace.showNotice(context.getString(R.string.lyrics_open_folder_format, OutputFolders.path(folder)))
                                }
                            },
                            MenuItem(stringResource(R.string.lyrics_change_folder_action), enabled = !exporting, icon = MuIcons.AddFolder) {
                                pickFolder.launch(null)
                            },
                        ),
                    )
                },
                modifier = Modifier.pointerInput(title) {
                    detectTapGestures(onLongPress = { if (title.isNotEmpty()) copyText(context, title) })
                },
            )
            NoticeBanner(workspace.notice, vm::dismissNotice)
            SegmentedControl(
                LyricTab.entries.map { it.label },
                tab.ordinal,
                { tabIndex = it },
                Modifier.padding(horizontal = MuSpacing.page, vertical = MuSpacing.s),
            )
            EditorBody(
                workspace = workspace,
                tab = tab,
                exporting = exporting,
                onEdit = { text ->
                    val normalized = lyricNewlines(text)
                    if (normalized != workspace.text(tab)) workspace.edit(tab, normalized)
                },
                onAdjustSettings = { showSettings = true },
                modifier = Modifier.weight(1f).padding(bottom = 120.dp),
            )
        }
        Column(Modifier.align(Alignment.BottomCenter).navigationBarsPadding()) {
            if (workspace.failures.isNotEmpty()) {
                EditorFailures(workspace.failuresTitle) { showFailures = true }
            }
            EditorFooter(vm, workspace, tab, exporting, folderFallback)
        }
    }

    ConfirmDialog(
        visible = confirmReload,
        title = stringResource(R.string.lyrics_reload_title),
        confirmLabel = stringResource(R.string.lyrics_reload_action),
        onConfirm = {
            confirmReload = false
            vm.select(songId, reload = true)
        },
        onDismissRequest = { confirmReload = false },
        icon = MuIcons.Warning,
    )
    MuBottomSheet(showSettings, { showSettings = false }, title = stringResource(R.string.lyrics_export_settings_title)) {
        LyricsExportSettingsContent()
    }
    MuBottomSheet(showFailures, { showFailures = false }, title = workspace.failuresTitle.ifEmpty { null }) {
        Column(Modifier.padding(bottom = MuSpacing.l)) {
            workspace.failures.forEach { line ->
                Text(
                    line,
                    style = MuTheme.type.body,
                    modifier = Modifier
                        .fillMaxWidth()
                        .padding(horizontal = MuSpacing.page, vertical = MuSpacing.s)
                        .pointerInput(line) { detectTapGestures(onLongPress = { copyText(context, line) }) },
                )
            }
        }
    }
}

@Composable
private fun EditorBody(
    workspace: LyricWorkspace,
    tab: LyricTab,
    exporting: Boolean,
    onEdit: (String) -> Unit,
    onAdjustSettings: () -> Unit,
    modifier: Modifier = Modifier,
) {
    val error = workspace.previewError
    when {
        workspace.loadingLyrics -> Column(
            modifier.fillMaxWidth(),
            horizontalAlignment = Alignment.CenterHorizontally,
            verticalArrangement = Arrangement.Center,
        ) {
            MuSpinner()
            Text(
                stringResource(R.string.lyrics_loading_lyrics),
                style = MuTheme.type.body,
                color = MuTheme.colors.secondary,
                modifier = Modifier.padding(top = MuSpacing.m),
            )
        }
        tab == LyricTab.PREVIEW && !error.isNullOrEmpty() -> Column(
            modifier.fillMaxWidth().padding(horizontal = MuSpacing.page),
            horizontalAlignment = Alignment.CenterHorizontally,
            verticalArrangement = Arrangement.Center,
        ) {
            Icon(MuIcons.Settings, null, Modifier.size(32.dp), tint = MuTheme.colors.secondary)
            Text(
                stringResource(R.string.lyrics_preview_error_title),
                style = MuTheme.type.headline,
                modifier = Modifier.padding(top = MuSpacing.m),
            )
            Text(error, style = MuTheme.type.body, color = MuTheme.colors.secondary, modifier = Modifier.padding(top = MuSpacing.s))
            PrimaryButton(
                stringResource(R.string.lyrics_adjust_settings_action),
                onAdjustSettings,
                Modifier.padding(top = MuSpacing.l),
            )
        }
        else -> {
            val vertical = rememberScrollState()
            val horizontal = rememberScrollState()
            Box(
                modifier
                    .fillMaxWidth()
                    .verticalScroll(vertical)
                    .horizontalScroll(horizontal)
                    .padding(horizontal = MuSpacing.page),
            ) {
                MuTextField(
                    value = workspace.text(tab),
                    onValueChange = { if (tab != LyricTab.PREVIEW && !workspace.loadingLyrics && !exporting) onEdit(it) },
                    singleLine = false,
                    monospace = true,
                    readOnly = tab == LyricTab.PREVIEW || exporting,
                    // 不用 Ascii 键盘，否则中文歌词无法输入；关掉自动更正即可去掉拼写检查和联想。
                    keyboardOptions = KeyboardOptions(autoCorrectEnabled = false, keyboardType = KeyboardType.Text),
                    modifier = Modifier.width(4000.dp),
                )
            }
        }
    }
}

@Composable
private fun EditorFailures(title: String, onClick: () -> Unit) {
    Text(
        title,
        style = MuTheme.type.caption,
        color = MuTheme.colors.secondary,
        modifier = Modifier.fillMaxWidth().pressable(onClick).padding(horizontal = MuSpacing.page, vertical = MuSpacing.s),
    )
}

@Composable
private fun EditorFooter(
    vm: LyricsViewModel,
    workspace: io.github.backspace135.musicunlock.core.LyricWorkspace,
    tab: LyricTab,
    exporting: Boolean,
    folderFallback: String,
) {
    val context = LocalContext.current
    val hint = stringResource(if (tab == LyricTab.PREVIEW) R.string.lyrics_preview_match else R.string.lyrics_editable_hint)
    val lines = stringResource(R.string.lyrics_line_count_format, workspace.lineCount)
    ActionFooter(
        title = workspace.progressText,
        progress = if (exporting && workspace.exportTotal > 0) workspace.progress.toFloat() / workspace.exportTotal else if (exporting) 0f else null,
        actionLabel = when {
            workspace.stopRequested -> stringResource(R.string.lyrics_stopping_action)
            exporting -> stringResource(R.string.lyrics_stop_export_action)
            else -> stringResource(R.string.lyrics_export_current_action)
        },
        onAction = { if (exporting) vm.stopExport() else vm.exportCurrent(context, folderFallback) },
        actionEnabled = if (exporting) !workspace.stopRequested else workspace.canExportCurrent,
        leading = if (exporting) {
            null
        } else {
            {
                Row(verticalAlignment = Alignment.CenterVertically) {
                    Text(hint, style = MuTheme.type.caption, color = MuTheme.colors.secondary, modifier = Modifier.weight(1f))
                    Text(lines, style = MuTheme.type.caption, color = MuTheme.colors.secondary, modifier = Modifier.padding(start = MuSpacing.s))
                }
            }
        },
    )
}
