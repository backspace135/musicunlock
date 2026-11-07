package io.github.backspace135.musicunlock.ui.unlock

import android.content.ActivityNotFoundException
import android.content.Context
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.remember
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.stringResource
import io.github.backspace135.musicunlock.R
import io.github.backspace135.musicunlock.core.NamingPolicy
import io.github.backspace135.musicunlock.core.OutputFolder
import io.github.backspace135.musicunlock.core.OutputFolders
import io.github.backspace135.musicunlock.core.UnlockSettings
import io.github.backspace135.musicunlock.ui.kit.ListGroup
import io.github.backspace135.musicunlock.ui.kit.MuIcons
import io.github.backspace135.musicunlock.ui.kit.NavigationRow
import io.github.backspace135.musicunlock.ui.kit.OptionRow
import io.github.backspace135.musicunlock.ui.kit.RowDivider
import io.github.backspace135.musicunlock.ui.kit.SectionHeader
import io.github.backspace135.musicunlock.ui.theme.MuSpacing
import io.github.backspace135.musicunlock.ui.theme.MuTheme

/**
 * Output folder and naming. Shown on the settings page and in the queue's export-settings sheet;
 * one edit updates both. The two choices are locked while a batch is running.
 */
@Composable
fun OutputSettingsSection(modifier: Modifier = Modifier) {
    val context = LocalContext.current
    val tick by UnlockSession.queue.changes.collectAsState()
    val running = remember(tick) { UnlockSession.queue.running }
    val settings by UnlockSettings.values.collectAsState()
    val label = OutputFolders.label(settings.outputFolder)
    val pickOutput = rememberLauncherForActivityResult(ActivityResultContracts.OpenDocumentTree()) { tree ->
        if (tree != null) {
            UnlockSettings.setOutputFolder(OutputFolders.remember(tree, UnlockSettings.values.value.outputFolder))
        }
    }
    LaunchedEffect(Unit) { UnlockSession.fallBackIfOutputUnusable(context) }

    Column(modifier) {
        ListGroup(header = stringResource(R.string.settings_output_folder)) {
            NavigationRow(
                title = label.name,
                subtitle = label.location,
                icon = MuIcons.Folder,
                enabled = !running,
                onClick = { if (!running) pickOutput.launch(null) },
            )
            RowDivider()
            NavigationRow(
                title = stringResource(R.string.unlock_view_in_files_action),
                icon = MuIcons.Folder,
                onClick = { viewFolder(context, settings.outputFolder) },
            )
        }
        ListGroup(
            header = stringResource(R.string.settings_output_naming),
            footer = stringResource(R.string.unlock_naming_hint),
        ) {
            NamingPolicy.entries.forEachIndexed { index, policy ->
                if (index > 0) RowDivider()
                OptionRow(
                    title = policy.label,
                    selected = policy == settings.naming,
                    enabled = !running,
                    onClick = { if (!running) UnlockSettings.setNaming(policy) },
                )
            }
        }
        Column(Modifier.padding(horizontal = MuSpacing.page + MuSpacing.l, vertical = MuSpacing.m)) {
            Text(stringResource(R.string.unlock_quality_title), style = MuTheme.type.body)
            Text(
                stringResource(R.string.unlock_quality_body),
                style = MuTheme.type.caption,
                color = MuTheme.colors.secondary,
                modifier = Modifier.padding(top = MuSpacing.s),
            )
        }
        SectionHeader(stringResource(R.string.unlock_sources_title), Modifier.padding(horizontal = MuSpacing.page))
        SourceLine(R.string.unlock_sources_netease, R.string.unlock_sources_netease_formats)
        SourceLine(R.string.unlock_sources_qq, R.string.unlock_sources_qq_formats)
        SourceLine(R.string.unlock_sources_more, R.string.unlock_sources_more_formats)
        Text(
            stringResource(R.string.unlock_sources_hint),
            style = MuTheme.type.caption,
            color = MuTheme.colors.secondary,
            modifier = Modifier.padding(horizontal = MuSpacing.page + MuSpacing.l, vertical = MuSpacing.s),
        )
    }
}

@Composable
private fun SourceLine(name: Int, formats: Int) {
    Row(
        Modifier
            .fillMaxWidth()
            .padding(horizontal = MuSpacing.page + MuSpacing.l, vertical = MuSpacing.xs),
        horizontalArrangement = Arrangement.SpaceBetween,
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Text(stringResource(name), style = MuTheme.type.caption, color = MuTheme.colors.secondary)
        Text(stringResource(formats), style = MuTheme.type.caption, color = MuTheme.colors.secondary)
    }
}

private fun viewFolder(context: Context, folder: OutputFolder) {
    try {
        context.startActivity(OutputFolders.viewIntent(folder))
    } catch (_: ActivityNotFoundException) {
        UnlockSession.notice = context.getString(R.string.unlock_view_in_files_failed_format, OutputFolders.path(folder))
    }
}
