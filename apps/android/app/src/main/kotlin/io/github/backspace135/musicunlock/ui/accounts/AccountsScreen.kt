package io.github.backspace135.musicunlock.ui.accounts

import android.Manifest
import android.content.ActivityNotFoundException
import android.content.Intent
import android.content.pm.PackageManager
import android.net.Uri
import android.os.Build
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.Image
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.WindowInsets
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.navigationBars
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.windowInsetsBottomHeight
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Icon
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.runtime.snapshotFlow
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.FilterQuality
import androidx.compose.ui.graphics.asImageBitmap
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import androidx.core.content.ContextCompat
import androidx.lifecycle.viewmodel.compose.viewModel
import io.github.backspace135.musicunlock.R
import io.github.backspace135.musicunlock.core.AccountStore
import io.github.backspace135.musicunlock.core.MusicProvider
import io.github.backspace135.musicunlock.core.QrLoginState
import io.github.backspace135.musicunlock.core.QrLoginUpdate
import io.github.backspace135.musicunlock.ui.kit.ListGroup
import io.github.backspace135.musicunlock.ui.kit.MuBottomSheet
import io.github.backspace135.musicunlock.ui.kit.MuIcons
import io.github.backspace135.musicunlock.ui.kit.MuSpinner
import io.github.backspace135.musicunlock.ui.kit.MuTopBar
import io.github.backspace135.musicunlock.ui.kit.NoticeBanner
import io.github.backspace135.musicunlock.ui.kit.SecondaryButton
import io.github.backspace135.musicunlock.ui.nav.LocalNavigator
import io.github.backspace135.musicunlock.ui.nav.Route
import io.github.backspace135.musicunlock.ui.theme.MuShapes
import io.github.backspace135.musicunlock.ui.theme.MuSpacing
import io.github.backspace135.musicunlock.ui.theme.MuTheme

@Composable
fun AccountsScreen(vm: AccountsViewModel = viewModel()) {
    val navigator = LocalNavigator.current
    val context = LocalContext.current
    val signedIn by AccountStore.signedInState.collectAsState()
    vm.bind(context, stringResource(R.string.accounts_qr_invalid))
    LaunchedEffect(Unit) {
        snapshotFlow { AccountFeedback.openQr }.collect { open ->
            if (open) {
                AccountFeedback.openQr = false
                vm.openQr()
            }
        }
    }

    Column(Modifier.fillMaxSize()) {
        MuTopBar(stringResource(R.string.accounts_title), onBack = navigator::pop)
        NoticeBanner(AccountFeedback.notice, onDismiss = { AccountFeedback.notice = null })
        Column(Modifier.verticalScroll(rememberScrollState())) {
            Text(
                stringResource(R.string.accounts_intro),
                style = MuTheme.type.subtitle,
                color = MuTheme.colors.secondary,
                modifier = Modifier.padding(horizontal = MuSpacing.page + MuSpacing.xs, vertical = MuSpacing.s),
            )
            ProviderCard(
                provider = MusicProvider.NETEASE,
                signedIn = signedIn[MusicProvider.NETEASE] == true,
                onLogin = vm::openQr,
                onRelogin = vm::openQr,
                onLogout = { vm.logout(MusicProvider.NETEASE) },
            )
            ProviderCard(
                provider = MusicProvider.QQ,
                signedIn = signedIn[MusicProvider.QQ] == true,
                onLogin = { navigator.push(Route.WebLogin(MusicProvider.QQ, relogin = false)) },
                onRelogin = { navigator.push(Route.WebLogin(MusicProvider.QQ, relogin = true)) },
                onLogout = { vm.logout(MusicProvider.QQ) },
            )
            Row(
                Modifier.padding(horizontal = MuSpacing.page + MuSpacing.xs, vertical = MuSpacing.xl),
                verticalAlignment = Alignment.CenterVertically,
            ) {
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

    QrLoginSheet(
        vm = vm,
        onOfficial = { relogin ->
            vm.closeQr()
            navigator.push(Route.WebLogin(MusicProvider.NETEASE, relogin))
        },
    )
}

@Composable
private fun ProviderCard(
    provider: MusicProvider,
    signedIn: Boolean,
    onLogin: () -> Unit,
    onRelogin: () -> Unit,
    onLogout: () -> Unit,
) {
    val reading = stringResource(R.string.accounts_reading)
    var accountLine by remember(provider) { mutableStateOf(reading) }
    LaunchedEffect(signedIn) {
        if (!signedIn) return@LaunchedEffect
        accountLine = reading
        val line = AccountStore.nickname(provider)
        if (signedIn) accountLine = line
    }
    ListGroup(Modifier.padding(top = MuSpacing.m)) {
        Column(Modifier.padding(MuSpacing.l), verticalArrangement = Arrangement.spacedBy(MuSpacing.s)) {
            if (signedIn) {
                Text(stringResource(R.string.accounts_saved_title_format, provider.title), style = MuTheme.type.headline)
                Text(accountLine, style = MuTheme.type.body)
                Text(
                    stringResource(R.string.accounts_session_hint),
                    style = MuTheme.type.caption,
                    color = MuTheme.colors.secondary,
                )
                Row(horizontalArrangement = Arrangement.spacedBy(MuSpacing.s)) {
                    SecondaryButton(stringResource(R.string.accounts_relogin_action), onRelogin, small = true)
                    SecondaryButton(stringResource(R.string.accounts_logout_format, provider.title), onLogout, small = true)
                }
            } else {
                Text(provider.title, style = MuTheme.type.headline)
                Text(stringResource(R.string.accounts_signed_out), style = MuTheme.type.subtitle, color = MuTheme.colors.secondary)
                SecondaryButton(stringResource(R.string.accounts_sign_in_action), onLogin, small = true)
            }
        }
    }
}

@Composable
private fun QrLoginSheet(vm: AccountsViewModel, onOfficial: (relogin: Boolean) -> Unit) {
    val context = LocalContext.current
    val signedIn by AccountStore.signedInState.collectAsState()
    val update = vm.qr
    val askStorage = rememberLauncherForActivityResult(ActivityResultContracts.RequestPermission()) { granted ->
        if (granted) {
            if (!vm.saveQr(context)) AccountFeedback.show(context.getString(R.string.accounts_qr_save_failed))
        } else {
            vm.storageDenied = true
            AccountFeedback.show(context.getString(R.string.accounts_qr_permission_hint))
        }
    }
    MuBottomSheet(
        visible = vm.qrVisible,
        onDismissRequest = vm::closeQr,
        title = stringResource(R.string.accounts_qr_title),
    ) {
        Column(
            Modifier
                .fillMaxWidth()
                .verticalScroll(rememberScrollState())
                .padding(horizontal = MuSpacing.page, vertical = MuSpacing.s),
            horizontalAlignment = Alignment.CenterHorizontally,
            verticalArrangement = Arrangement.spacedBy(MuSpacing.m),
        ) {
            QrCode(update)
            StatusLine(update)
            Text(
                stringResource(R.string.accounts_qr_scan_hint),
                style = MuTheme.type.caption,
                color = MuTheme.colors.secondary,
                textAlign = TextAlign.Center,
            )
            val state = update?.state ?: QrLoginState.LOADING
            SecondaryButton(
                text = stringResource(if (QrPresentation.regenerate(state)) R.string.accounts_qr_regenerate_action else R.string.accounts_qr_refresh_action),
                onClick = vm::startQr,
                enabled = QrPresentation.refreshEnabled(state),
                modifier = Modifier.fillMaxWidth(),
            )
            SecondaryButton(
                text = stringResource(R.string.accounts_qr_web_action),
                onClick = { onOfficial(signedIn[MusicProvider.NETEASE] == true) },
                modifier = Modifier.fillMaxWidth(),
            )
            val url = update?.url
            if (url != null && (state == QrLoginState.WAITING || state == QrLoginState.SCANNED)) {
                SecondaryButton(
                    text = stringResource(R.string.accounts_qr_open_action),
                    onClick = {
                        try {
                            context.startActivity(Intent(Intent.ACTION_VIEW, Uri.parse(url)))
                        } catch (_: ActivityNotFoundException) {
                            AccountFeedback.show(context.getString(R.string.accounts_qr_open_failed))
                        }
                    },
                    modifier = Modifier.fillMaxWidth(),
                )
                SecondaryButton(
                    text = stringResource(R.string.accounts_qr_save_action),
                    onClick = {
                        if (needsStoragePermission(context)) {
                            if (vm.storageDenied) AccountFeedback.show(context.getString(R.string.accounts_qr_permission_hint))
                            else askStorage.launch(Manifest.permission.WRITE_EXTERNAL_STORAGE)
                        } else if (!vm.saveQr(context)) {
                            AccountFeedback.show(context.getString(R.string.accounts_qr_save_failed))
                        }
                    },
                    modifier = Modifier.fillMaxWidth(),
                )
            }
            if (vm.qrSaved) {
                Text(
                    stringResource(R.string.accounts_qr_saved_hint),
                    style = MuTheme.type.caption,
                    color = MuTheme.colors.secondary,
                    textAlign = TextAlign.Center,
                )
            }
            vm.qrNotice?.let { notice ->
                Text(notice, style = MuTheme.type.body, textAlign = TextAlign.Center)
            }
            Spacer(Modifier.windowInsetsBottomHeight(WindowInsets.navigationBars))
        }
    }
}

@Composable
private fun QrCode(update: QrLoginUpdate?) {
    val state = update?.state ?: QrLoginState.LOADING
    val matrix = update?.matrix
    // Black modules on a white plate in every theme, or a phone cannot scan it.
    Box(
        Modifier
            .size(232.dp)
            .clip(RoundedCornerShape(12.dp))
            .background(Color.White),
        contentAlignment = Alignment.Center,
    ) {
        if (matrix != null && state != QrLoginState.LOADING) {
            val bitmap = remember(matrix) { matrix.toBitmap() }
            Image(
                bitmap = bitmap.asImageBitmap(),
                contentDescription = stringResource(R.string.accounts_qr_description),
                modifier = Modifier.fillMaxSize(),
                contentScale = ContentScale.Fit,
                filterQuality = FilterQuality.None,
            )
        }
        if (state == QrLoginState.LOADING) {
            MuSpinner(size = 40.dp, color = Color.Black)
        }
        if (QrPresentation.covered(state)) {
            Box(Modifier.fillMaxSize().background(Color.White.copy(alpha = 0.94f)), contentAlignment = Alignment.Center) {
                Icon(qrIcon(state), contentDescription = null, modifier = Modifier.size(48.dp), tint = Color.Black)
            }
        }
    }
}

@Composable
private fun StatusLine(update: QrLoginUpdate?) {
    val state = update?.state ?: QrLoginState.LOADING
    val message = update?.message ?: QrLoginState.LOADING.defaultMessage
    Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(MuSpacing.s)) {
        if (state == QrLoginState.LOADING) {
            MuSpinner(size = 16.dp)
        } else {
            Icon(qrIcon(state), contentDescription = null, modifier = Modifier.size(16.dp), tint = MuTheme.colors.text)
        }
        Text(message, style = MuTheme.type.body, textAlign = TextAlign.Center)
    }
}

private fun qrIcon(state: QrLoginState): ImageVector = when (state) {
    QrLoginState.LOADING, QrLoginState.WAITING -> MuIcons.QrCode
    QrLoginState.SCANNED -> MuIcons.Phone
    QrLoginState.CONFIRMED -> MuIcons.Check
    QrLoginState.EXPIRED -> MuIcons.Refresh
    QrLoginState.FAILED -> MuIcons.Warning
}

private fun needsStoragePermission(context: android.content.Context): Boolean =
    Build.VERSION.SDK_INT <= 28 &&
        ContextCompat.checkSelfPermission(context, Manifest.permission.WRITE_EXTERNAL_STORAGE) != PackageManager.PERMISSION_GRANTED
