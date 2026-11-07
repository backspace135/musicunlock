package io.github.backspace135.musicunlock.core

import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import org.json.JSONArray
import org.json.JSONObject
import java.io.File

/** A cookie of a platform login (session.h `Cookie`). [expires] is Unix seconds, null for a session cookie. */
data class Cookie(
    val name: String,
    val value: String,
    val domain: String,
    val path: String = "/",
    val expires: Long? = null,
    val secure: Boolean = false,
) {
    /** Never print the value: sessions must not reach logs. */
    override fun toString(): String = "Cookie(name=$name, domain=$domain)"

    internal companion object {
        fun list(array: JSONArray): List<Cookie> = List(array.length()) {
            val o = array.getJSONObject(it)
            Cookie(o.getString("name"), o.getString("value"), o.getString("domain"), o.getString("path"),
                if (o.isNull("expires")) null else o.getLong("expires"), o.getBoolean("secure"))
        }

        fun json(cookies: List<Cookie>): String = JSONArray(cookies.map {
            JSONObject().put("name", it.name).put("value", it.value).put("domain", it.domain).put("path", it.path)
                .put("expires", it.expires ?: JSONObject.NULL).put("secure", it.secure)
        }).toString()
    }
}

/**
 * Saved music platform logins (Windows `Accounts.cpp`, macOS `MusicAccounts.swift`): the sessions in memory plus
 * their encrypted files in the app's private `files/sessions/<provider>.bin` ([KeystoreSessionCipher]). The lyric
 * requests read [cookieHeader] from here. Thread-safe; quick except [nickname].
 */
object AccountStore {
    private val lock = Any()
    private val sessions = mutableMapOf<MusicProvider, List<Cookie>>()  // guarded by lock
    private var loaded = false  // guarded by lock
    private val state = MutableStateFlow(MusicProvider.entries.associateWith { false })

    /** Overridable for tests. */
    @Volatile
    internal var cipher: SessionCipher = KeystoreSessionCipher()

    @Volatile
    internal var directory: File? = null

    private val folder: String get() = (directory ?: File(MusicUnlockCore.context.filesDir, "sessions")).path

    /**
     * Whether each platform has a session; collect it so the lyrics toolbar and the accounts page update right after a
     * login or logout. Call [refresh] to re-check expiry.
     */
    val signedInState: StateFlow<Map<MusicProvider, Boolean>>
        get() {
            synchronized(lock) { loadSaved() }
            return state.asStateFlow()
        }

    /** True while the platform has a session whose cookies have not expired (the platform may still end it earlier). */
    fun signedIn(provider: MusicProvider): Boolean = synchronized(lock) { current(provider) != null }

    /** "name=value; ..." of the platform's session for the core's requests; empty when not signed in. */
    fun cookieHeader(provider: MusicProvider): String = synchronized(lock) {
        current(provider)?.let { Sessions.nativeCookieHeader(Cookie.json(it)) }.orEmpty()
    }

    /**
     * Keeps [received] when it forms a music session (NetEase MUSIC_U, or a QQ music key plus an account id) and
     * saves it encrypted. Returns the status line for the user, or null when the cookies are no session and nothing
     * changed ("平台没有返回有效登录…" is then up to the caller, as on Windows).
     */
    fun accept(provider: MusicProvider, received: List<Cookie>): String? {
        val accepted = Sessions.nativeAccept(Cookie.json(received), provider.ordinal) ?: return null
        val title = provider.title
        val line = synchronized(lock) {
            loadSaved()
            sessions[provider] = Cookie.list(JSONArray(accepted))
            try {
                Sessions.nativeSave(folder, provider.ordinal, accepted, cipher)
                "${title}登录成功，会话已加密保存在这台手机上。"
            } catch (error: LyricException) {
                "已登录$title，本次可用；保存登录失败：${error.message}"
            } catch (_: Exception) {
                "已登录$title，本次可用；保存登录失败。"
            }
        }
        publish()
        return line
    }

    /**
     * Forgets the session and deletes its file. Returns the status line. Clearing the platform's WebView cookies and
     * storage is the caller's part (#33).
     */
    fun logout(provider: MusicProvider): String {
        val removed = synchronized(lock) {
            loadSaved()
            sessions.remove(provider)
            Sessions.nativeRemove(folder, provider.ordinal, cipher)
        }
        publish()
        return if (removed) "已退出${provider.title}。" else "已退出本次会话；会话文件删除失败。"
    }

    /**
     * The account line of the accounts page, as on Windows: "账号：{昵称}" (QQ: "账号：QQ 音乐用户"), or one of
     * "平台没有返回账号信息…", "无法读取账号信息：{错误}", "暂时无法读取账号信息，请检查网络。会话仍已保存。".
     * Makes a request; cancel the calling coroutine to drop it.
     */
    suspend fun nickname(provider: MusicProvider, api: MusicApi = MusicApi.shared): String = try {
        val name = api.accountName(provider, cookieHeader(provider))
        if (name != null) "账号：$name" else "平台没有返回账号信息，会话可能已过期，可以重新登录。"
    } catch (error: CoreCancelledException) {
        throw error
    } catch (error: LyricException) {
        "无法读取账号信息：${error.message}"
    } catch (_: Exception) {
        "暂时无法读取账号信息，请检查网络。会话仍已保存。"
    }

    /**
     * Cookies from `CookieManager.getInstance().getCookie(url)` ("a=1; b=2", without domains or expiry): each pair
     * becomes a cookie of [host] ("music.163.com" or "y.qq.com"), path "/", no expiry. Pass them to [accept].
     */
    fun cookiesFromCookieManager(host: String, cookieString: String?): List<Cookie> =
        if (cookieString.isNullOrBlank()) emptyList()
        else Cookie.list(JSONArray(Sessions.nativeCookiesFromHeader(cookieString, host)))

    /** Parses Set-Cookie header values; cookies without Domain belong to [requestHost]. */
    fun parseSetCookies(headers: List<String>, requestHost: String): List<Cookie> =
        Cookie.list(JSONArray(Sessions.nativeParseSetCookies(headers.toTypedArray(), requestHost)))

    /** Whether [cookies] form a music session of [provider] now. */
    fun isMusicSession(provider: MusicProvider, cookies: List<Cookie>): Boolean =
        Sessions.nativeHasSession(Cookie.json(cookies), provider.ordinal)

    /** Re-reads expiry into [signedInState]. */
    fun refresh() = publish()

    /** Drops the in-memory sessions so the next call reads the files again (tests). */
    internal fun reload() {
        synchronized(lock) {
            sessions.clear()
            loaded = false
        }
        publish()
    }

    private fun loadSaved() {
        if (loaded) return
        loaded = true
        for (provider in MusicProvider.entries) {
            Sessions.nativeLoad(folder, provider.ordinal, cipher)?.let { sessions[provider] = Cookie.list(JSONArray(it)) }
        }
    }

    private fun current(provider: MusicProvider): List<Cookie>? {
        loadSaved()
        return sessions[provider]?.takeIf { Sessions.nativeHasSession(Cookie.json(it), provider.ordinal) }
    }

    private fun publish() {
        state.value = MusicProvider.entries.associateWith { signedIn(it) }
    }
}

/** session.h for AccountStore (session_jni.cpp). */
internal object Sessions {
    init {
        MusicUnlockCore.load()
    }

    @JvmStatic external fun nativeLoad(directory: String, provider: Int, cipher: SessionCipher): String?
    @JvmStatic external fun nativeSave(directory: String, provider: Int, cookies: String, cipher: SessionCipher)
    @JvmStatic external fun nativeRemove(directory: String, provider: Int, cipher: SessionCipher): Boolean
    @JvmStatic external fun nativeFile(directory: String, provider: Int, cipher: SessionCipher): String
    @JvmStatic external fun nativeAccept(cookies: String, provider: Int): String?
    @JvmStatic external fun nativeHasSession(cookies: String, provider: Int): Boolean
    @JvmStatic external fun nativeCookieHeader(cookies: String): String
    @JvmStatic external fun nativeParseSetCookies(headers: Array<String>, host: String): String
    @JvmStatic external fun nativeCookiesFromHeader(header: String, host: String): String
    @JvmStatic external fun nativeUnixNow(): Long
}
