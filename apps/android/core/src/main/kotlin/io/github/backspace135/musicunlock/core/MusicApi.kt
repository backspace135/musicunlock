package io.github.backspace135.musicunlock.core

import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import org.json.JSONArray
import org.json.JSONObject
import java.io.File

/**
 * The core's first-party NetEase / QQ Music requests (music_api.h `MusicApi`, and the background half of
 * lyric_workspace.h). Every function is a `suspend` function that runs on [Dispatchers.IO], takes an optional
 * [CancelToken], and throws [LyricException] with the message to show, or [CoreCancelledException] when cancelled.
 * Thread-safe; use [shared] unless a test needs another transport.
 *
 * [cookie] is the "Cookie" header of the platform's login: `AccountStore.cookieHeader(provider)`, empty when not
 * signed in (public content still works).
 */
class MusicApi(transport: HttpTransport = OkHttpTransport()) : AutoCloseable {
    private var handle = nativeCreate(transport)

    /** Runs a [SearchRequest] from [LyricWorkspace]: a search, the next page, or a link / album / playlist. */
    suspend fun runSearch(request: SearchRequest, cookie: String, cancel: CancelToken = CancelToken()): SearchResult =
        blockingCall(cancel) { token ->
            ModelJson.searchResult(JSONObject(nativeRunSearch(checked(), ModelJson.json(request).toString(), cookie, token)))
        }

    /** The lyrics of one song (for a [LyricsRequest]). */
    suspend fun lyrics(song: LyricSong, cookie: String, cancel: CancelToken = CancelToken()): LyricDocument =
        blockingCall(cancel) { token ->
            ModelJson.document(JSONObject(nativeLyrics(checked(), ModelJson.json(song).toString(), cookie, token)))
        }

    /**
     * Fetches (unless [job] has a document), renders and writes one song of an [ExportBatch] to [folder], never
     * overwriting: names collide into " (n)" like the core's save_lyrics. [ExportOutcome.file] is null when the song
     * was skipped as having no lyrics. Pass the result to [LyricWorkspace.exportSucceeded].
     */
    suspend fun exportSong(job: ExportJob, options: LyricOptions, folder: OutputFolder, cookie: String,
                           cancel: CancelToken = CancelToken()): ExportOutcome = withContext(Dispatchers.IO) {
        CacheFolders.withStaging { staging ->
            val result = blockingCall(cancel) { token ->
                JSONObject(nativeExportSong(checked(), ModelJson.json(job).toString(), options.toJson(), staging.path,
                    cookie, token))
            }
            val document = ModelJson.document(result.getJSONObject("document"))
            val path = if (result.isNull("file")) null else File(result.getString("file"))
            val file = path?.let {
                val extension = options.format.extension
                val published = FilePublisher.publish(it, it.name.removeSuffix(".$extension"), extension, folder,
                    TOO_MANY_NAMES)
                ExportedFile(published.uri, published.name, published.location)
            }
            ExportOutcome(document, file)
        }
    }

    /** One page of a keyword search (the workspace normally does this through [runSearch]). */
    suspend fun search(query: String, provider: MusicProvider, kind: SearchKind, page: Int, cookie: String,
                       cancel: CancelToken = CancelToken()): SearchResult = blockingCall(cancel) { token ->
        ModelJson.searchResult(JSONObject(nativeSearch(checked(), query, provider.ordinal, kind.ordinal, page, cookie, token)))
    }

    /** The songs behind a link (song, album or playlist). */
    suspend fun resolve(link: MusicLink, cookie: String, cancel: CancelToken = CancelToken()): List<LyricSong> =
        blockingCall(cancel) { token ->
            ModelJson.list(JSONArray(nativeResolve(checked(), ModelJson.json(link).toString(), cookie, token)), ModelJson::song)
        }

    /** The account's nickname; null when the platform returns none (the session may have expired). */
    suspend fun accountName(provider: MusicProvider, cookie: String, cancel: CancelToken = CancelToken()): String? =
        blockingCall(cancel) { nativeAccountName(checked(), provider.ordinal, cookie) }

    /** A new NetEase QR login (normally through [QrLogin]). Throws [LyricException] when NetEase refuses it. */
    suspend fun createQrCode(cancel: CancelToken = CancelToken()): QrTicket = blockingCall(cancel) {
        JSONObject(nativeCreateQrCode(checked())).let { QrTicket(it.getString("key"), it.getString("url"), it.getString("device")) }
    }

    /** One status query of a QR login: the code (800 expired, 801 waiting, 802 scanned, 803 confirmed) and cookies. */
    suspend fun checkQrCode(ticket: QrTicket, cancel: CancelToken = CancelToken()): QrStatus = blockingCall(cancel) {
        val status = JSONObject(nativeCheckQrCode(checked(), ticket.json()))
        QrStatus(status.getInt("code"), Cookie.list(status.getJSONArray("cookies")))
    }

    internal fun pollQrLogin(ticket: QrTicket, token: Long, intervalMillis: Long, attempts: Int,
                             listener: QrPollListener): JSONObject =
        JSONObject(nativePollQrLogin(checked(), ticket.json(), token, intervalMillis, attempts, listener))

    /** [accountName] on a thread started by C++ (tests of attaching native threads to the JVM). */
    internal fun accountNameOnNativeThread(provider: MusicProvider, cookie: String): String? =
        nativeAccountNameOnNativeThread(checked(), provider.ordinal, cookie)

    override fun close() {
        if (handle == 0L || this === shared) return
        nativeDestroy(handle)
        handle = 0
    }

    private fun checked(): Long = handle.also { check(it != 0L) { "MusicApi 已关闭" } }

    private fun QrTicket.json() = JSONObject().put("key", key).put("url", url).put("device", device).toString()

    companion object {
        private const val TOO_MANY_NAMES = "同名文件过多，请更换文件名或输出文件夹。"

        init {
            MusicUnlockCore.load()
        }

        /** The app's instance, over [OkHttpTransport]. */
        val shared: MusicApi by lazy { MusicApi() }

        /** QQ's g_tk: DJB hash of the session key. */
        fun qqToken(cookie: String): Long = nativeQqToken(cookie)

        /** Decodes HTML entities the platforms put in titles. */
        fun decodeEntities(text: String): String = nativeDecodeEntities(text)

        @JvmStatic private external fun nativeCreate(transport: HttpTransport): Long
        @JvmStatic private external fun nativeDestroy(api: Long)
        @JvmStatic private external fun nativeSearch(api: Long, query: String, provider: Int, kind: Int, page: Int,
                                                     cookie: String, token: Long): String
        @JvmStatic private external fun nativeResolve(api: Long, link: String, cookie: String, token: Long): String
        @JvmStatic private external fun nativeLyrics(api: Long, song: String, cookie: String, token: Long): String
        @JvmStatic private external fun nativeRunSearch(api: Long, request: String, cookie: String, token: Long): String
        @JvmStatic private external fun nativeExportSong(api: Long, job: String, options: String, directory: String,
                                                         cookie: String, token: Long): String
        @JvmStatic private external fun nativeCreateQrCode(api: Long): String
        @JvmStatic private external fun nativeCheckQrCode(api: Long, ticket: String): String
        @JvmStatic private external fun nativePollQrLogin(api: Long, ticket: String, token: Long, intervalMillis: Long,
                                                          attempts: Int, listener: QrPollListener): String
        @JvmStatic private external fun nativeAccountName(api: Long, provider: Int, cookie: String): String?
        @JvmStatic private external fun nativeAccountNameOnNativeThread(api: Long, provider: Int, cookie: String): String?
        @JvmStatic private external fun nativeQqToken(cookie: String): Long
        @JvmStatic private external fun nativeDecodeEntities(text: String): String
    }
}

/** A NetEase QR login: [url] is the code's content; [device] is sent with each status query. */
data class QrTicket(val key: String, val url: String, val device: String)

/** One status query's answer. */
data class QrStatus(val code: Int, val cookies: List<Cookie>)
