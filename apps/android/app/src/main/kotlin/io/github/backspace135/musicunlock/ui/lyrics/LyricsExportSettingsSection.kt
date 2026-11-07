package io.github.backspace135.musicunlock.ui.lyrics

import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.unit.dp
import androidx.compose.ui.text.input.KeyboardType
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import io.github.backspace135.musicunlock.R
import io.github.backspace135.musicunlock.core.LyricEncoding
import io.github.backspace135.musicunlock.core.LyricFormat
import io.github.backspace135.musicunlock.core.LyricLayout
import io.github.backspace135.musicunlock.core.LyricOptions
import io.github.backspace135.musicunlock.core.LyricSettings
import io.github.backspace135.musicunlock.core.Lyrics
import io.github.backspace135.musicunlock.core.OutputFolders
import io.github.backspace135.musicunlock.ui.kit.CheckRow
import io.github.backspace135.musicunlock.ui.kit.ListGroup
import io.github.backspace135.musicunlock.ui.kit.MuBottomSheet
import io.github.backspace135.musicunlock.ui.kit.MuIcons
import io.github.backspace135.musicunlock.ui.kit.MuTextField
import io.github.backspace135.musicunlock.ui.kit.NavigationRow
import io.github.backspace135.musicunlock.ui.kit.OptionRow
import io.github.backspace135.musicunlock.ui.kit.RowDivider
import io.github.backspace135.musicunlock.ui.kit.SegmentedControl
import io.github.backspace135.musicunlock.ui.theme.MuSpacing
import io.github.backspace135.musicunlock.ui.theme.MuTheme

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

/** 设置页和歌词工作区的「导出设置」共用这一份，改一处两边一起生效。 */
@Composable
fun LyricsExportSettingsContent() {
    val settings by LyricSettings.values.collectAsStateWithLifecycle()
    val options = settings.options
    val workspace = LyricsSession.workspace
    val revision = workspace.changes.collectAsStateWithLifecycle().value
    val exporting = workspace.exporting && revision >= 0L
    val fallback = stringResource(R.string.lyrics_folder_fallback)
    val pickFolder = rememberLauncherForActivityResult(ActivityResultContracts.OpenDocumentTree()) { uri ->
        if (uri != null) LyricsSession.setOutputFolder(uri)
    }
    LaunchedEffect(Unit) { LyricsSession.notifyFolderFallback(fallback) }

    Column(Modifier.verticalScroll(rememberScrollState())) {
        ListGroup(header = stringResource(R.string.lyrics_file_header)) {
            Column(Modifier.padding(MuSpacing.m)) {
                Text(stringResource(R.string.lyrics_export_format_label), style = MuTheme.type.caption, color = MuTheme.colors.secondary)
                SegmentedControl(
                    LyricFormat.entries.map { it.label },
                    options.format.ordinal,
                    { update(options) { copy(format = LyricFormat.entries[it]) } },
                    Modifier.padding(top = MuSpacing.s),
                )
                Text(
                    stringResource(R.string.lyrics_encoding_label),
                    style = MuTheme.type.caption,
                    color = MuTheme.colors.secondary,
                    modifier = Modifier.padding(top = MuSpacing.m),
                )
                SegmentedControl(
                    LyricEncoding.entries.map { it.label },
                    options.encoding.ordinal,
                    { update(options) { copy(encoding = LyricEncoding.entries[it]) } },
                    Modifier.padding(top = MuSpacing.s),
                )
                MuTextField(
                    value = options.filename,
                    onValueChange = { update(options) { copy(filename = it) } },
                    label = stringResource(R.string.lyrics_filename_label),
                    caption = stringResource(R.string.lyrics_filename_hint),
                    modifier = Modifier.padding(top = MuSpacing.m),
                )
            }
            RowDivider()
            NavigationRow(
                stringResource(R.string.lyrics_output_folder_label),
                onClick = { pickFolder.launch(null) },
                subtitle = OutputFolders.path(settings.outputFolder),
                icon = MuIcons.Folder,
                enabled = !exporting,
            )
        }

        ListGroup(header = stringResource(R.string.lyrics_content_header), footer = stringResource(R.string.lyrics_content_hint), modifier = Modifier.padding(top = MuSpacing.l)) {
            CheckRow(stringResource(R.string.lyrics_include_original), options.includeOriginal, { update(options) { copy(includeOriginal = it) } })
            RowDivider(inset = 50.dp)
            CheckRow(stringResource(R.string.lyrics_include_translation), options.includeTranslation, { update(options) { copy(includeTranslation = it) } })
            RowDivider(inset = 50.dp)
            CheckRow(stringResource(R.string.lyrics_include_romanization), options.includeRomanization, { update(options) { copy(includeRomanization = it) } })
            if (Lyrics.pinyinSupported) {
                RowDivider(inset = 50.dp)
                CheckRow(stringResource(R.string.lyrics_pinyin), options.includePinyin, { update(options) { copy(includePinyin = it) } })
            }
        }

        ListGroup(header = stringResource(R.string.lyrics_timing_header), modifier = Modifier.padding(top = MuSpacing.l)) {
            Text(
                stringResource(R.string.lyrics_layout_label),
                style = MuTheme.type.caption,
                color = MuTheme.colors.secondary,
                modifier = Modifier.padding(start = MuSpacing.l, top = MuSpacing.m, bottom = MuSpacing.xs),
            )
            LyricLayout.entries.forEachIndexed { index, layout ->
                if (index > 0) RowDivider()
                OptionRow(layout.label, selected = options.layout == layout, onClick = { update(options) { copy(layout = layout) } })
            }
            Column(Modifier.padding(MuSpacing.m)) {
                MuTextField(
                    value = options.separator,
                    onValueChange = { update(options) { copy(separator = it) } },
                    label = stringResource(R.string.lyrics_separator_label),
                    enabled = options.layout == LyricLayout.MERGE,
                )
                Text(
                    stringResource(R.string.lyrics_precision_label),
                    style = MuTheme.type.caption,
                    color = MuTheme.colors.secondary,
                    modifier = Modifier.padding(top = MuSpacing.m),
                )
                SegmentedControl(
                    listOf(
                        stringResource(R.string.lyrics_precision_1),
                        stringResource(R.string.lyrics_precision_2),
                        stringResource(R.string.lyrics_precision_3),
                    ),
                    (options.precision - 1).coerceIn(0, 2),
                    { update(options) { copy(precision = it + 1) } },
                    Modifier.padding(top = MuSpacing.s),
                )
                OffsetField(options)
            }
            RowDivider()
            CheckRow(stringResource(R.string.lyrics_ignore_empty), options.ignoreEmpty, { update(options) { copy(ignoreEmpty = it) } })
            RowDivider(inset = 50.dp)
            CheckRow(stringResource(R.string.lyrics_skip_instrumental), options.skipInstrumental, { update(options) { copy(skipInstrumental = it) } })
            RowDivider(inset = 50.dp)
            CheckRow(stringResource(R.string.lyrics_word_timing), options.wordTiming, { update(options) { copy(wordTiming = it) } })
        }
    }
}

@Composable
private fun OffsetField(options: LyricOptions) {
    var text by rememberSaveable { mutableStateOf(options.offset.toString()) }
    LaunchedEffect(options.offset) {
        val parsed = text.toLongOrNull()
        if (text.isNotEmpty() && parsed != options.offset) text = options.offset.toString()
    }
    MuTextField(
        value = text,
        onValueChange = { raw ->
            val next = raw.trim()
            if (next.isNotEmpty() && !next.matches(Regex("-?\\d*"))) return@MuTextField
            text = next
            val parsed = next.toLongOrNull() ?: return@MuTextField
            val clamped = parsed.coerceIn(-86_400_000L, 86_400_000L)
            if (clamped != options.offset) update(options) { copy(offset = clamped) }
            if (clamped != parsed) text = clamped.toString()
        },
        label = stringResource(R.string.lyrics_offset_label),
        caption = stringResource(R.string.lyrics_offset_hint),
        keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Number),
        modifier = Modifier.padding(top = MuSpacing.m),
    )
}

private fun update(options: LyricOptions, change: LyricOptions.() -> LyricOptions) {
    LyricsSession.setOptions(options.change())
}
