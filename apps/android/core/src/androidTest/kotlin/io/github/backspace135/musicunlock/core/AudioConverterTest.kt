package io.github.backspace135.musicunlock.core

import android.net.Uri
import android.os.Build
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import kotlinx.coroutines.runBlocking
import org.junit.After
import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Assert.assertThrows
import org.junit.Assert.assertTrue
import org.junit.Assume.assumeTrue
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith
import java.io.File
import java.util.UUID

/** Conversion from documents to MediaStore and picked folders, with the core's naming and numbering. */
@RunWith(AndroidJUnit4::class)
class AudioConverterTest {
    private val published = mutableListOf<Uri>()
    private val token = UUID.randomUUID().toString().take(8)

    @Before
    fun documents() {
        TestDocumentsProvider.reset(Fixtures.context)
    }

    @After
    fun cleanUp() = Fixtures.deletePublished(published)

    private suspend fun convert(uri: Uri, folder: OutputFolder, naming: NamingPolicy = NamingPolicy.ORIGINAL): ConvertedFile {
        val scan = DocumentImporter.scanDocuments(listOf(uri))
        return AudioConverter.convert(scan.files.single().path, folder, naming).also { published += it.uri }
    }

    @Test
    fun everyFixtureDecodesToItsTarget() = runBlocking {
        assumeTrue(Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q)
        for ((name, extension) in Fixtures.qmcContainers) {
            val uri = Fixtures.document("fixtures/$token-$name.$extension", Fixtures.qmcContainer(name))
            val file = convert(uri, OutputFolder.DefaultMusic)
            assertArrayEquals(name, Fixtures.qmcTarget(name), Fixtures.read(file.uri))
            assertEquals("Music/音匣/${file.name}", file.location)
            assertTrue(file.name.startsWith("$token-$name."))
        }
    }

    @Test
    fun collisionsAreNumberedLikeTheCore() = runBlocking {
        assumeTrue(Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q)
        val audio = Fixtures.wave()
        val uri = Fixtures.document("in/$token 中文歌曲.uc", Fixtures.uc(audio))
        val first = convert(uri, OutputFolder.DefaultMusic)
        val second = convert(uri, OutputFolder.DefaultMusic)
        val third = convert(uri, OutputFolder.DefaultMusic)
        assertEquals("$token 中文歌曲.wav", first.name)
        assertEquals("$token 中文歌曲 (1).wav", second.name)
        assertEquals("$token 中文歌曲 (2).wav", third.name)
        assertArrayEquals(audio, Fixtures.read(second.uri))
        assertEquals(listOf(first.name, second.name, third.name).sorted(), Fixtures.musicNames(token))
    }

    @Test
    fun legacyDefaultFolderNumbersCollisions() = runBlocking {
        assumeTrue(Build.VERSION.SDK_INT < Build.VERSION_CODES.Q)
        val permission = OutputFolders.requiredPermissions(OutputFolder.DefaultMusic).single()
        InstrumentationRegistry.getInstrumentation().uiAutomation.grantRuntimePermission(Fixtures.context.packageName, permission)
        assertTrue(OutputFolders.isUsable(OutputFolder.DefaultMusic))
        val audio = Fixtures.wave()
        val uri = Fixtures.document("in/$token 旧版.uc", Fixtures.uc(audio))
        val first = convert(uri, OutputFolder.DefaultMusic)
        val second = convert(uri, OutputFolder.DefaultMusic)
        @Suppress("DEPRECATION")
        val folder = File(android.os.Environment.getExternalStorageDirectory(), "Music/音匣")
        try {
            assertEquals("$token 旧版 (1).wav", second.name)
            assertArrayEquals(audio, File(folder, first.name).readBytes())
            assertArrayEquals(audio, Fixtures.read(second.uri))  // a content URI other apps can open
        } finally {
            File(folder, first.name).delete()
            File(folder, second.name).delete()
        }
    }

    @Test
    fun ncmTagsNameTheOutput() = runBlocking {
        assumeTrue(Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q)
        val container = Fixtures.ncm(Fixtures.flac(4096), "$token 标题", "歌手", cover = byteArrayOf(-1, -40, -1, 0))
        val uri = Fixtures.document("in/原文件.ncm", container)
        val byTitle = convert(uri, OutputFolder.DefaultMusic, NamingPolicy.TITLE)
        assertEquals("$token 标题.flac", byTitle.name)
        assertEquals("歌手", byTitle.artist)
        assertTrue(byTitle.cover!!.contentEquals(byteArrayOf(-1, -40, -1, 0)))
        assertEquals("歌手 - $token 标题.flac", convert(uri, OutputFolder.DefaultMusic, NamingPolicy.ARTIST_TITLE).name)
    }

    @Test
    fun pickedFoldersNumberCollisionsTooAndPipesAreRead() = runBlocking {
        val folder = TestDocumentsProvider.pickedFolder("out")
        assertTrue(OutputFolders.isUsable(folder))
        val audio = Fixtures.wave()
        // ".pipe." documents come through a pipe, like a cloud provider's, which cannot be memory-mapped.
        val uri = Fixtures.document("in/歌.pipe.uc", Fixtures.uc(audio))
        val first = convert(uri, folder)
        val second = convert(uri, folder)
        assertEquals("歌.pipe.wav", first.name)
        assertEquals("歌.pipe (1).wav", second.name)
        assertArrayEquals(audio, File(TestDocumentsProvider.rootFolder(Fixtures.context), "out/歌.pipe (1).wav").readBytes())
        assertEquals("out", OutputFolders.label(folder).name)
    }

    @Test
    fun failuresCarryTheCoreMessage() = runBlocking {
        val folder = TestDocumentsProvider.pickedFolder("out")
        val broken = Fixtures.document("in/坏文件.ncm", "CTENFDAM".toByteArray())
        val path = DocumentImporter.scanDocuments(listOf(broken)).files.single().path
        val error = assertThrows(DecodeException::class.java) { runBlocking { AudioConverter.convert(path, folder, NamingPolicy.ORIGINAL) } }
        assertEquals("文件被截断，或加密容器头部无效", error.message)
        assertEquals(DecodeException.Kind.INVALID, error.kind)
        // Nothing is left behind in the output folder or the staging cache.
        assertEquals(0, File(TestDocumentsProvider.rootFolder(Fixtures.context), "out").list()!!.size)
        val missing = assertThrows(DecodeException::class.java) {
            runBlocking { AudioConverter.convert("/not/imported.ncm", folder, NamingPolicy.ORIGINAL) }
        }
        assertTrue(missing.message.startsWith("无法读取文件"))
    }
}
