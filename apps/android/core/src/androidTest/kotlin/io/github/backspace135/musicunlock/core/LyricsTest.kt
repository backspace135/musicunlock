package io.github.backspace135.musicunlock.core

import android.os.Build
import androidx.test.ext.junit.runners.AndroidJUnit4
import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertThrows
import org.junit.Test
import org.junit.runner.RunWith

/** lyrics.h's free functions and the model enums through JNI. */
@RunWith(AndroidJUnit4::class)
class LyricsTest {
    private val song = LyricSong(MusicProvider.NETEASE, "186016", "晴天", "周杰伦", "叶惠美", 269_000, "", "", "netease:186016")

    @Test
    fun labelsComeFromTheCore() {
        assertEquals(listOf("网易云音乐", "QQ 音乐"), MusicProvider.entries.map { it.title })
        assertEquals("qq", MusicProvider.QQ.id)
        assertEquals(listOf("歌曲", "专辑", "歌单"), SearchKind.entries.map { it.label })
        assertEquals(listOf("LRC", "SRT", "TXT", "VTT"), LyricFormat.entries.map { it.label })
        assertEquals("srt", LyricFormat.SRT.extension)
        assertEquals(listOf("逐行交错", "同一行合并", "分段排列"), LyricLayout.entries.map { it.label })
        assertEquals("UTF-8 BOM", LyricEncoding.UTF8_BOM.label)
        assertEquals("导出预览", LyricTab.PREVIEW.label)
        assertEquals("4:29", song.duration)
        assertEquals(true, MusicProvider.NETEASE.owns(".music.163.com"))
    }

    @Test
    fun optionsUseTheCoresJsonAndClamping() {
        val options = LyricOptions(format = LyricFormat.VTT, layout = LyricLayout.SEPARATE,
            encoding = LyricEncoding.UTF16LE, includePinyin = true, precision = 1, offset = -250, separator = " | ",
            filename = "\${id}")
        assertEquals(options, LyricOptions.fromJson(options.toJson()))
        assertEquals(LyricOptions(), LyricOptions.fromJson("not json"))
        val clamped = LyricOptions.fromJson("""{"format":"mp3","precision":9,"offset":999999999999,"filename":""}""")
        assertEquals(LyricFormat.LRC, clamped.format)
        assertEquals(3, clamped.precision)
        assertEquals(86_400_000L, clamped.offset)
        assertEquals("\${name} - \${singer}", clamped.filename)
    }

    @Test
    fun renderingParsingAndNames() {
        val document = LyricDocument("[00:01.000]你好", "[00:01.000]Hello")
        assertEquals("[00:01.000]你好\n[00:01.000]Hello\n", Lyrics.renderLyrics(document, LyricOptions()))
        val pinyin = LyricOptions(includeTranslation = false, includePinyin = true)
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
            assertEquals("[00:01.000]你好\n[00:01.000]nǐ hǎo\n", Lyrics.renderLyrics(document, pinyin))
        } else {
            assertThrows(LyricException::class.java) { Lyrics.renderLyrics(document, pinyin) }
        }
        assertEquals(listOf(LyricLine(1000, "你好")), Lyrics.parseLyrics("[00:01.000]你好"))
        assertEquals("[01:02.34]", Lyrics.lyricTimestamp(62_345, LyricFormat.LRC, 2))
        assertEquals("晴天 - 周杰伦", Lyrics.lyricFilename(song, LyricOptions().filename, 1))
        assertArrayEquals(byteArrayOf(0xff.toByte(), 0xfe.toByte(), 0x2d, 0x4e), Lyrics.encodeText(LyricEncoding.UTF16LE, "中"))
        assertEquals(MusicLink(MusicProvider.NETEASE, SearchKind.PLAYLIST, "123"),
            Lyrics.parseLink("分享歌单 https://music.163.com/playlist?id=123 (来自网易云音乐)", MusicProvider.QQ, SearchKind.SONG))
        assertNull(Lyrics.parseLink("https://example.com/abc", MusicProvider.NETEASE, SearchKind.SONG))
    }
}
