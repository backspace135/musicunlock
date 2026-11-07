package io.github.backspace135.musicunlock.ui.accounts

import android.annotation.SuppressLint
import android.content.pm.ApplicationInfo
import android.graphics.Bitmap
import android.os.Handler
import android.os.Looper
import android.webkit.CookieManager
import android.webkit.PermissionRequest
import android.webkit.WebChromeClient
import android.webkit.WebResourceError
import android.webkit.WebResourceRequest
import android.webkit.WebSettings
import android.webkit.WebView
import android.webkit.WebViewClient
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.Icon
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import androidx.compose.ui.viewinterop.AndroidView
import androidx.lifecycle.viewmodel.compose.viewModel
import io.github.backspace135.musicunlock.R
import io.github.backspace135.musicunlock.core.MusicProvider
import io.github.backspace135.musicunlock.ui.kit.MuIcons
import io.github.backspace135.musicunlock.ui.kit.MuSpinner
import io.github.backspace135.musicunlock.ui.kit.MuTopBar
import io.github.backspace135.musicunlock.ui.kit.SecondaryButton
import io.github.backspace135.musicunlock.ui.nav.LocalNavigator
import io.github.backspace135.musicunlock.ui.theme.MuSpacing
import io.github.backspace135.musicunlock.ui.theme.MuTheme

/**
 * Desktop Chrome UA for the QQ page. Null keeps the WebView's own (mobile) UA. Set this only after the
 * on-device check shows the mobile page cannot sign in with a password or a QR code.
 */
internal object WebLoginUserAgent {
    val QQ: String? = null
}

@Composable
fun WebLoginScreen(provider: MusicProvider, relogin: Boolean, vm: WebLoginViewModel = viewModel()) {
    val navigator = LocalNavigator.current
    val context = LocalContext.current
    val netease = provider == MusicProvider.NETEASE
    val url = AccountCookies.loginPage(netease)
    // Expire a re-login's music keys before the WebView's first load, which happens later in this composition.
    remember(provider, relogin) {
        vm.prepare(provider, relogin)
        true
    }
    DisposableEffect(vm) {
        vm.onClose = { navigator.pop() }
        onDispose { vm.onClose = null }
    }
    LaunchedEffect(Unit) {
        val debuggable = context.applicationInfo.flags and ApplicationInfo.FLAG_DEBUGGABLE != 0
        if (debuggable) WebView.setWebContentsDebuggingEnabled(true)
    }

    Column(Modifier.fillMaxSize().background(MuTheme.colors.background)) {
        MuTopBar(
            title = stringResource(R.string.accounts_web_title_format, provider.title),
            onBack = {
                vm.cancel()
                navigator.pop()
            },
        )
        Row(
            Modifier.fillMaxWidth().padding(horizontal = MuSpacing.page, vertical = MuSpacing.xs),
            horizontalArrangement = Arrangement.spacedBy(MuSpacing.s),
        ) {
            SecondaryButton(
                stringResource(R.string.accounts_web_refresh_action),
                onClick = vm::refresh,
                small = true,
                modifier = Modifier.weight(1f),
            )
            if (netease) {
                SecondaryButton(
                    stringResource(R.string.accounts_web_native_action),
                    onClick = {
                        vm.cancel()
                        AccountFeedback.openQr = true
                        navigator.pop()
                    },
                    small = true,
                    modifier = Modifier.weight(1f),
                )
            }
        }
        Text(
            stringResource(R.string.accounts_web_hint),
            style = MuTheme.type.caption,
            color = MuTheme.colors.secondary,
            modifier = Modifier.padding(horizontal = MuSpacing.page, vertical = MuSpacing.xs),
        )
        Box(Modifier.fillMaxSize()) {
            LoginWebView(url, provider, vm, Modifier.fillMaxSize())
            if (!vm.pageVisible) {
                Column(
                    Modifier.fillMaxSize().background(MuTheme.colors.background).padding(MuSpacing.xl),
                    horizontalAlignment = Alignment.CenterHorizontally,
                    verticalArrangement = Arrangement.spacedBy(MuSpacing.m, Alignment.CenterVertically),
                ) {
                    if (vm.failed) {
                        Icon(MuIcons.Warning, contentDescription = null, tint = MuTheme.colors.text)
                        Text(
                            stringResource(R.string.accounts_web_failed),
                            style = MuTheme.type.body,
                            textAlign = TextAlign.Center,
                        )
                    } else {
                        MuSpinner(size = 32.dp)
                        Text(stringResource(R.string.accounts_web_loading), style = MuTheme.type.body, textAlign = TextAlign.Center)
                    }
                }
            }
        }
    }
}

@SuppressLint("SetJavaScriptEnabled")
@Composable
private fun LoginWebView(url: String, provider: MusicProvider, vm: WebLoginViewModel, modifier: Modifier) {
    var appliedReload by remember { mutableIntStateOf(-1) }
    AndroidView(
        modifier = modifier,
        factory = { context ->
            WebView(context).apply {
                val client = LoginWebClient(vm)
                webViewClient = client
                webChromeClient = object : WebChromeClient() {
                    override fun onPermissionRequest(request: PermissionRequest) {
                        request.deny()
                    }

                    override fun onCreateWindow(view: WebView?, isDialog: Boolean, isUserGesture: Boolean, resultMsg: android.os.Message?): Boolean =
                        false
                }
                settings.javaScriptEnabled = true
                settings.domStorageEnabled = true
                settings.allowFileAccess = false
                settings.allowContentAccess = false
                settings.setSupportMultipleWindows(false)
                @Suppress("DEPRECATION")
                settings.saveFormData = false
                settings.javaScriptCanOpenWindowsAutomatically = false
                settings.setGeolocationEnabled(false)
                settings.mixedContentMode = WebSettings.MIXED_CONTENT_NEVER_ALLOW
                if (provider == MusicProvider.QQ) WebLoginUserAgent.QQ?.let { settings.userAgentString = it }
                CookieManager.getInstance().setAcceptCookie(true)
                // Third-party cookies stay off; the login page's own host is enough for the session we read.
                CookieManager.getInstance().setAcceptThirdPartyCookies(this, false)
            }
        },
        update = { web ->
            if (appliedReload != vm.reload) {
                appliedReload = vm.reload
                (web.webViewClient as LoginWebClient).navigationToken = vm.loadToken
                web.loadUrl(url)
            }
        },
        onRelease = { web ->
            // Never destroy the WebView on the callback's stack. Drop its clients first, then destroy next turn.
            web.stopLoading()
            web.webChromeClient = null
            web.webViewClient = WebViewClient()
            Handler(Looper.getMainLooper()).post { web.destroy() }
        },
    )
}

private class LoginWebClient(private val vm: WebLoginViewModel) : WebViewClient() {
    var navigationToken: Int = 0
    private var startedToken: Int = 0

    override fun shouldOverrideUrlLoading(view: WebView, request: WebResourceRequest): Boolean {
        return !WebLoginPolicy.allows(request.url?.toString().orEmpty())
    }

    override fun onPageStarted(view: WebView, url: String?, favicon: Bitmap?) {
        startedToken = navigationToken
    }

    override fun onPageFinished(view: WebView, url: String?) {
        vm.pageFinished(startedToken)
    }

    override fun onReceivedError(view: WebView, request: WebResourceRequest, error: WebResourceError) {
        if (!request.isForMainFrame) return
        vm.mainFrameError(startedToken, error.errorCode, error.description)
    }
}
