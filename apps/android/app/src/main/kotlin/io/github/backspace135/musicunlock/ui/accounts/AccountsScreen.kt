package io.github.backspace135.musicunlock.ui.accounts

import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.WindowInsets
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.navigationBars
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.windowInsetsBottomHeight
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Icon
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.unit.dp
import io.github.backspace135.musicunlock.R
import io.github.backspace135.musicunlock.ui.kit.ListGroup
import io.github.backspace135.musicunlock.ui.kit.ListRow
import io.github.backspace135.musicunlock.ui.kit.MuIcons
import io.github.backspace135.musicunlock.ui.kit.MuTopBar
import io.github.backspace135.musicunlock.ui.kit.RowIcon
import io.github.backspace135.musicunlock.ui.kit.SecondaryButton
import io.github.backspace135.musicunlock.ui.nav.LocalNavigator
import io.github.backspace135.musicunlock.ui.theme.MuSpacing
import io.github.backspace135.musicunlock.ui.theme.MuTheme

// PLACEHOLDER (#30). #33 replaces this file with the real account page ("入口：账号页" in #33).

@Composable
fun AccountsScreen() {
    val navigator = LocalNavigator.current
    Column(Modifier.fillMaxSize()) {
        MuTopBar(stringResource(R.string.accounts_title), onBack = navigator::pop)
        Column(Modifier.verticalScroll(rememberScrollState())) {
            Text(
                stringResource(R.string.accounts_intro),
                style = MuTheme.type.subtitle,
                color = MuTheme.colors.secondary,
                modifier = Modifier.padding(horizontal = MuSpacing.page + MuSpacing.xs, vertical = MuSpacing.s),
            )
            PlatformCard(stringResource(R.string.accounts_netease))
            PlatformCard(stringResource(R.string.accounts_qq))
            Row(Modifier.padding(horizontal = MuSpacing.page + MuSpacing.xs, vertical = MuSpacing.xl), verticalAlignment = Alignment.CenterVertically) {
                Icon(MuIcons.Lock, null, Modifier.size(16.dp), tint = MuTheme.colors.secondary)
                Text(
                    stringResource(R.string.accounts_storage_hint),
                    style = MuTheme.type.caption,
                    color = MuTheme.colors.secondary,
                    modifier = Modifier.padding(start = MuSpacing.xs),
                )
            }
            Spacer(Modifier.windowInsetsBottomHeight(WindowInsets.navigationBars))
        }
    }
}

@Composable
private fun PlatformCard(platform: String) {
    ListGroup(Modifier.padding(top = MuSpacing.m)) {
        ListRow(
            title = platform,
            subtitle = stringResource(R.string.accounts_signed_out),
            leading = { RowIcon(MuIcons.Account) },
            // #33: open the QR sheet (网易云) or the web login page (QQ 音乐).
            trailing = { SecondaryButton(stringResource(R.string.accounts_sign_in_action), {}, small = true) },
        )
    }
}
