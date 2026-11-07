package io.github.backspace135.musicunlock.ui.unlock

import android.content.ActivityNotFoundException
import android.content.ClipData
import android.content.ClipboardManager
import android.content.Context
import android.content.Intent
import android.content.pm.PackageManager
import android.graphics.BitmapFactory
import android.net.Uri
import android.provider.Settings
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.Image
import androidx.compose.foundation.background
import androidx.compose.foundation.combinedClickable
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
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Icon
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.asImageBitmap
import androidx.compose.ui.input.nestedscroll.nestedScroll
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.unit.dp
import androidx.core.content.ContextCompat
import androidx.lifecycle.viewmodel.compose.viewModel
import io.github.backspace135.musicunlock.R
import io.github.backspace135.musicunlock.core.AudioJob
import io.github.backspace135.musicunlock.core.JobState
import io.github.backspace135.musicunlock.core.OutputFolders
import io.github.backspace135.musicunlock.core.ProcessingService
import io.github.backspace135.musicunlock.core.QueueFilter
import io.github.backspace135.musicunlock.core.UnlockSettings
import io.github.backspace135.musicunlock.ui.kit.ActionFooter
import io.github.backspace135.musicunlock.ui.kit.ConfirmDialog
import io.github.backspace135.musicunlock.ui.kit.EmptyState
import io.github.backspace135.musicunlock.ui.kit.IconAction
import io.github.backspace135.musicunlock.ui.kit.JobGlyph
import io.github.backspace135.musicunlock.ui.kit.LazyGroupItem
import io.github.backspace135.musicunlock.ui.kit.ListRow
import io.github.backspace135.musicunlock.ui.kit.MenuItem
import io.github.backspace135.musicunlock.ui.kit.MuBottomSheet
import io.github.backspace135.musicunlock.ui.kit.MuIcons
import io.github.backspace135.musicunlock.ui.kit.MuSpinner
import io.github.backspace135.musicunlock.ui.kit.MuTopBar
import io.github.backspace135.musicunlock.ui.kit.NoticeBanner
import io.github.backspace135.musicunlock.ui.kit.OverflowMenu
import io.github.backspace135.musicunlock.ui.kit.PlainButton
import io.github.backspace135.musicunlock.ui.kit.PrimaryButton
import io.github.backspace135.musicunlock.ui.kit.SecondaryButton
import io.github.backspace135.musicunlock.ui.kit.SegmentedControl
import io.github.backspace135.musicunlock.ui.kit.StatusGlyph
import io.github.backspace135.musicunlock.ui.kit.SwipeToDeleteRow
import io.github.backspace135.musicunlock.ui.kit.pressable
import io.github.backspace135.musicunlock.ui.kit.rememberTopBarState
import io.github.backspace135.musicunlock.ui.nav.LocalNavigator
import io.github.backspace135.musicunlock.ui.nav.Route
import io.github.backspace135.musicunlock.ui.theme.MuMotion
import io.github.backspace135.musicunlock.ui.theme.MuShapes
import io.github.backspace135.musicunlock.ui.theme.MuSpacing
import io.github.backspace135.musicunlock.ui.theme.MuTheme

@Composable
fun UnlockScreen(vm: UnlockViewModel = viewModel()) {
    val context = LocalContext.current
    val navigator = LocalNavigator.current
    val topBar = rememberTopBarState()
    val tick by vm.queue.changes.collectAsState()
    val jobs = remember(tick) { vm.queue.jobs }
    val visible = remember(tick, vm.filter) { vm.queue.visible(vm.filter) }
    val running = remember(tick) { vm.queue.running }
    val stopRequested = remember(tick) { vm.queue.stopRequested }
    val waiting = remember(tick) { vm.queue.count(JobState.WAITING) }
    val finished = remember(tick) { vm.queue.count(JobState.DONE) }
    val failed = remember(tick) { vm.queue.count(JobState.FAILED) }
    val statusTitle = remember(tick) { vm.queue.statusTitle }
    val statusDetail = remember(tick) { vm.queue.statusDetail }
    val batchFinished = remember(tick) { vm.queue.batchFinished }
    val batchCount = remember(tick) { vm.queue.batchCount }
    var openJobId by rememberSaveable { mutableStateOf<Long?>(null) }
    var showExportSettings by rememberSaveable { mutableStateOf(false) }
    var confirmClear by rememberSaveable { mutableStateOf(false) }
    val scanning = vm.scanning

    val pickFiles = rememberLauncherForActivityResult(ActivityResultContracts.OpenMultipleDocuments()) { uris ->
        vm.importDocuments(uris)
    }
    val pickFolder = rememberLauncherForActivityResult(ActivityResultContracts.OpenDocumentTree()) { tree ->
        if (tree != null) vm.importTree(tree)
    }
    val askNotifications = rememberLauncherForActivityResult(ActivityResultContracts.RequestPermission()) {
        vm.start(context)
    }
    val askStorage = rememberLauncherForActivityResult(ActivityResultContracts.RequestMultiplePermissions()) {
        vm.start(context)
    }
    fun chooseFiles() {
        if (!scanning) pickFiles.launch(arrayOf("*/*"))
    }
    fun chooseFolder() {
        if (!scanning) pickFolder.launch(null)
    }
    fun start() {
        val permission = ProcessingService.notificationPermission
        if (permission != null && !vm.notificationsAsked && !context.granted(permission)) {
            vm.markNotificationsAsked()
            askNotifications.launch(permission)
            return
        }
        val missing = OutputFolders.requiredPermissions(UnlockSettings.values.value.outputFolder)
            .filterNot(context::granted)
        if (missing.isNotEmpty()) {
            askStorage.launch(missing.toTypedArray())
            return
        }
        vm.start(context)
    }

    Box(Modifier.fillMaxSize()) {
        Column(Modifier.fillMaxSize()) {
            MuTopBar(
                title = stringResource(R.string.unlock_title),
                state = topBar,
                actions = {
                    IconAction(
                        MuIcons.AddFolder,
                        stringResource(R.string.unlock_add_folder_description),
                        onClick = ::chooseFolder,
                        enabled = !scanning,
                    )
                    IconAction(
                        MuIcons.Add,
                        stringResource(R.string.unlock_add_files_description),
                        onClick = ::chooseFiles,
                        enabled = !scanning,
                    )
                    OverflowMenu(
                        listOf(
                            MenuItem(
                                stringResource(R.string.unlock_clear_finished_action),
                                enabled = finished > 0,
                                onClick = vm::clearFinished,
                            ),
                            MenuItem(
                                stringResource(R.string.unlock_clear_queue_action),
                                enabled = !running && jobs.isNotEmpty(),
                            ) { confirmClear = true },
                        ),
                    )
                    IconAction(MuIcons.Settings, stringResource(R.string.nav_settings_description), onClick = {
                        navigator.push(Route.Settings)
                    })
                },
            )
            NoticeBanner(vm.notice.ifEmpty { null }, onDismiss = vm::dismissNotice)
            if (scanning) {
                Row(
                    Modifier.padding(horizontal = MuSpacing.page, vertical = MuSpacing.s),
                    verticalAlignment = Alignment.CenterVertically,
                ) {
                    MuSpinner(size = 16.dp)
                    Text(
                        stringResource(R.string.unlock_scanning),
                        style = MuTheme.type.caption,
                        color = MuTheme.colors.secondary,
                        modifier = Modifier.padding(start = MuSpacing.s),
                    )
                }
            }
            LazyColumn(
                Modifier.fillMaxSize().nestedScroll(topBar.nestedScrollConnection),
                contentPadding = PaddingValues(bottom = if (jobs.isEmpty()) MuSpacing.xl else 112.dp),
            ) {
                if (jobs.isNotEmpty()) {
                    item(key = "filters") {
                        Column(Modifier.padding(horizontal = MuSpacing.page, vertical = MuSpacing.s)) {
                            Text(
                                stringResource(R.string.unlock_count_format, jobs.size),
                                style = MuTheme.type.caption,
                                color = MuTheme.colors.secondary,
                            )
                            Spacer(Modifier.height(MuSpacing.s))
                            SegmentedControl(
                                QueueFilter.entries.map { it.label },
                                vm.filter.ordinal,
                                onSelect = { vm.filter = QueueFilter.entries[it] },
                            )
                        }
                    }
                }
                item(key = "output") { OutputStrip { showExportSettings = true } }
                when {
                    jobs.isEmpty() -> item(key = "empty") { QueueEmptyState(scanning, ::chooseFiles, ::chooseFolder) }
                    visible.isEmpty() -> item(key = "filterEmpty") {
                        EmptyState(
                            title = stringResource(R.string.unlock_filter_empty_format, vm.filter.label),
                            icon = if (vm.filter == QueueFilter.DONE) MuIcons.Check else MuIcons.Info,
                        ) {
                            PlainButton(stringResource(R.string.unlock_filter_show_all_action), onClick = { vm.filter = QueueFilter.ALL })
                        }
                    }
                    else -> itemsIndexed(visible, key = { _, job -> job.id }) { index, job ->
                        LazyGroupItem(
                            index,
                            visible.size,
                            Modifier.animateItem(
                                fadeInSpec = MuMotion.fadeIn(),
                                placementSpec = MuMotion.list(),
                                fadeOutSpec = MuMotion.fadeOut(),
                            ),
                            dividerInset = 64.dp,
                        ) {
                            SwipeToDeleteRow(
                                onDelete = { vm.remove(job.id) },
                                label = stringResource(R.string.unlock_remove_action),
                                enabled = job.state != JobState.RUNNING,
                            ) {
                                JobRow(job, vm.queue) { openJobId = job.id }
                            }
                        }
                    }
                }
            }
        }

        ActionFooter(
            title = statusTitle,
            detail = statusDetail,
            progress = if (running) batchFinished.toFloat() / maxOf(1, batchCount) else null,
            actionLabel = when {
                running && stopRequested -> stringResource(R.string.unlock_stopping_action)
                running -> stringResource(R.string.unlock_stop_action)
                else -> stringResource(R.string.unlock_start_action)
            },
            onAction = { if (running) vm.stop() else start() },
            actionEnabled = if (running) !stopRequested else waiting > 0 && !scanning,
            secondaryLabel = if (failed > 0 && !running) stringResource(R.string.unlock_retry_action) else null,
            onSecondary = { vm.retryFailed(context) },
            visible = jobs.isNotEmpty(),
            modifier = Modifier.align(Alignment.BottomCenter),
        )
    }

    val openJob = jobs.firstOrNull { it.id == openJobId }
    MuBottomSheet(visible = openJob != null, onDismissRequest = { openJobId = null }, title = openJob?.displayTitle) {
        if (openJob != null) JobSheet(openJob, onDismiss = { openJobId = null }, onRemove = { vm.remove(openJob.id) })
    }
    MuBottomSheet(
        showExportSettings,
        { showExportSettings = false },
        title = stringResource(R.string.unlock_export_settings_title),
    ) {
        OutputSettingsSection()
        Spacer(Modifier.height(MuSpacing.l))
    }
    ConfirmDialog(
        visible = confirmClear,
        title = stringResource(R.string.unlock_clear_queue_title),
        message = stringResource(R.string.unlock_clear_queue_body),
        confirmLabel = stringResource(R.string.unlock_clear_queue_action),
        onConfirm = {
            confirmClear = false
            vm.clear()
        },
        onDismissRequest = { confirmClear = false },
        icon = MuIcons.Warning,
    )
}

@Composable
private fun JobSheet(job: AudioJob, onDismiss: () -> Unit, onRemove: () -> Unit) {
    val context = LocalContext.current
    Text(
        "${job.sourceFormat} · ${job.state.label}",
        style = MuTheme.type.subtitle,
        color = MuTheme.colors.secondary,
        modifier = Modifier.padding(horizontal = MuSpacing.page + MuSpacing.xs),
    )
    if (job.state == JobState.FAILED && job.detail.isNotEmpty()) {
        Text(
            job.detail,
            style = MuTheme.type.subtitle,
            modifier = Modifier
                .padding(horizontal = MuSpacing.page + MuSpacing.xs, vertical = MuSpacing.s)
                .combinedClickable(onClick = {}, onLongClick = { copyText(context, job.detail) }),
        )
    }
    Column(
        Modifier.fillMaxWidth().padding(MuSpacing.page),
        verticalArrangement = Arrangement.spacedBy(MuSpacing.s),
    ) {
        val uri = job.outputUri
        if (job.state == JobState.DONE && uri != null) {
            SecondaryButton(
                stringResource(R.string.unlock_open_action),
                { openOutput(context, uri) },
                Modifier.fillMaxWidth(),
            )
            SecondaryButton(
                stringResource(R.string.unlock_share_action),
                { shareOutput(context, uri) },
                Modifier.fillMaxWidth(),
                icon = MuIcons.Share,
            )
        }
        // "查找这首歌的歌词" stays hidden until #32's lyrics workspace can take
        // LyricWorkspace.searchForAudio(displayTitle). The core method exists; the screen does not.
        if (job.state != JobState.RUNNING) {
            SecondaryButton(
                stringResource(R.string.unlock_remove_from_queue_action),
                {
                    onDismiss()
                    onRemove()
                },
                Modifier.fillMaxWidth(),
                icon = MuIcons.Delete,
            )
        }
    }
}

@Composable
private fun JobRow(job: AudioJob, queue: io.github.backspace135.musicunlock.core.AudioQueue, onClick: () -> Unit) {
    val context = LocalContext.current
    val failed = job.state == JobState.FAILED && job.detail.isNotEmpty()
    ListRow(
        title = job.displayTitle,
        subtitle = job.subtitle,
        highlighted = job.state == JobState.RUNNING,
        onClick = onClick,
        onLongClick = if (failed) ({ copyText(context, job.detail) }) else null,
        modifier = Modifier.semantics { contentDescription = job.spokenSummary },
        leading = { Cover(job, queue) },
        extra = if (failed) {
            {
                Text(job.detail, style = MuTheme.type.caption, modifier = Modifier.padding(top = 2.dp))
            }
        } else {
            null
        },
        trailing = {
            Column(horizontalAlignment = Alignment.End) {
                Text(job.sourceFormat, style = MuTheme.type.caption, color = MuTheme.colors.secondary)
                if (job.outputExtension.isNotEmpty()) {
                    Text(
                        "↳ ${job.outputExtension.uppercase()}",
                        style = MuTheme.type.caption,
                        color = MuTheme.colors.secondary,
                    )
                }
                Row(verticalAlignment = Alignment.CenterVertically, modifier = Modifier.padding(top = 2.dp)) {
                    StatusGlyph(job.state.glyph, contentDescription = job.state.label, size = 16.dp)
                    Text(
                        job.state.label,
                        style = MuTheme.type.caption,
                        modifier = Modifier.padding(start = MuSpacing.xs),
                    )
                }
            }
        },
    )
}

@Composable
private fun Cover(job: AudioJob, queue: io.github.backspace135.musicunlock.core.AudioQueue) {
    val bytes = remember(job.id, job.hasCover) { if (job.hasCover) queue.cover(job.id) else null }
    val image = remember(bytes) { bytes?.let { BitmapFactory.decodeByteArray(it, 0, it.size)?.asImageBitmap() } }
    Box(
        Modifier.size(36.dp, 40.dp).clip(RoundedCornerShape(7.dp)).background(MuTheme.colors.fill),
        contentAlignment = Alignment.Center,
    ) {
        if (image != null) {
            Image(image, contentDescription = null, modifier = Modifier.fillMaxSize(), contentScale = ContentScale.Crop)
        } else {
            Icon(MuIcons.MusicNote, null, Modifier.size(20.dp), tint = MuTheme.colors.secondary)
        }
    }
}

@Composable
private fun OutputStrip(onClick: () -> Unit) {
    val settings by UnlockSettings.values.collectAsState()
    val label = OutputFolders.label(settings.outputFolder)
    Row(
        Modifier.padding(horizontal = MuSpacing.page).pressable(onClick, pressedScale = 0.98f).padding(vertical = MuSpacing.s),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Text(
            stringResource(R.string.unlock_output_strip_format, label.name, settings.naming.label),
            style = MuTheme.type.caption,
            color = MuTheme.colors.secondary,
        )
        Icon(MuIcons.Chevron, null, Modifier.size(16.dp), tint = MuTheme.colors.secondary)
    }
}

@Composable
private fun QueueEmptyState(scanning: Boolean, onFiles: () -> Unit, onFolder: () -> Unit) {
    val context = LocalContext.current
    val drift = Settings.Global.getFloat(context.contentResolver, Settings.Global.ANIMATOR_DURATION_SCALE, 1f) > 0f
    EmptyState(
        title = stringResource(R.string.unlock_empty_title),
        body = stringResource(R.string.unlock_empty_body),
        drift = drift,
        modifier = Modifier.padding(top = MuSpacing.xl),
        artwork = {
            Image(
                painterResource(R.drawable.unlock_app_icon),
                contentDescription = null,
                modifier = Modifier.size(112.dp),
            )
        },
    ) {
        PrimaryButton(stringResource(R.string.unlock_empty_pick_files_action), onFiles, enabled = !scanning)
        Spacer(Modifier.height(MuSpacing.s))
        SecondaryButton(stringResource(R.string.unlock_empty_pick_folder_action), onFolder, enabled = !scanning)
        Spacer(Modifier.height(MuSpacing.l))
        Row(horizontalArrangement = Arrangement.spacedBy(MuSpacing.s)) {
            FormatChip(stringResource(R.string.unlock_chip_ncm))
            FormatChip(stringResource(R.string.unlock_chip_qmc))
            FormatChip(stringResource(R.string.unlock_chip_mflac))
            FormatChip(stringResource(R.string.unlock_chip_mgg))
        }
        Spacer(Modifier.height(MuSpacing.xl))
        Row(verticalAlignment = Alignment.CenterVertically) {
            Icon(MuIcons.Shield, null, Modifier.size(16.dp), tint = MuTheme.colors.secondary)
            Text(
                stringResource(R.string.unlock_empty_local_hint),
                style = MuTheme.type.caption,
                color = MuTheme.colors.secondary,
                modifier = Modifier.padding(start = MuSpacing.xs),
            )
        }
    }
}

@Composable
private fun FormatChip(text: String) {
    Text(
        text,
        style = MuTheme.type.caption,
        color = MuTheme.colors.secondary,
        modifier = Modifier
            .clip(MuShapes.small)
            .background(MuTheme.colors.fill)
            .padding(horizontal = MuSpacing.s, vertical = MuSpacing.xs),
    )
}

private val JobState.glyph: JobGlyph
    get() = when (this) {
        JobState.WAITING -> JobGlyph.Pending
        JobState.RUNNING -> JobGlyph.Running
        JobState.DONE -> JobGlyph.Done
        JobState.FAILED -> JobGlyph.Failed
    }

private fun Context.granted(permission: String): Boolean =
    ContextCompat.checkSelfPermission(this, permission) == PackageManager.PERMISSION_GRANTED

private fun copyText(context: Context, text: String) {
    val clipboard = context.getSystemService(ClipboardManager::class.java) ?: return
    clipboard.setPrimaryClip(ClipData.newPlainText("musicunlock", text))
}

private fun openOutput(context: Context, uri: Uri) {
    val intent = Intent(Intent.ACTION_VIEW)
        .setDataAndType(uri, "audio/*")
        .addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION)
        .apply { clipData = ClipData.newRawUri("", uri) }
    try {
        context.startActivity(intent)
    } catch (_: ActivityNotFoundException) {
        UnlockSession.notice = context.getString(R.string.unlock_open_unavailable)
    }
}

private fun shareOutput(context: Context, uri: Uri) {
    val share = Intent(Intent.ACTION_SEND)
        .setType("audio/*")
        .putExtra(Intent.EXTRA_STREAM, uri)
        .addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION)
        .apply { clipData = ClipData.newRawUri("", uri) }
    try {
        context.startActivity(Intent.createChooser(share, null).addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION))
    } catch (_: ActivityNotFoundException) {
        UnlockSession.notice = context.getString(R.string.unlock_open_unavailable)
    }
}
