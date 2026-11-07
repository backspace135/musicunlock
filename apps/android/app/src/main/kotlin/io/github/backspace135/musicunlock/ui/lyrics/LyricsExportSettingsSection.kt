package io.github.backspace135.musicunlock.ui.lyrics

import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.unit.dp
import io.github.backspace135.musicunlock.R
import io.github.backspace135.musicunlock.ui.kit.CheckRow
import io.github.backspace135.musicunlock.ui.kit.ListGroup
import io.github.backspace135.musicunlock.ui.kit.MuBottomSheet
import io.github.backspace135.musicunlock.ui.kit.MuIcons
import io.github.backspace135.musicunlock.ui.kit.NavigationRow
import io.github.backspace135.musicunlock.ui.kit.RowDivider
import io.github.backspace135.musicunlock.ui.kit.SegmentedControl
import io.github.backspace135.musicunlock.ui.kit.SwitchRow
import io.github.backspace135.musicunlock.ui.theme.MuSpacing

// PLACEHOLDER (#30). #32 fills the lyrics export settings ("导出设置" in #32); the entry on the settings
// page opens the same sheet as the workspace's "⋯" menu.

@Composable
fun LyricsExportSettingsSection(modifier: Modifier = Modifier) {
    var open by rememberSaveable { mutableStateOf(false) }
    ListGroup(modifier, header = stringResource(R.string.settings_lyrics_header)) {
        NavigationRow(stringResource(R.string.settings_lyrics_export), onClick = { open = true }, icon = MuIcons.Lyrics)
    }
    MuBottomSheet(open, { open = false }, title = stringResource(R.string.lyrics_export_settings_title)) {
        LyricsExportSettingsContent()
    }
}

/** Sheet content. Fake, unsaved state until #32 connects it to the core's export settings. */
@Composable
fun LyricsExportSettingsContent() {
    var format by rememberSaveable { mutableIntStateOf(0) }
    var original by rememberSaveable { mutableStateOf(true) }
    var translation by rememberSaveable { mutableStateOf(true) }
    var romanization by rememberSaveable { mutableStateOf(false) }
    var ignoreEmpty by rememberSaveable { mutableStateOf(true) }
    var skipInstrumental by rememberSaveable { mutableStateOf(true) }
    Column(Modifier.verticalScroll(rememberScrollState())) {
        ListGroup(header = stringResource(R.string.lyrics_export_format_label)) {
            Box(Modifier.padding(MuSpacing.m)) { SegmentedControl(ExportFormats, format, { format = it }) }
        }
        ListGroup(header = stringResource(R.string.settings_lyrics_header)) {
            CheckRow(stringResource(R.string.lyrics_include_original), original, { original = it })
            RowDivider(inset = 50.dp)
            CheckRow(stringResource(R.string.lyrics_include_translation), translation, { translation = it })
            RowDivider(inset = 50.dp)
            CheckRow(stringResource(R.string.lyrics_include_romanization), romanization, { romanization = it })
        }
        ListGroup(Modifier.padding(top = MuSpacing.l)) {
            SwitchRow(stringResource(R.string.lyrics_ignore_empty), ignoreEmpty, { ignoreEmpty = it })
            RowDivider()
            SwitchRow(stringResource(R.string.lyrics_skip_instrumental), skipInstrumental, { skipInstrumental = it })
        }
        Spacer(Modifier.height(MuSpacing.xl))
    }
}
