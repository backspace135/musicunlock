package io.github.backspace135.musicunlock.core

import android.os.Debug
import android.util.Log
import androidx.test.ext.junit.runners.AndroidJUnit4
import kotlinx.coroutines.runBlocking
import org.junit.After
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith
import java.io.File
import java.util.concurrent.atomic.AtomicBoolean
import java.util.concurrent.atomic.AtomicLong
import kotlin.concurrent.thread

/**
 * A 120 MB NCM (FLAC with tags and cover, the most memory-hungry path: decrypted audio plus the re-tagged copy)
 * converts with at most about twice its size in process memory. The source is memory-mapped, so its pages are
 * file-backed and do not count as the app's own (anonymous) memory.
 */
@RunWith(AndroidJUnit4::class)
class LargeFileTest {
    @After
    fun cleanUp() {
        TestDocumentsProvider.reset(Fixtures.context)
    }

    @Test
    fun largeFilesStayWithinTwiceTheirSize() = runBlocking {
        TestDocumentsProvider.reset(Fixtures.context)
        val output = TestDocumentsProvider.pickedFolder("large-out")
        val size = 120L * 1000 * 1000
        val source = File(TestDocumentsProvider.rootFolder(Fixtures.context), "in/大文件.ncm")
        source.parentFile!!.mkdirs()
        source.outputStream().buffered(1 shl 20).use {
            Fixtures.writeNcm(it, size, "大文件", "歌手", ByteArray(200_000) { i -> i.toByte() }, Fixtures::flacBytes)
        }
        val scan = DocumentImporter.scanDocuments(listOf(TestDocumentsProvider.documentUri("in/大文件.ncm")))

        System.gc()
        val baseline = anonymousMemory()
        val peak = AtomicLong(baseline)
        val sampling = AtomicBoolean(true)
        val sampler = thread {
            while (sampling.get()) {
                peak.accumulateAndGet(anonymousMemory(), ::maxOf)
                Thread.sleep(2)
            }
        }
        val started = System.nanoTime()
        val converted = try {
            AudioConverter.convert(scan.files.single().path, output, NamingPolicy.ORIGINAL)
        } finally {
            sampling.set(false)
            sampler.join()
        }
        val seconds = (System.nanoTime() - started) / 1e9
        val grown = peak.get() - baseline
        Log.i("LargeFileTest", "source %d MB, anonymous memory +%d MB at peak, %.1f s".format(
            size / 1_000_000, grown / 1_000_000, seconds))
        assertEquals("大文件.flac", converted.name)
        val written = File(TestDocumentsProvider.rootFolder(Fixtures.context), "large-out/大文件.flac")
        assertTrue(written.length() > size)  // the audio plus the restored tags and cover
        // About twice the size: the decrypted audio, plus the copy made while the tags are written back.
        assertTrue("anonymous memory grew by $grown bytes for a $size byte file", grown <= size * 23 / 10)
    }

    /**
     * The process's anonymous memory: RssAnon from /proc/self/status, or the native heap on kernels without it
     * (Android 9), where the decoded audio lives.
     */
    private fun anonymousMemory(): Long =
        File("/proc/self/status").readLines().firstOrNull { it.startsWith("RssAnon:") }
            ?.substringAfter(':')?.trim()?.substringBefore(' ')?.toLong()?.times(1024)
            ?: Debug.getNativeHeapAllocatedSize()
}
