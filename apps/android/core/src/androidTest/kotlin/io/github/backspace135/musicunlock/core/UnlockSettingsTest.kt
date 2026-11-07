package io.github.backspace135.musicunlock.core

import android.net.Uri
import androidx.test.ext.junit.runners.AndroidJUnit4
import org.json.JSONObject
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith
import java.io.File

/** The audio export settings keep the Windows keys and fall back from folders that went away. */
@RunWith(AndroidJUnit4::class)
class UnlockSettingsTest {
    private fun saved() = JSONObject(File(Fixtures.context.filesDir, "settings.json").readText())

    @Test
    fun settingsAreSavedWithTheWindowsKeys() {
        UnlockSettings.setNaming(NamingPolicy.ARTIST_TITLE)
        UnlockSettings.setOutputFolder(OutputFolder.DefaultMusic)
        UnlockSettings.flush()
        assertEquals("artistTitle", saved().getString("namingPolicy"))
        assertEquals("", saved().getString("outputDirectory"))
        val picked = TestDocumentsProvider.pickedFolder("settings-out")
        UnlockSettings.setOutputFolder(picked)
        UnlockSettings.flush()
        assertEquals(picked.treeUri.toString(), saved().getString("outputDirectory"))
        assertEquals(picked, OutputFolders.fromSetting(saved().getString("outputDirectory"), OutputFolder.DefaultMusic))
        assertFalse(UnlockSettings.fallBackIfUnusable())
    }

    @Test
    fun unusableFoldersFallBackToTheDefault() {
        val gone = OutputFolder.Picked(Uri.parse("content://${TestDocumentsProvider.AUTHORITY}/tree/missing"))
        assertFalse(OutputFolders.isUsable(gone))
        UnlockSettings.setOutputFolder(gone)
        assertTrue(UnlockSettings.fallBackIfUnusable())
        assertEquals(OutputFolder.DefaultMusic, UnlockSettings.values.value.outputFolder)
        assertEquals(FolderLabel("音匣", "内部存储/Music"), OutputFolders.label(OutputFolder.DefaultMusic))
    }
}
