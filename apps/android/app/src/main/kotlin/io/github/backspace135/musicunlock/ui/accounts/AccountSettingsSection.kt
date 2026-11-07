package io.github.backspace135.musicunlock.ui.accounts

import androidx.compose.runtime.Composable
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.stringResource
import io.github.backspace135.musicunlock.R
import io.github.backspace135.musicunlock.core.AccountStore
import io.github.backspace135.musicunlock.core.MusicProvider
import io.github.backspace135.musicunlock.ui.kit.ListGroup
import io.github.backspace135.musicunlock.ui.kit.MuIcons
import io.github.backspace135.musicunlock.ui.kit.NavigationRow
import io.github.backspace135.musicunlock.ui.nav.LocalNavigator
import io.github.backspace135.musicunlock.ui.nav.Route

/** Settings entry for account login. The value is which platforms currently have a saved session. */
@Composable
fun AccountSettingsSection(modifier: Modifier = Modifier) {
    val navigator = LocalNavigator.current
    val signedIn by AccountStore.signedInState.collectAsState()
    val netease = signedIn[MusicProvider.NETEASE] == true
    val qq = signedIn[MusicProvider.QQ] == true
    val value = when {
        netease && qq -> stringResource(R.string.accounts_signed_in_both)
        netease -> stringResource(R.string.accounts_netease)
        qq -> stringResource(R.string.accounts_qq)
        else -> stringResource(R.string.accounts_signed_out)
    }
    ListGroup(modifier, header = stringResource(R.string.settings_accounts_header)) {
        NavigationRow(
            stringResource(R.string.settings_accounts_entry),
            onClick = { navigator.push(Route.Accounts) },
            value = value,
            icon = MuIcons.Account,
        )
    }
}
