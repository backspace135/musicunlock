package io.github.backspace135.musicunlock.core

import androidx.test.ext.junit.runners.AndroidJUnit4
import kotlinx.coroutines.runBlocking
import org.junit.After
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith
import java.io.File

/** The whole lyric flow of #32 against canned NetEase responses: search, lyrics, edit, export to a folder. */
@RunWith(AndroidJUnit4::class)
class LyricWorkspaceTest {
    private val transport = ScriptedTransport.netease()
    private val api = MusicApi(transport)
    private val workspace = LyricWorkspace()

    @Before
    fun documents() {
        TestDocumentsProvider.reset(Fixtures.context)
    }

    @After
    fun close() {
        workspace.close()
        api.close()
    }

    @Test
    fun searchFetchEditAndExport() = runBlocking {
        val ticks = workspace.changes.value
        workspace.query = "晴天 周杰伦"
        val search = workspace.search()!!
        assertTrue(workspace.searching)
        assertTrue(workspace.changes.value > ticks)
        assertEquals("正在读取网易云音乐…", workspace.loadingText)
        val lyrics = workspace.searchFinished(search, api.runSearch(search, ""))!!
        assertEquals(2, workspace.songs.size)
        assertEquals("搜索结果", workspace.resultsTitle)
        assertEquals("netease:186016", lyrics.song.id)
        assertTrue(workspace.isSelected("netease:186016"))

        workspace.lyricsLoaded(lyrics, api.lyrics(lyrics.song, ""))
        assertEquals("[00:01.00]故事的小黄花\n[00:03.00]从出生那年就飘着", workspace.text(LyricTab.ORIGINAL))
        assertEquals(2, workspace.lineCount)
        workspace.edit(LyricTab.ORIGINAL, "[00:01.00]改过的一行")
        assertTrue(workspace.preview.contains("改过的一行"))
        assertNull(workspace.previewError)

        workspace.toggle("netease:186016")
        workspace.toggle("netease:2")
        assertTrue(workspace.allChecked)
        assertEquals("2 首歌曲，已选 2 首", workspace.resultsSubtitle)
        assertEquals("导出所选 2 首", workspace.exportCheckedLabel)

        workspace.options = LyricOptions(format = LyricFormat.SRT)
        val folder = TestDocumentsProvider.pickedFolder("lyrics-out")
        val batch = workspace.exportChecked()!!
        assertTrue(workspace.exporting)
        assertEquals(LyricFormat.SRT, batch.options.format)
        for (job in batch.jobs) {
            workspace.exportSucceeded(job, api.exportSong(job, batch.options, folder, ""))
        }
        assertEquals("正在导出 2 / 2", workspace.progressText)
        workspace.endExport()
        assertFalse(workspace.exporting)
        assertEquals("导出完成：成功 2 首，跳过 0 首，失败 0 首。", workspace.notice)
        val names = workspace.exported.map { it.name }
        assertEquals(listOf("晴天 - 周杰伦.srt", "晴天（翻唱） - 歌手.srt"), names)
        // The edited text, not the platform's, went into the first file.
        val written = File(TestDocumentsProvider.rootFolder(Fixtures.context), "lyrics-out/晴天 - 周杰伦.srt").readText()
        assertTrue(written.contains("改过的一行"))
        assertEquals(written, Fixtures.read(workspace.exported.first().uri).decodeToString())
    }

    @Test
    fun linksAndLocalFilesAndFailures() = runBlocking {
        workspace.query = "https://example.com/abc"
        assertNull(workspace.search())
        assertTrue(workspace.notice.startsWith("暂不支持这个链接"))
        workspace.dismissNotice()
        assertEquals("", workspace.notice)

        assertTrue(workspace.importLocal("本地歌", "[00:01.00]本地"))
        assertEquals("本地歌", workspace.selected?.title)
        assertEquals("[00:01.00]本地", workspace.text(LyricTab.ORIGINAL))

        workspace.query = "晴天"
        val search = workspace.search()!!
        workspace.searchFailed(search, "网易云音乐暂时无法连接，请稍后重试。")
        assertFalse(workspace.searching)
        assertEquals("网易云音乐暂时无法连接，请稍后重试。", workspace.notice)
        assertNotNull(workspace.options)
    }
}
