package io.github.backspace135.musicunlock.core

import androidx.test.ext.junit.runners.AndroidJUnit4
import org.json.JSONObject
import org.junit.Assert.assertEquals
import org.junit.Test
import org.junit.runner.RunWith
import java.io.File

/** Lyric export settings keep the Windows lyrics.json layout. */
@RunWith(AndroidJUnit4::class)
class LyricSettingsTest {
    @Test
    fun savedLikeWindows() {
        LyricSettings.setOptions(LyricOptions(format = LyricFormat.SRT, offset = 120))
        LyricSettings.setOutputFolder(OutputFolder.DefaultLyrics)
        LyricSettings.flush()
        val json = JSONObject(File(Fixtures.context.filesDir, "lyrics.json").readText())
        assertEquals("srt", json.getJSONObject("options").getString("format"))
        assertEquals(120, json.getJSONObject("options").getInt("offset"))
        assertEquals("\${name} - \${singer}", json.getJSONObject("options").getString("filename"))
        assertEquals("", json.getString("outputDirectory"))
        assertEquals(FolderLabel("歌词", "内部存储/Download/音匣"), OutputFolders.label(OutputFolder.DefaultLyrics))
    }
}
