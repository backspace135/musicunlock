package io.github.backspace135.musicunlock.ui.accounts

import io.github.backspace135.musicunlock.core.QrLoginState

/**
 * Bumped whenever a login attempt is replaced or cancelled (refresh, close, switch page). A result may be
 * applied only while [isCurrent] is still true for the token [next] returned when that attempt started.
 */
internal class LoginGeneration {
    var current: Int = 0
        private set

    fun next(): Int {
        current += 1
        return current
    }

    fun isCurrent(token: Int): Boolean = token == current
}

/**
 * Web login accepts a session only after two consecutive valid reads. One invalid read starts the count over:
 * the music key and the account id often arrive in different responses (Windows `session_seen`).
 */
internal class SessionReadGate {
    var streak: Int = 0
        private set

    /** @return true when this read is at least the second consecutive valid one. */
    fun observe(valid: Boolean): Boolean {
        streak = if (valid) streak + 1 else 0
        return streak >= 2
    }

    fun reset() {
        streak = 0
    }
}

internal object WebLoginPolicy {
    /** Only https and about may load. Everything else (custom schemes, http, files) is cancelled. */
    fun allows(url: String): Boolean {
        val scheme = url.substringBefore(':', missingDelimiterValue = "").lowercase()
        return scheme == "https" || scheme == "about"
    }

    /**
     * A load aborted by refresh, back, or a new navigation. Chromium reports it as net error -3
     * (`ERR_ABORTED`); some WebView versions also put that text in the description.
     */
    fun isCancelledLoad(errorCode: Int, description: CharSequence?): Boolean {
        if (errorCode == NET_ERR_ABORTED) return true
        return description?.contains("ERR_ABORTED", ignoreCase = true) == true
    }

    private const val NET_ERR_ABORTED = -3
}

/** Cookie names and domains for one platform. Values are never formatted into these strings. */
internal object AccountCookies {
    fun domains(netease: Boolean): List<String> = if (netease) {
        listOf("music.163.com", ".music.163.com", ".163.com")
    } else {
        listOf("y.qq.com", ".y.qq.com", ".qq.com")
    }

    fun musicKeys(netease: Boolean): List<String> =
        if (netease) listOf("MUSIC_U") else listOf("qqmusic_key", "qm_keyst", "qqmusic_session")

    fun readUrl(netease: Boolean): String = if (netease) "https://music.163.com/" else "https://y.qq.com/"

    fun host(netease: Boolean): String = if (netease) "music.163.com" else "y.qq.com"

    fun loginPage(netease: Boolean): String =
        if (netease) "https://music.163.com/#/login" else "https://y.qq.com/portal/profile.html"

    /** Origins whose WebStorage belongs to this platform's login page. Our WebView only. */
    fun origins(netease: Boolean): List<String> = if (netease) {
        listOf("https://music.163.com")
    } else {
        listOf("https://y.qq.com", "https://ssl.ptlogin2.qq.com", "https://xui.ptlogin2.qq.com", "https://graph.qq.com")
    }

    fun expired(name: String, domain: String): String = "$name=; Max-Age=0; Path=/; Domain=$domain"

    /**
     * Cookie names from `CookieManager.getCookie` (`a=1; b=2`). Names that are not a single token are dropped
     * so a value can never be written back into a `Set-Cookie`. The values themselves are not returned.
     */
    fun names(header: String?): List<String> {
        if (header.isNullOrBlank()) return emptyList()
        return header.split(';').mapNotNull { part ->
            val name = part.substringBefore('=').trim()
            name.takeIf { NAME.matches(it) }
        }.distinct()
    }

    private val NAME = Regex("[A-Za-z0-9_]+")
}

internal object QrPresentation {
    /** White cover and a status icon. Hidden while the code is loading or waiting to be scanned. */
    fun covered(state: QrLoginState): Boolean = state != QrLoginState.LOADING && state != QrLoginState.WAITING

    fun refreshEnabled(state: QrLoginState): Boolean = state != QrLoginState.LOADING && state != QrLoginState.CONFIRMED

    fun regenerate(state: QrLoginState): Boolean = state == QrLoginState.EXPIRED || state == QrLoginState.FAILED
}
