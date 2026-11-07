package io.github.backspace135.musicunlock.ui.accounts

import android.webkit.CookieManager
import android.webkit.WebStorage
import io.github.backspace135.musicunlock.core.AccountStore
import io.github.backspace135.musicunlock.core.Cookie
import io.github.backspace135.musicunlock.core.MusicProvider

/**
 * WebView cookies for one platform. Never logs a cookie value: only names are copied out of the header, and only
 * into an expired `Set-Cookie`. Does not call [CookieManager.removeAllCookies], which would sign the other
 * platform out too.
 */
internal object WebSession {
    fun read(provider: MusicProvider): List<Cookie> {
        val netease = provider == MusicProvider.NETEASE
        val header = runCatching { CookieManager.getInstance().getCookie(AccountCookies.readUrl(netease)) }.getOrNull()
        return AccountStore.cookiesFromCookieManager(AccountCookies.host(netease), header)
    }

    /** Drops the music-key cookies before a re-login so the platform has to issue a new session. */
    fun expireMusicKeys(provider: MusicProvider) {
        val netease = provider == MusicProvider.NETEASE
        expire(netease, AccountCookies.musicKeys(netease))
    }

    /** Logout: expire every cookie visible to this platform, then its WebStorage. */
    fun clearPlatform(provider: MusicProvider) {
        val netease = provider == MusicProvider.NETEASE
        val header = runCatching { CookieManager.getInstance().getCookie(AccountCookies.readUrl(netease)) }.getOrNull()
        val names = (AccountCookies.names(header) + AccountCookies.musicKeys(netease)).distinct()
        expire(netease, names)
        val storage = WebStorage.getInstance()
        for (origin in AccountCookies.origins(netease)) storage.deleteOrigin(origin)
        runCatching { CookieManager.getInstance().flush() }
    }

    private fun expire(netease: Boolean, names: List<String>) {
        val manager = CookieManager.getInstance()
        val url = AccountCookies.readUrl(netease)
        for (name in names) {
            manager.setCookie(url, "$name=; Max-Age=0; Path=/")
            for (domain in AccountCookies.domains(netease)) {
                manager.setCookie(url, AccountCookies.expired(name, domain))
            }
        }
        manager.flush()
    }
}
