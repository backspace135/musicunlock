package io.github.backspace135.musicunlock.core

import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import kotlinx.coroutines.flow.first
import kotlinx.coroutines.runBlocking
import kotlinx.coroutines.withTimeout
import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Assume.assumeTrue
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith
import java.io.File

/**
 * The real platforms through OkHttp, like the core's MUSICUNLOCK_LIVE_TESTS on Windows. Opt in with the
 * instrumentation argument musicunlockLive=1 (`build.py --device --live`); skipped otherwise.
 */
@RunWith(AndroidJUnit4::class)
class LiveTest {
    @Before
    fun optIn() {
        assumeTrue("set musicunlockLive=1 to check the real platforms",
            InstrumentationRegistry.getArguments().getString("musicunlockLive") == "1")
        TestDocumentsProvider.reset(Fixtures.context)
    }

    @Test
    fun searchLyricsExportAndLinks() = runBlocking {
        val api = MusicApi.shared
        for (provider in MusicProvider.entries) {
            val workspace = LyricWorkspace()
            workspace.provider = provider
            workspace.query = "晴天 周杰伦"
            val search = workspace.search()!!
            val lyrics = workspace.searchFinished(search, api.runSearch(search, ""))!!
            assertTrue(provider.title, workspace.songs.size > 1)
            workspace.lyricsLoaded(lyrics, api.lyrics(lyrics.song, ""))
            assertTrue(provider.title, workspace.lineCount > 10)

            // The exported file is the core's rendering, byte for byte.
            val options = workspace.options.copy(format = LyricFormat.SRT)
            workspace.options = options
            val folder = TestDocumentsProvider.pickedFolder("live-${provider.id}")
            val batch = workspace.exportCurrent()!!
            val outcome = api.exportSong(batch.jobs[0], batch.options, folder, "")
            workspace.exportSucceeded(batch.jobs[0], outcome)
            workspace.endExport()
            val file = outcome.file!!
            assertTrue(file.name.endsWith(".srt"))
            val written = File(TestDocumentsProvider.rootFolder(Fixtures.context), "live-${provider.id}/${file.name}").readBytes()
            val expected = Lyrics.encodeText(options.encoding,
                Lyrics.renderLyrics(workspace.document, options, lyrics.song.durationMs))
            assertArrayEquals(expected, written)

            workspace.query = if (provider == MusicProvider.NETEASE) "https://music.163.com/song?id=186016"
            else "https://y.qq.com/n/ryqq/songDetail/0039MnYb0qxYhV"
            val link = workspace.search()!!
            workspace.searchFinished(link, api.runSearch(link, ""))
            assertEquals(provider.title, "晴天", workspace.songs.single().title)
            workspace.close()
        }
    }

    @Test
    fun qrCodeIsIssued() = runBlocking {
        val waiting = withTimeout(30_000) {
            QrLogin().start().first { it.state != QrLoginState.LOADING }
        }
        assertEquals(waiting.message, QrLoginState.WAITING, waiting.state)
        assertTrue(waiting.url!!.startsWith("https://music.163.com/login?codekey="))
        // A status query on a fresh code is "waiting for a scan" (801), the answer the poll loop keeps getting.
        val ticket = MusicApi.shared.createQrCode()
        assertEquals(801, MusicApi.shared.checkQrCode(ticket).code)
    }
}
