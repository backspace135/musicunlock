package io.github.backspace135.musicunlock.ui.lyrics

import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.navigationBarsPadding
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.unit.dp
import io.github.backspace135.musicunlock.R
import io.github.backspace135.musicunlock.ui.kit.ActionFooter
import io.github.backspace135.musicunlock.ui.kit.ConfirmDialog
import io.github.backspace135.musicunlock.ui.kit.IconAction
import io.github.backspace135.musicunlock.ui.kit.MuIcons
import io.github.backspace135.musicunlock.ui.kit.MuTextField
import io.github.backspace135.musicunlock.ui.kit.MuTopBar
import io.github.backspace135.musicunlock.ui.kit.SegmentedControl
import io.github.backspace135.musicunlock.ui.nav.LocalNavigator
import io.github.backspace135.musicunlock.ui.theme.MuSpacing

// PLACEHOLDER (#30) with fake data. #32 replaces this file (page two in #32).

@Composable
fun LyricsEditorScreen(songId: String) {
    val navigator = LocalNavigator.current
    val song = SampleSongs.firstOrNull { it.id == songId } ?: SampleSongs.first()
    var tab by rememberSaveable { mutableIntStateOf(0) }
    var text by rememberSaveable { mutableStateOf(SampleLyrics) }
    var confirmReload by rememberSaveable { mutableStateOf(false) }

    Box(Modifier.fillMaxSize()) {
        Column(Modifier.fillMaxSize()) {
            MuTopBar(
                title = song.title,
                onBack = navigator::pop,
                actions = { IconAction(MuIcons.Refresh, stringResource(R.string.lyrics_reload_description), { confirmReload = true }) },
            )
            SegmentedControl(
                listOf(
                    stringResource(R.string.lyrics_tab_original),
                    stringResource(R.string.lyrics_tab_translation),
                    stringResource(R.string.lyrics_tab_romanization),
                    stringResource(R.string.lyrics_tab_preview),
                ),
                tab,
                { tab = it },
                Modifier.padding(horizontal = MuSpacing.page, vertical = MuSpacing.s),
            )
            MuTextField(
                value = text,
                onValueChange = { text = it },
                singleLine = false,
                monospace = true,
                readOnly = tab == 3,
                modifier = Modifier
                    .weight(1f)
                    .verticalScroll(rememberScrollState())
                    .padding(horizontal = MuSpacing.page)
                    .padding(bottom = 112.dp),
            )
        }
        ActionFooter(
            title = stringResource(R.string.lyrics_editable_hint),
            detail = stringResource(R.string.lyrics_line_count_format, text.lines().size),
            actionLabel = stringResource(R.string.lyrics_export_current_action),
            onAction = {},
            modifier = Modifier.align(Alignment.BottomCenter).navigationBarsPadding(),
        )
    }
    ConfirmDialog(
        visible = confirmReload,
        title = stringResource(R.string.lyrics_reload_title),
        confirmLabel = stringResource(R.string.lyrics_reload_action),
        onConfirm = {
            confirmReload = false
            text = SampleLyrics
        },
        onDismissRequest = { confirmReload = false },
        icon = MuIcons.Warning,
    )
}
