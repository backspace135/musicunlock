package io.github.backspace135.musicunlock.core

import android.os.Build
import androidx.test.ext.junit.runners.AndroidJUnit4
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertThrows
import org.junit.Test
import org.junit.runner.RunWith

/** The core's platform text conversion, called from C++ into PlatformText.kt. */
@RunWith(AndroidJUnit4::class)
class PlatformTextTest {
    @Test
    fun pinyinMatchesTheMacTransform() {
        val latin = Lyrics.mandarinToLatin("我们在重庆银行唱歌 Hello 了")
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.Q) {
            assertNull(latin)
        } else {
            // The same expectation as the core's pinyin_uses_system_transliteration on macOS and Windows.
            assertEquals("wǒ men zài chóng qìng yín xíng chàng gē Hello le", latin)
        }
    }

    @Test
    fun gbkLyricFilesDecode() {
        val gbk = byteArrayOf('['.code.toByte(), '0'.code.toByte(), '0'.code.toByte(), ':'.code.toByte(),
            '0'.code.toByte(), '1'.code.toByte(), ']'.code.toByte(), 0xd6.toByte(), 0xd0.toByte(), 0xce.toByte(),
            0xc4.toByte())
        assertEquals("[00:01]中文", Lyrics.decodeLyricFile(gbk))
        // Characters outside the BMP survive the trip through JNI (NewStringUTF would mangle them).
        assertEquals("🎵 歌词", Lyrics.decodeLyricFile("🎵 歌词".toByteArray()))
    }

    @Test
    fun coreErrorsArriveWithTheirMessage() {
        val error = assertThrows(LyricException::class.java) { Lyrics.decodeLyricFile(ByteArray(5_000_001)) }
        assertEquals("歌词文件超过 5 MB，请选择较小的 LRC 或 TXT。", error.message)
        // Invalid GB18030 is "cannot decode", reported by the core rather than thrown from Kotlin.
        assertThrows(LyricException::class.java) { Lyrics.decodeLyricFile(byteArrayOf(0x81.toByte(), 0x20)) }
    }
}
