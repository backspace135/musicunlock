package io.github.backspace135.musicunlock.core

import okhttp3.OkHttpClient
import okhttp3.Request
import okhttp3.RequestBody.Companion.toRequestBody
import java.io.IOException
import java.util.concurrent.TimeUnit

/** One HTTP request the core makes (music_api.h `HttpRequest`). */
class HttpRequest(val url: String, val method: String, val headers: List<Pair<String, String>>, val body: ByteArray)

/** The response the core reads: status, body and every Set-Cookie header value. */
class HttpResponse(val status: Int, val body: ByteArray, val setCookies: List<String> = emptyList())

/**
 * Performs the core's HTTP requests (music_api.h `HttpTransport`), synchronously on the calling thread. Throw
 * [LyricException] when the network is unreachable. The app uses [OkHttpTransport]; tests pass canned responses.
 */
fun interface HttpTransport {
    fun execute(request: HttpRequest): HttpResponse
}

/**
 * OkHttp with the rules of the Windows WinHTTP transport: HTTPS only, no cookie storage (the core passes cookies
 * explicitly), no cache, 25 s timeouts, redirects followed, bodies cut at 20 MB. The request in flight is cancelled
 * with the [CancelToken] of the core call that made it.
 */
class OkHttpTransport(private val client: OkHttpClient = defaultClient) : HttpTransport {
    override fun execute(request: HttpRequest): HttpResponse {
        if (!request.url.startsWith("https://", ignoreCase = true)) throw LyricException("只支持 HTTPS 请求。")
        val builder = Request.Builder().url(request.url)
        for ((name, value) in request.headers) builder.addHeader(name, value)
        val body = if (request.method == "GET" && request.body.isEmpty()) null else request.body.toRequestBody()
        val call = client.newCall(builder.method(request.method, body).build())
        val token = CancelToken.current.get()
        token?.register(call)
        try {
            call.execute().use { response ->
                val source = response.body.source()
                source.request(READ_LIMIT)
                val bytes = source.buffer.let { buffer -> buffer.readByteArray(minOf(buffer.size, READ_LIMIT)) }
                return HttpResponse(response.code, bytes, response.headers("Set-Cookie"))
            }
        } catch (error: IOException) {
            if (call.isCanceled() || token?.isCancelled == true) throw CoreCancelledException()
            throw LyricException("网络连接失败，请检查网络或代理设置后重试。")
        } finally {
            token?.unregister(call)
        }
    }

    companion object {
        /** One byte more than the core accepts, so it can tell an oversized response. */
        private const val READ_LIMIT = 20_000_001L

        val defaultClient: OkHttpClient by lazy {
            OkHttpClient.Builder()
                .connectTimeout(25, TimeUnit.SECONDS)
                .readTimeout(25, TimeUnit.SECONDS)
                .writeTimeout(25, TimeUnit.SECONDS)
                .build()
        }
    }
}

/** The HTTP result as JNI reads it (api_jni.cpp). Kept by consumer-rules.pro. */
internal class NativeHttpResult(
    @JvmField val status: Int,
    @JvmField val body: ByteArray,
    @JvmField val setCookies: Array<String>,
)

/** Kotlin entry points the C++ core calls back into. Kept by consumer-rules.pro. */
internal object CoreCallbacks {
    @JvmStatic
    fun http(transport: HttpTransport, url: String, method: String, headers: Array<String>, body: ByteArray): NativeHttpResult {
        val pairs = List(headers.size / 2) { headers[it * 2] to headers[it * 2 + 1] }
        val response = transport.execute(HttpRequest(url, method, pairs, body))
        return NativeHttpResult(response.status, response.body, response.setCookies.toTypedArray())
    }

    @JvmStatic
    fun qrReport(listener: QrPollListener, state: Int, message: String) {
        listener.report(QrLoginState.entries[state], message)
    }

    @JvmStatic
    fun seal(cipher: SessionCipher, plain: ByteArray): ByteArray = cipher.seal(plain)

    @JvmStatic
    fun open(cipher: SessionCipher, sealed: ByteArray): ByteArray? = cipher.open(sealed)
}
