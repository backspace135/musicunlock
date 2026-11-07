package io.github.backspace135.musicunlock.core

import androidx.test.ext.junit.runners.AndroidJUnit4
import kotlinx.coroutines.async
import kotlinx.coroutines.delay
import kotlinx.coroutines.runBlocking
import kotlinx.coroutines.withTimeout
import org.junit.Assert.assertEquals
import org.junit.Assert.assertThrows
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith

/** Cancellation and the exception mapping between Kotlin callbacks and the core; native threads calling Kotlin. */
@RunWith(AndroidJUnit4::class)
class NetworkTest {
    @Test
    fun cancellingTheCoroutineCancelsTheToken() = runBlocking {
        val slow = MusicApi(HttpTransport { Thread.sleep(200); throw LyricException("网络连接失败，请检查网络或代理设置后重试。") })
        val token = CancelToken()
        val job = async(kotlinx.coroutines.Dispatchers.Default) {
            runCatching { slow.runSearch(SearchRequest(1, "晴天", MusicProvider.NETEASE, SearchKind.SONG, 1, false, null, ""), "", token) }
        }
        delay(50)
        job.cancel()
        withTimeout(5_000) { job.join() }
        assertTrue(token.isCancelled)
        slow.close()
    }

    @Test
    fun kotlinExceptionsCrossTheCoreAndComeBack() = runBlocking {
        val song = LyricSong(MusicProvider.QQ, "x", "", "", "", 0, "", "", "qq:x")
        val offline = MusicApi(HttpTransport { throw LyricException("网络连接失败，请检查网络或代理设置后重试。") })
        assertEquals("网络连接失败，请检查网络或代理设置后重试。",
            assertThrows(LyricException::class.java) { runBlocking { offline.lyrics(song, "") } }.message)
        // Anything else is "cannot connect" in the core's words, never a crash.
        val broken = MusicApi(HttpTransport { throw IllegalStateException("boom") })
        assertEquals("QQ 音乐暂时无法连接，请稍后重试。",
            assertThrows(LyricException::class.java) { runBlocking { broken.lyrics(song, "") } }.message)
        val cancelled = CancelToken().apply { cancel() }
        assertThrows(CoreCancelledException::class.java) { runBlocking { offline.lyrics(song, "", cancelled) } }
        offline.close()
        broken.close()
    }

    @Test
    fun nativeThreadsAttachToCallKotlin() {
        val api = MusicApi(ScriptedTransport("/api/w/nuser/account/get" to {
            ScriptedTransport.json("""{"code":200,"profile":{"nickname":"测试用户🎵"}}""")
        }))
        // The transport runs on a thread C++ started; ThreadEnv attaches it to the JVM and detaches it afterwards.
        assertEquals("测试用户🎵", api.accountNameOnNativeThread(MusicProvider.NETEASE, "MUSIC_U=1"))
        assertEquals("QQ 音乐用户", api.accountNameOnNativeThread(MusicProvider.QQ, "qqmusic_key=1"))
        api.close()
    }
}
