package io.github.backspace135.musicunlock.core

import androidx.test.ext.junit.runners.AndroidJUnit4
import kotlinx.coroutines.flow.take
import kotlinx.coroutines.flow.toList
import kotlinx.coroutines.runBlocking
import kotlinx.coroutines.withTimeout
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith
import java.util.concurrent.atomic.AtomicInteger

/** The QR login flow over the core's create_qr_code and poll_qr_login, with canned NetEase answers. */
@RunWith(AndroidJUnit4::class)
class QrLoginTest {
    private fun api(vararg codes: Int, cookies: Array<String> = emptyArray()): Pair<MusicApi, AtomicInteger> {
        val polls = AtomicInteger()
        val api = MusicApi(ScriptedTransport(
            "qrcode/unikey" to { ScriptedTransport.json("""{"code":200,"unikey":"KEY"}""") },
            "qrcode/client/login" to {
                val code = codes[minOf(polls.getAndIncrement(), codes.size - 1)]
                ScriptedTransport.json("""{"code":$code}""", *(if (code == 803) cookies else emptyArray()))
            },
        ))
        return api to polls
    }

    @Test
    fun scanConfirmAndSession() = runBlocking {
        val (api, _) = api(801, 802, 803, cookies = arrayOf("MUSIC_U=abc; Domain=music.163.com; Path=/; Max-Age=3600"))
        val updates = withTimeout(10_000) { QrLogin(api).start(intervalMillis = 0).toList() }
        assertEquals(listOf(QrLoginState.LOADING, QrLoginState.WAITING, QrLoginState.SCANNED, QrLoginState.CONFIRMED),
            updates.map { it.state })
        val waiting = updates[1]
        assertTrue(waiting.url!!.contains("codekey=KEY"))
        val matrix = waiting.matrix!!
        assertTrue(matrix.size >= 21 && matrix.dark(0, 0) && matrix.pathData.startsWith("M0,0h7"))
        assertEquals((matrix.size + 8) * 8, matrix.toBitmap().width)
        assertEquals("已扫码，请在手机上确认登录", updates[2].message)
        assertEquals(listOf("MUSIC_U"), updates.last().session.map { it.name })
        api.close()
    }

    @Test
    fun expiryAndRefusal() = runBlocking {
        val (expiring, _) = api(801, 800)
        assertEquals(QrLoginState.EXPIRED, QrLogin(expiring).start(intervalMillis = 0).toList().last().state)
        val refused = MusicApi(ScriptedTransport("qrcode/unikey" to {
            ScriptedTransport.json("""{"code":8821,"message":"请切换其他登录方式或升级新版本再试"}""")
        }))
        val last = QrLogin(refused).start(intervalMillis = 0).toList().last()
        assertEquals(QrLoginState.FAILED, last.state)
        assertTrue(last.message.startsWith("网易云音乐拒绝了这次扫码登录"))
        expiring.close()
        refused.close()
    }

    @Test
    fun cancellingStopsPolling() = runBlocking {
        val (api, polls) = api(801)
        // Take LOADING and WAITING, then stop collecting: the poll loop must end, not keep asking NetEase.
        QrLogin(api).start(intervalMillis = 50).take(2).toList()
        Thread.sleep(500)
        val after = polls.get()
        Thread.sleep(500)
        assertEquals("no polls after cancelling", after, polls.get())
        api.close()
    }
}
