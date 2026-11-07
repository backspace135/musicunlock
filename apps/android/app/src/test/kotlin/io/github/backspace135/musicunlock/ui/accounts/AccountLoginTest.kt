package io.github.backspace135.musicunlock.ui.accounts

import io.github.backspace135.musicunlock.core.QrLoginState
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class AccountLoginTest {
    @Test
    fun generationDropsResultsFromAReplacedAttempt() {
        val generation = LoginGeneration()
        val first = generation.next()
        assertTrue(generation.isCurrent(first))
        val second = generation.next()
        assertFalse(generation.isCurrent(first))
        assertTrue(generation.isCurrent(second))
        assertTrue(second > first)
    }

    @Test
    fun sessionNeedsTwoConsecutiveValidReads() {
        val gate = SessionReadGate()
        assertFalse(gate.observe(true))
        assertTrue(gate.observe(true))
        assertEquals(2, gate.streak)
        assertFalse(gate.observe(false))
        assertEquals(0, gate.streak)
        assertFalse(gate.observe(true))
        gate.reset()
        assertEquals(0, gate.streak)
    }

    @Test
    fun webLoginAllowsOnlyHttpsAndAbout() {
        assertTrue(WebLoginPolicy.allows("https://y.qq.com/portal/profile.html"))
        assertTrue(WebLoginPolicy.allows("HTTPS://music.163.com/#/login"))
        assertTrue(WebLoginPolicy.allows("about:blank"))
        assertFalse(WebLoginPolicy.allows("http://y.qq.com/"))
        assertFalse(WebLoginPolicy.allows("weixin://dl/"))
        assertFalse(WebLoginPolicy.allows("file:///sdcard/a.html"))
        assertFalse(WebLoginPolicy.allows("intent://scan/#Intent;end"))
        assertFalse(WebLoginPolicy.allows("javascript:void(0)"))
    }

    @Test
    fun abortedLoadsAreCancelledAndOtherErrorsAreNot() {
        assertTrue(WebLoginPolicy.isCancelledLoad(-3, "net::ERR_ABORTED"))
        assertTrue(WebLoginPolicy.isCancelledLoad(-1, "net::ERR_ABORTED"))
        assertFalse(WebLoginPolicy.isCancelledLoad(-2, "net::ERR_NAME_NOT_RESOLVED"))
        assertFalse(WebLoginPolicy.isCancelledLoad(-6, null))
    }

    @Test
    fun cookieNamesNeverIncludeValues() {
        assertEquals(listOf("MUSIC_U", "os"), AccountCookies.names("MUSIC_U=secret-value; os=pc"))
        assertEquals(emptyList<String>(), AccountCookies.names(null))
        assertEquals(emptyList<String>(), AccountCookies.names("   "))
        assertEquals(listOf("ok_1"), AccountCookies.names("not a name=1; ok_1=2"))
        assertFalse(AccountCookies.names("qqmusic_key=hidden").any { it.contains("hidden") })
    }

    @Test
    fun expiredCookieMatchesThePlatformRule() {
        assertEquals(
            "MUSIC_U=; Max-Age=0; Path=/; Domain=.music.163.com",
            AccountCookies.expired("MUSIC_U", ".music.163.com"),
        )
        assertEquals(listOf("MUSIC_U"), AccountCookies.musicKeys(netease = true))
        assertEquals(listOf("qqmusic_key", "qm_keyst", "qqmusic_session"), AccountCookies.musicKeys(netease = false))
        assertTrue(AccountCookies.domains(netease = true).contains(".163.com"))
        assertTrue(AccountCookies.domains(netease = false).contains(".qq.com"))
    }

    @Test
    fun qrControlsFollowTheWindowsStates() {
        assertFalse(QrPresentation.covered(QrLoginState.LOADING))
        assertFalse(QrPresentation.covered(QrLoginState.WAITING))
        assertTrue(QrPresentation.covered(QrLoginState.SCANNED))
        assertTrue(QrPresentation.covered(QrLoginState.CONFIRMED))
        assertTrue(QrPresentation.covered(QrLoginState.EXPIRED))
        assertTrue(QrPresentation.covered(QrLoginState.FAILED))

        assertFalse(QrPresentation.refreshEnabled(QrLoginState.LOADING))
        assertTrue(QrPresentation.refreshEnabled(QrLoginState.WAITING))
        assertTrue(QrPresentation.refreshEnabled(QrLoginState.EXPIRED))
        assertFalse(QrPresentation.refreshEnabled(QrLoginState.CONFIRMED))

        assertFalse(QrPresentation.regenerate(QrLoginState.WAITING))
        assertTrue(QrPresentation.regenerate(QrLoginState.EXPIRED))
        assertTrue(QrPresentation.regenerate(QrLoginState.FAILED))
    }
}
