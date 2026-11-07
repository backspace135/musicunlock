package io.github.backspace135.musicunlock.ui.settings

import android.content.Intent
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.WindowInsets
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.navigationBars
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.windowInsetsBottomHeight
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Icon
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.produceState
import androidx.compose.runtime.remember
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.input.nestedscroll.nestedScroll
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import androidx.core.net.toUri
import io.github.backspace135.musicunlock.R
import io.github.backspace135.musicunlock.ui.kit.ListGroup
import io.github.backspace135.musicunlock.ui.kit.MuIcons
import io.github.backspace135.musicunlock.ui.kit.MuSpinner
import io.github.backspace135.musicunlock.ui.kit.MuTopBar
import io.github.backspace135.musicunlock.ui.kit.NavigationRow
import io.github.backspace135.musicunlock.ui.kit.RowDivider
import io.github.backspace135.musicunlock.ui.kit.rememberTopBarState
import io.github.backspace135.musicunlock.ui.nav.LocalNavigator
import io.github.backspace135.musicunlock.ui.nav.Route
import io.github.backspace135.musicunlock.ui.theme.MuSpacing
import io.github.backspace135.musicunlock.ui.theme.MuTheme
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext

private const val SourceUrl = "https://github.com/backspace135/musicunlock"

@Composable
fun AboutScreen() {
    val navigator = LocalNavigator.current
    val context = LocalContext.current
    val version = remember { context.packageManager.getPackageInfo(context.packageName, 0).versionName.orEmpty() }
    Column(Modifier.fillMaxSize()) {
        MuTopBar(stringResource(R.string.about_title), onBack = navigator::pop)
        Column(Modifier.verticalScroll(rememberScrollState()), horizontalAlignment = Alignment.CenterHorizontally) {
            Spacer(Modifier.height(MuSpacing.xl))
            Box(Modifier.size(88.dp).clip(CircleShape).background(MuTheme.colors.fill), contentAlignment = Alignment.Center) {
                Icon(MuIcons.MusicNote, null, Modifier.size(40.dp), tint = MuTheme.colors.text)
            }
            Text(stringResource(R.string.app_name), style = MuTheme.type.title, modifier = Modifier.padding(top = MuSpacing.m))
            Text(stringResource(R.string.about_version_format, version), style = MuTheme.type.caption, color = MuTheme.colors.secondary)
            Text(stringResource(R.string.about_subtitle), style = MuTheme.type.caption, color = MuTheme.colors.secondary)
            Text(stringResource(R.string.about_platform), style = MuTheme.type.caption, color = MuTheme.colors.secondary)
            Text(
                stringResource(R.string.about_body),
                style = MuTheme.type.subtitle,
                color = MuTheme.colors.secondary,
                textAlign = TextAlign.Center,
                modifier = Modifier.padding(horizontal = MuSpacing.xxl, vertical = MuSpacing.l),
            )
            ListGroup {
                NavigationRow(stringResource(R.string.about_licenses_entry), onClick = { navigator.push(Route.Licenses) })
                RowDivider()
                NavigationRow(
                    stringResource(R.string.about_source_entry),
                    onClick = { context.startActivity(Intent(Intent.ACTION_VIEW, SourceUrl.toUri())) },
                    value = "GitHub",
                )
            }
            Spacer(Modifier.windowInsetsBottomHeight(WindowInsets.navigationBars))
        }
    }
}

@Composable
fun LicensesScreen() {
    val navigator = LocalNavigator.current
    val app = stringResource(R.string.licenses_app)
    val notices = stringResource(R.string.licenses_notices)
    val android = stringResource(R.string.licenses_android)
    Column(Modifier.fillMaxSize()) {
        MuTopBar(stringResource(R.string.licenses_title), onBack = navigator::pop)
        ListGroup(Modifier.padding(top = MuSpacing.s)) {
            NavigationRow(app, { navigator.push(Route.LicenseText("licenses/LICENSE", app)) }, subtitle = stringResource(R.string.licenses_app_subtitle))
            RowDivider()
            NavigationRow(notices, { navigator.push(Route.LicenseText("licenses/THIRD_PARTY_NOTICES.txt", notices)) }, subtitle = stringResource(R.string.licenses_notices_subtitle))
            RowDivider()
            NavigationRow(android, { navigator.push(Route.LicenseText("licenses/apache-2.0.txt", android)) }, subtitle = stringResource(R.string.licenses_android_subtitle))
        }
    }
}

/** Plain text of a license file from assets/, paragraph by paragraph so long files scroll smoothly. */
@Composable
fun LicenseTextScreen(asset: String, title: String) {
    val navigator = LocalNavigator.current
    val context = LocalContext.current
    val topBar = rememberTopBarState()
    val paragraphs by produceState<List<String>?>(null, asset) {
        value = withContext(Dispatchers.IO) {
            context.assets.open(asset).bufferedReader().use { it.readText() }.split(Regex("\n\\s*\n"))
        }
    }
    Column(Modifier.fillMaxSize()) {
        MuTopBar(title, state = topBar, onBack = navigator::pop)
        val text = paragraphs
        if (text == null) {
            Box(Modifier.fillMaxWidth().padding(MuSpacing.xl), contentAlignment = Alignment.Center) { MuSpinner() }
        } else {
            LazyColumn(
                Modifier.fillMaxSize().nestedScroll(topBar.nestedScrollConnection),
                verticalArrangement = Arrangement.spacedBy(MuSpacing.m),
            ) {
                items(text.size) { index ->
                    Text(text[index].trimEnd(), style = MuTheme.type.caption, modifier = Modifier.padding(horizontal = MuSpacing.page))
                }
                item { Spacer(Modifier.windowInsetsBottomHeight(WindowInsets.navigationBars)) }
            }
        }
    }
}
