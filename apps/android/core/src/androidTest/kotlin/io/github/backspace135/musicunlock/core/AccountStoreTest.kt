package io.github.backspace135.musicunlock.core

import androidx.test.ext.junit.runners.AndroidJUnit4
import kotlinx.coroutines.runBlocking
import org.junit.After
import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith
import java.io.File

/** The Keystore session cipher and AccountStore over the core's session rules. */
@RunWith(AndroidJUnit4::class)
class AccountStoreTest {
    private val folder = File(Fixtures.context.cacheDir, "test-sessions")
    private val cipher = KeystoreSessionCipher("musicunlock-test-session")

    @Before
    fun isolate() {
        folder.deleteRecursively()
        AccountStore.directory = folder
        AccountStore.cipher = cipher
        AccountStore.reload()
    }

    @After
    fun restore() {
        folder.deleteRecursively()
        cipher.deleteKey()
        AccountStore.directory = null
        AccountStore.cipher = KeystoreSessionCipher()
        AccountStore.reload()
    }

    @Test
    fun cipherRoundTripsAndRejectsTampering() {
        val plain = "[{\"name\":\"MUSIC_U\"}]".toByteArray()
        val sealed = cipher.seal(plain)
        assertArrayEquals(plain, cipher.open(sealed))
        assertFalse(sealed.decodeToString().contains("MUSIC_U"))
        assertTrue("random IV", !cipher.seal(plain).contentEquals(sealed))
        val tampered = sealed.copyOf().also { it[it.size - 1] = (it[it.size - 1].toInt() xor 1).toByte() }
        assertNull(cipher.open(tampered))
        assertNull(cipher.open(ByteArray(3)))
        assertNull(KeystoreSessionCipher("musicunlock-test-other").apply { seal(plain) }.open(sealed).also {
            KeystoreSessionCipher("musicunlock-test-other").deleteKey()
        })
    }

    @Test
    fun acceptSaveReloadAndLogout() {
        val now = System.currentTimeMillis() / 1000
        val received = listOf(Cookie("MUSIC_U", "secret", ".music.163.com", expires = now + 3600), Cookie("x", "1", ".other.com"))
        assertNull(AccountStore.accept(MusicProvider.NETEASE, listOf(Cookie("x", "1", ".music.163.com"))))
        assertEquals("网易云音乐登录成功，会话已加密保存在这台手机上。", AccountStore.accept(MusicProvider.NETEASE, received))
        assertTrue(AccountStore.signedIn(MusicProvider.NETEASE))
        assertFalse(AccountStore.signedIn(MusicProvider.QQ))
        assertEquals("MUSIC_U=secret", AccountStore.cookieHeader(MusicProvider.NETEASE))
        assertEquals(mapOf(MusicProvider.NETEASE to true, MusicProvider.QQ to false), AccountStore.signedInState.value)
        val file = File(folder, "netease.bin")
        assertTrue(file.isFile)
        assertFalse("saved encrypted", file.readBytes().decodeToString().contains("secret"))

        AccountStore.reload()
        assertEquals("MUSIC_U=secret", AccountStore.cookieHeader(MusicProvider.NETEASE))

        assertEquals("已退出网易云音乐。", AccountStore.logout(MusicProvider.NETEASE))
        assertFalse(file.exists())
        assertEquals("", AccountStore.cookieHeader(MusicProvider.NETEASE))
        assertEquals(false, AccountStore.signedInState.value[MusicProvider.NETEASE])
    }

    @Test
    fun webViewCookiesBecomeASession() {
        val cookies = AccountStore.cookiesFromCookieManager("y.qq.com", "uin=123; qqmusic_key=Q_H_L_abc; pgv=1")
        assertEquals(listOf("uin", "qqmusic_key", "pgv"), cookies.map { it.name })
        assertTrue(cookies.all { it.domain == "y.qq.com" && it.path == "/" && it.expires == null })
        assertTrue(AccountStore.isMusicSession(MusicProvider.QQ, cookies))
        assertEquals("QQ 音乐登录成功，会话已加密保存在这台手机上。", AccountStore.accept(MusicProvider.QQ, cookies))
        assertTrue(AccountStore.cookieHeader(MusicProvider.QQ).contains("qqmusic_key=Q_H_L_abc"))
        assertTrue(AccountStore.cookiesFromCookieManager("y.qq.com", null).isEmpty())
        val parsed = AccountStore.parseSetCookies(listOf("MUSIC_U=v; Domain=music.163.com; Max-Age=60"), "music.163.com")
        assertEquals(".music.163.com", parsed.single().domain)
        assertFalse("values stay out of logs", parsed.single().toString().contains("v)"))
    }

    @Test
    fun nicknameLinesMatchWindows() = runBlocking {
        val now = System.currentTimeMillis() / 1000
        AccountStore.accept(MusicProvider.NETEASE, listOf(Cookie("MUSIC_U", "s", ".music.163.com", expires = now + 3600)))
        val named = MusicApi(ScriptedTransport("/api/w/nuser/account/get" to {
            ScriptedTransport.json("""{"code":200,"profile":{"nickname":"小明"}}""")
        }))
        assertEquals("账号：小明", AccountStore.nickname(MusicProvider.NETEASE, named))
        val empty = MusicApi(ScriptedTransport("/api/w/nuser/account/get" to { ScriptedTransport.json("""{"code":200}""") }))
        assertEquals("平台没有返回账号信息，会话可能已过期，可以重新登录。", AccountStore.nickname(MusicProvider.NETEASE, empty))
        val offline = MusicApi(ScriptedTransport())
        assertEquals("无法读取账号信息：网络连接失败，请检查网络或代理设置后重试。", AccountStore.nickname(MusicProvider.NETEASE, offline))
        listOf(named, empty, offline).forEach { it.close() }
    }
}
