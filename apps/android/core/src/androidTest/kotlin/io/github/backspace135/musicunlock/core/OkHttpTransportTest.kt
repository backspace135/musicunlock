package io.github.backspace135.musicunlock.core

import android.os.Build
import androidx.test.ext.junit.runners.AndroidJUnit4
import kotlinx.coroutines.async
import kotlinx.coroutines.delay
import kotlinx.coroutines.runBlocking
import kotlinx.coroutines.withTimeout
import mockwebserver3.MockResponse
import mockwebserver3.MockWebServer
import okhttp3.OkHttpClient
import okhttp3.tls.HandshakeCertificates
import okhttp3.tls.HeldCertificate
import org.junit.After
import org.junit.Assert.assertEquals
import org.junit.Assert.assertThrows
import org.junit.Assert.assertTrue
import org.junit.Assume.assumeTrue
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith
import java.net.InetAddress
import java.util.concurrent.TimeUnit

/** OkHttpTransport against an HTTPS test server: headers, cookies, and cancelling a request in flight. */
@RunWith(AndroidJUnit4::class)
class OkHttpTransportTest {
    private val server = MockWebServer()
    private lateinit var client: OkHttpClient

    @Before
    fun startServer() {
        // MockWebServer's TLS side fails on Android 9 (no SNI there); the transport itself is plain OkHttp.
        assumeTrue(Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q)
        // The transport only speaks HTTPS, so the test server gets a certificate the client trusts.
        val localhost = InetAddress.getByName("localhost").canonicalHostName
        val certificate = HeldCertificate.Builder().addSubjectAlternativeName(localhost).build()
        server.useHttps(HandshakeCertificates.Builder().heldCertificate(certificate).build().sslSocketFactory())
        server.start()
        val trust = HandshakeCertificates.Builder().addTrustedCertificate(certificate.certificate).build()
        client = OkHttpClient.Builder().sslSocketFactory(trust.sslSocketFactory(), trust.trustManager).build()
    }

    @After
    fun stopServer() = server.close()


    @Test
    fun transportSendsHeadersAndReturnsCookies() {
        server.enqueue(MockResponse.Builder().code(200).body("""{"code":200}""")
            .addHeader("Set-Cookie", "MUSIC_U=abc; Path=/").addHeader("Set-Cookie", "__csrf=x").build())
        val response = OkHttpTransport(client).execute(
            HttpRequest(server.url("/api").toString(), "POST", listOf("Cookie" to "a=1"), "params=x".toByteArray()))
        assertEquals(200, response.status)
        assertEquals("""{"code":200}""", response.body.decodeToString())
        assertEquals(listOf("MUSIC_U=abc; Path=/", "__csrf=x"), response.setCookies)
        val recorded = server.takeRequest()
        assertEquals("a=1", recorded.headers["Cookie"])
        assertEquals("params=x", recorded.body?.utf8())
        val plain = assertThrows(LyricException::class.java) {
            OkHttpTransport(client).execute(HttpRequest("http://example.com/", "GET", emptyList(), ByteArray(0)))
        }
        assertEquals("只支持 HTTPS 请求。", plain.message)
    }

    @Test
    fun cancellingStopsTheRequestInFlight() = runBlocking {
        server.enqueue(MockResponse.Builder().body("{}").headersDelay(20, TimeUnit.SECONDS).build())
        val song = LyricSong(MusicProvider.QQ, "x", "", "", "", 0, "", "", "qq:x")
        val token = CancelToken()
        // QQ requests go to y.qq.com; route them to the test server instead.
        val routed = MusicApi(HttpTransport { request ->
            OkHttpTransport(client).execute(HttpRequest(server.url("/slow").toString(), request.method, request.headers, request.body))
        })
        val started = System.nanoTime()
        val running = async(kotlinx.coroutines.Dispatchers.Default) { runCatching { routed.lyrics(song, "", token) } }
        delay(500)
        token.cancel()
        val result = withTimeout(5_000) { running.await() }
        assertTrue(result.exceptionOrNull() is CoreCancelledException)
        assertTrue("cancelled promptly", System.nanoTime() - started < 5_000_000_000)
        routed.close()
    }
}
