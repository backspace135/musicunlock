package io.github.backspace135.musicunlock.ui.unlock

import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.stringResource
import io.github.backspace135.musicunlock.R
import io.github.backspace135.musicunlock.ui.kit.ListGroup
import io.github.backspace135.musicunlock.ui.kit.NavigationRow
import io.github.backspace135.musicunlock.ui.kit.RowDivider

// PLACEHOLDER (#30). #31 fills this section: output folder and naming policy ("导出设置" in #31). It is
// shown on the settings page and in the queue's export-settings sheet.

@Composable
fun OutputSettingsSection(modifier: Modifier = Modifier) {
    ListGroup(modifier, header = stringResource(R.string.settings_output_header), footer = stringResource(R.string.unlock_status_detail)) {
        NavigationRow(stringResource(R.string.settings_output_folder), onClick = {}, value = "Music/音匣")
        RowDivider()
        NavigationRow(stringResource(R.string.settings_output_naming), onClick = {}, value = stringResource(R.string.unlock_naming_original))
    }
}
