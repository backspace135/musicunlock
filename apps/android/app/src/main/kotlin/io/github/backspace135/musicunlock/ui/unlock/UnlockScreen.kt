package io.github.backspace135.musicunlock.ui.unlock

import androidx.compose.foundation.background
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
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.input.nestedscroll.nestedScroll
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.unit.dp
import androidx.lifecycle.viewmodel.compose.viewModel
import io.github.backspace135.musicunlock.R
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
import io.github.backspace135.musicunlock.ui.shell.LocalShell
import io.github.backspace135.musicunlock.ui.shell.Workspace
import io.github.backspace135.musicunlock.ui.theme.MuMotion
import io.github.backspace135.musicunlock.ui.theme.MuSpacing
import io.github.backspace135.musicunlock.ui.theme.MuTheme

// PLACEHOLDER (#30) built from the kit with fake data. #31 replaces this file; the structure follows
// the page layout in #31 so most of it can stay.

@Composable
fun UnlockScreen(vm: UnlockViewModel = viewModel()) {
    val navigator = LocalNavigator.current
    val shell = LocalShell.current
    val topBar = rememberTopBarState()
    var openJobId by rememberSaveable { mutableStateOf<Long?>(null) }
    var showExportSettings by rememberSaveable { mutableStateOf(false) }
    var confirmClear by rememberSaveable { mutableStateOf(false) }
    val jobs = vm.jobs
    val visible = vm.visibleJobs

    Box(Modifier.fillMaxSize()) {
        Column(Modifier.fillMaxSize()) {
            MuTopBar(
                title = stringResource(R.string.unlock_title),
                state = topBar,
                actions = {
                    IconAction(MuIcons.AddFolder, stringResource(R.string.unlock_add_folder_description), vm::addFiles)
                    IconAction(MuIcons.Add, stringResource(R.string.unlock_add_files_description), vm::addFiles)
                    OverflowMenu(
                        listOf(
                            MenuItem(stringResource(R.string.unlock_clear_finished_action), enabled = vm.finishedCount > 0, onClick = vm::clearFinished),
                            MenuItem(stringResource(R.string.unlock_clear_queue_action), enabled = !vm.running && jobs.isNotEmpty()) { confirmClear = true },
                        ),
                    )
                    IconAction(MuIcons.Settings, stringResource(R.string.nav_settings_description), { navigator.push(Route.Settings) })
                },
            )
            // Outside the list: a banner inserted as the first lazy item would appear above the viewport.
            val notice = vm.notice
            NoticeBanner(notice?.let { stringResource(it.text, *it.args.toTypedArray()) }, onDismiss = { vm.notice = null })
            LazyColumn(
                Modifier.fillMaxSize().nestedScroll(topBar.nestedScrollConnection),
                contentPadding = PaddingValues(bottom = 112.dp),
            ) {
                if (jobs.isNotEmpty()) {
                    item(key = "filters") {
                        Column(Modifier.padding(horizontal = MuSpacing.page, vertical = MuSpacing.s)) {
                            Text(stringResource(R.string.unlock_count_format, jobs.size), style = MuTheme.type.caption, color = MuTheme.colors.secondary)
                            Spacer(Modifier.height(MuSpacing.s))
                            SegmentedControl(
                                QueueFilter.entries.map { stringResource(it.label) },
                                vm.filter.ordinal,
                                { vm.filter = QueueFilter.entries[it] },
                            )
                        }
                    }
                }
                item(key = "output") { OutputStrip { showExportSettings = true } }
                when {
                    jobs.isEmpty() -> item(key = "empty") { QueueEmptyState(vm::addFiles) }
                    visible.isEmpty() -> item(key = "filterEmpty") {
                        EmptyState(
                            title = stringResource(R.string.unlock_filter_empty_format, stringResource(vm.filter.label)),
                            icon = if (vm.filter == QueueFilter.Finished) MuIcons.Check else MuIcons.Info,
                        ) { PlainButton(stringResource(R.string.unlock_filter_show_all_action), { vm.filter = QueueFilter.All }) }
                    }
                    else -> itemsIndexed(visible, key = { _, job -> job.id }) { index, job ->
                        LazyGroupItem(
                            index,
                            visible.size,
                            Modifier.animateItem(fadeInSpec = MuMotion.fadeIn(), placementSpec = MuMotion.list(), fadeOutSpec = MuMotion.fadeOut()),
                            dividerInset = 64.dp,
                        ) {
                            SwipeToDeleteRow(
                                onDelete = { vm.remove(job.id) },
                                label = stringResource(R.string.unlock_remove_action),
                                enabled = job.state != JobState.Running,
                            ) {
                                JobRow(job) { openJobId = job.id }
                            }
                        }
                    }
                }
            }
        }

        ActionFooter(
            title = when {
                vm.running -> stringResource(R.string.unlock_status_running_format, vm.batchFinished + 1, vm.batchTotal)
                vm.pendingCount > 0 -> stringResource(R.string.unlock_status_ready_format, vm.pendingCount)
                else -> stringResource(R.string.unlock_status_idle)
            },
            detail = stringResource(R.string.unlock_status_detail),
            progress = if (vm.running) vm.batchFinished / vm.batchTotal.coerceAtLeast(1).toFloat() else null,
            actionLabel = stringResource(if (vm.running) R.string.unlock_stop_action else R.string.unlock_start_action),
            onAction = { if (vm.running) vm.stop() else vm.start() },
            actionEnabled = vm.running || vm.pendingCount > 0,
            secondaryLabel = if (vm.failedCount > 0 && !vm.running) stringResource(R.string.unlock_retry_action) else null,
            onSecondary = vm::retryFailed,
            visible = jobs.isNotEmpty(),
            modifier = Modifier.align(Alignment.BottomCenter),
        )
    }

    val openJob = jobs.firstOrNull { it.id == openJobId }
    MuBottomSheet(visible = openJob != null, onDismissRequest = { openJobId = null }, title = openJob?.title) {
        if (openJob != null) {
            Text(
                "${openJob.format} · ${stringResource(openJob.state.label)}",
                style = MuTheme.type.subtitle,
                color = MuTheme.colors.secondary,
                modifier = Modifier.padding(horizontal = MuSpacing.page + MuSpacing.xs),
            )
            if (openJob.detail != null) {
                Text(openJob.detail, style = MuTheme.type.subtitle, modifier = Modifier.padding(horizontal = MuSpacing.page + MuSpacing.xs, vertical = MuSpacing.s))
            }
            Column(Modifier.fillMaxWidth().padding(MuSpacing.page), verticalArrangement = Arrangement.spacedBy(MuSpacing.s)) {
                SecondaryButton(stringResource(R.string.unlock_find_lyrics_action), {
                    openJobId = null
                    shell.open(Workspace.Lyrics)
                }, Modifier.fillMaxWidth(), icon = MuIcons.Lyrics)
                if (openJob.state != JobState.Running) {
                    SecondaryButton(stringResource(R.string.unlock_remove_from_queue_action), {
                        openJobId = null
                        vm.remove(openJob.id)
                    }, Modifier.fillMaxWidth(), icon = MuIcons.Delete)
                }
            }
        }
    }
    MuBottomSheet(showExportSettings, { showExportSettings = false }, title = stringResource(R.string.unlock_export_settings_title)) {
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
            vm.clearQueue()
        },
        onDismissRequest = { confirmClear = false },
        icon = MuIcons.Warning,
    )
}

private val JobState.label: Int
    get() = when (this) {
        JobState.Pending -> R.string.unlock_state_pending
        JobState.Running -> R.string.unlock_state_running
        JobState.Done -> R.string.unlock_state_done
        JobState.Failed -> R.string.unlock_state_failed
    }

private val JobState.glyph: JobGlyph
    get() = when (this) {
        JobState.Pending -> JobGlyph.Pending
        JobState.Running -> JobGlyph.Running
        JobState.Done -> JobGlyph.Done
        JobState.Failed -> JobGlyph.Failed
    }

@Composable
private fun JobRow(job: FakeJob, onClick: () -> Unit) {
    val colors = MuTheme.colors
    ListRow(
        title = job.title,
        subtitle = "${job.platform} · ${job.sizeMb} MB",
        highlighted = job.state == JobState.Running,
        onClick = onClick,
        leading = {
            Box(Modifier.size(36.dp, 40.dp).clip(RoundedCornerShape(7.dp)).background(colors.fill), contentAlignment = Alignment.Center) {
                Icon(MuIcons.MusicNote, null, Modifier.size(20.dp), tint = colors.secondary)
            }
        },
        extra = job.detail?.let { { Text(it, style = MuTheme.type.caption, modifier = Modifier.padding(top = 2.dp)) } },
        trailing = {
            Column(horizontalAlignment = Alignment.End) {
                Text(job.format, style = MuTheme.type.caption, color = colors.secondary)
                if (job.output != null) Text("↳ ${job.output}", style = MuTheme.type.caption, color = colors.secondary)
                Row(verticalAlignment = Alignment.CenterVertically, modifier = Modifier.padding(top = 2.dp)) {
                    StatusGlyph(job.state.glyph, contentDescription = null, size = 16.dp)
                    Text(stringResource(job.state.label), style = MuTheme.type.caption, modifier = Modifier.padding(start = MuSpacing.xs))
                }
            }
        },
    )
}

@Composable
private fun OutputStrip(onClick: () -> Unit) {
    Row(
        Modifier
            .padding(horizontal = MuSpacing.page)
            .pressable(onClick, pressedScale = 0.98f)
            .padding(vertical = MuSpacing.s),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Text(
            stringResource(R.string.unlock_output_strip_format, stringResource(R.string.unlock_default_folder), stringResource(R.string.unlock_naming_original)),
            style = MuTheme.type.caption,
            color = MuTheme.colors.secondary,
        )
        Icon(MuIcons.Chevron, null, Modifier.size(16.dp), tint = MuTheme.colors.secondary)
    }
}

@Composable
private fun QueueEmptyState(onAdd: () -> Unit) {
    EmptyState(
        title = stringResource(R.string.unlock_empty_title),
        body = stringResource(R.string.unlock_empty_body),
        icon = MuIcons.MusicNote,
        drift = true,
        modifier = Modifier.padding(top = MuSpacing.xl),
    ) {
        PrimaryButton(stringResource(R.string.unlock_empty_pick_files_action), onAdd)
        Spacer(Modifier.height(MuSpacing.s))
        SecondaryButton(stringResource(R.string.unlock_empty_pick_folder_action), onAdd)
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
