package io.github.backspace135.musicunlock.ui.accounts

import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.stringResource
import io.github.backspace135.musicunlock.R
import io.github.backspace135.musicunlock.ui.kit.ListGroup
import io.github.backspace135.musicunlock.ui.kit.MuIcons
import io.github.backspace135.musicunlock.ui.kit.NavigationRow
import io.github.backspace135.musicunlock.ui.nav.LocalNavigator
import io.github.backspace135.musicunlock.ui.nav.Route

// PLACEHOLDER (#30). #33 fills this section: the "账号登录" entry, with the signed-in state as its value.

@Composable
fun AccountSettingsSection(modifier: Modifier = Modifier) {
    val navigator = LocalNavigator.current
    ListGroup(modifier, header = stringResource(R.string.settings_accounts_header)) {
        NavigationRow(
            stringResource(R.string.settings_accounts_entry),
            onClick = { navigator.push(Route.Accounts) },
            value = stringResource(R.string.accounts_signed_out),
            icon = MuIcons.Account,
        )
    }
}
