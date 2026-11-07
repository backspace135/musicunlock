package io.github.backspace135.musicunlock.core

import android.content.Intent
import androidx.test.ext.junit.runners.AndroidJUnit4
import kotlinx.coroutines.runBlocking
import org.junit.After
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith
import java.io.File

/** Imports from documents, folders and share intents, with import.cpp's rules. */
@RunWith(AndroidJUnit4::class)
class DocumentImporterTest {
    private val queue = AudioQueue()

    @Before
    fun documents() {
        TestDocumentsProvider.reset(Fixtures.context)
    }

    @After
    fun close() = queue.close()

    @Test
    fun pickedFilesKeepTheirNameAndSizeAndDeduplicate() = runBlocking {
        val first = Fixtures.document("in/10 歌.ncm", ByteArray(1234))
        val second = Fixtures.document("in/2 歌.mflac", ByteArray(10))
        val other = Fixtures.document("in/notes.txt", ByteArray(3))
        val scan = DocumentImporter.scanDocuments(listOf(first, second, other, first))
        assertEquals(1, scan.skipped)
        assertEquals(2, scan.files.size)
        assertTrue(scan.files[0].path.endsWith("/2 歌.mflac"))  // natural order: 2 before 10
        assertEquals(1234L, scan.files[1].size)
        assertEquals("已添加 2 个文件，跳过 1 个其他格式的文件。", queue.add(scan))
        assertEquals("10 歌", queue.jobs.first { it.size == 1234L }.displayTitle)
        // The same URI again is the same path, so the core reports it as already queued.
        assertEquals("这些文件已在队列中。", queue.add(DocumentImporter.scanDocuments(listOf(first))))
    }

    @Test
    fun foldersAreScannedLikeImportCpp() = runBlocking {
        Fixtures.document("music/10.ncm", ByteArray(1))
        Fixtures.document("music/2.ncm", ByteArray(1))
        Fixtures.document("music/sub/1.qmcflac", ByteArray(1))
        Fixtures.document("music/sub/cover.jpg", ByteArray(1))
        Fixtures.document("music/.hidden/skip.ncm", ByteArray(1))
        Fixtures.document("music/.skip.ncm", ByteArray(1))
        Fixtures.document("music/unreadable/x.ncm", ByteArray(1))
        val scan = DocumentImporter.scanTree(TestDocumentsProvider.treeUri("music"))
        assertEquals(listOf("10.ncm", "2.ncm", "1.qmcflac").sorted(), scan.files.map { File(it.path).name }.sorted())
        // Natural order of the path within the folder: "music/2.ncm" < "music/10.ncm" < "music/sub/1.qmcflac".
        assertEquals(listOf("2.ncm", "10.ncm", "1.qmcflac"), scan.files.map { File(it.path).name })
        assertEquals(1, scan.skipped)  // cover.jpg; hidden names are not counted
    }

    @Test
    fun sharedFilesAreCopiedAndReleased() = runBlocking {
        val shared = Fixtures.document("share/歌.uc", Fixtures.uc(Fixtures.wave()))
        val intent = Intent(Intent.ACTION_SEND_MULTIPLE).putParcelableArrayListExtra(Intent.EXTRA_STREAM,
            arrayListOf(shared, Fixtures.document("share/a.txt", ByteArray(1))))
        val scan = DocumentImporter.scanIntent(intent)!!
        assertEquals(1, scan.skipped)
        val copy = File(scan.files.single().path)
        assertTrue(copy.isFile)
        assertEquals("已添加 1 个文件，跳过 1 个其他格式的文件。", queue.add(scan))
        queue.clear()
        assertTrue("a cleared job's copy is deleted", !copy.exists())
        assertNull(DocumentImporter.scanIntent(Intent(Intent.ACTION_MAIN)))
    }
}
