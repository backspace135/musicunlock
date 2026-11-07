package io.github.backspace135.musicunlock.core

import android.net.Uri
import androidx.test.ext.junit.runners.AndroidJUnit4
import org.junit.After
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertThrows
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith
import kotlin.concurrent.thread

/** AudioFormats and AudioQueue through JNI: the core's rules and wording arrive unchanged. */
@RunWith(AndroidJUnit4::class)
class AudioQueueTest {
    private val queue = AudioQueue()

    @After
    fun close() = queue.close()

    @Test
    fun formatsFollowTheCore() {
        assertTrue("ncm" in AudioFormats.supportedExtensions)
        assertTrue(AudioFormats.isSupportedExtension("MFLAC"))
        assertFalse(AudioFormats.isSupportedExtension("mp3"))
        assertTrue(AudioFormats.isSupportedFileName("歌.NCM"))
        assertFalse(AudioFormats.isSupportedFileName(".ncm"))  // a dot file has no extension
        assertFalse(AudioFormats.isSupportedFileName("notes.txt"))
        assertEquals("网易云", AudioFormats.platformFor("ncm"))
        assertTrue("mflac" in AudioFormats.qqExtensions && "ncm" !in AudioFormats.qqExtensions)
        assertEquals("flac", AudioFormats.sniffAudio("fLaC".toByteArray()))
        assertEquals(null, AudioFormats.sniffAudio(ByteArray(4)))
        assertEquals(listOf("a2", "A10", "b"), listOf("b", "A10", "a2").sortedWith(AudioFormats.naturalOrder))
        assertEquals(listOf("2.ncm", "10.ncm"), AudioFormats.sortedNaturally(listOf("10.ncm", "2.ncm")) { it })
        assertEquals("28.4 MB", AudioFormats.formatFileSize(28_400_000))
        assertEquals("A_B", AudioFormats.sanitizeFileStem("A/B"))
        assertEquals("歌 (2).flac", AudioFormats.numberedFileName("歌", "flac", 2))
        assertEquals(10_000, AudioFormats.maxNameSuffix)
        assertEquals(listOf("保留原文件名", "歌手 - 歌曲名", "歌曲名"), NamingPolicy.entries.map { it.label })
        assertEquals(NamingPolicy.ARTIST_TITLE, NamingPolicy.fromId("artistTitle"))
        assertEquals(NamingPolicy.ORIGINAL, NamingPolicy.fromId("bogus"))
        assertEquals(listOf("全部", "待转换", "已完成", "未完成"), QueueFilter.entries.map { it.label })
        assertEquals(listOf("待转换", "转换中", "已完成", "未完成"), JobState.entries.map { it.label })
        assertTrue(QueueFilter.FAILED.includes(JobState.FAILED))
        assertFalse(QueueFilter.DONE.includes(JobState.WAITING))
    }

    @Test
    fun addingReportsLikeWindows() {
        val scan = ImportScan(listOf(ImportedFile("/x/远行的旋律.ncm", 28_400_000), ImportedFile("/x/2.mflac", 10)), 1)
        val ticks = queue.changes.value
        assertEquals("已添加 2 个文件，跳过 1 个其他格式的文件。", queue.add(scan))
        assertTrue(queue.changes.value > ticks)
        assertEquals("这些文件已在队列中。", queue.add(scan.copy(skipped = 0)))
        assertEquals("没有找到支持的加密音频文件。可添加 NCM、QMC、MFLAC、MGG 等格式。", queue.add(scan))
        val job = queue.jobs.first()
        assertEquals("远行的旋律", job.displayTitle)
        assertEquals("网易云 · 28.4 MB", job.subtitle)
        assertEquals("NCM", job.sourceFormat)
        assertEquals(JobState.WAITING, job.state)
        assertEquals("2 个待转换，0 个已完成", queue.statusTitle)
        assertEquals(2, queue.visible(QueueFilter.WAITING).size)
    }

    @Test
    fun batchesRecordResultsAndStop() {
        queue.add(ImportScan(listOf(ImportedFile("/x/a.ncm", 1), ImportedFile("/x/b.ncm", 1), ImportedFile("/x/c.ncm", 1)), 0))
        val ids = queue.beginBatch()
        assertEquals(3, ids.size)
        assertTrue(queue.running)
        queue.jobStarted(ids[0])
        assertEquals("正在转换 1 / 3", queue.statusTitle)
        val uri = Uri.parse("content://media/external/audio/media/1")
        val cover = byteArrayOf(1, 2, 3)
        queue.jobSucceeded(ids[0], ConvertedFile(uri, "标题 (1).flac", "Music/音匣/标题 (1).flac", "flac", "标题",
            "歌手", "专辑", cover))
        queue.jobStarted(ids[1])
        queue.jobFailed(ids[1], AudioQueue.failureMessage(DecodeException(DecodeException.Kind.INVALID, "")))
        queue.requestStop()
        assertTrue(queue.stopRequested)
        assertEquals("已停止。未开始的文件仍保留在队列中，可以继续转换。", queue.endBatch())
        val done = queue.find(ids[0])!!
        assertEquals(JobState.DONE, done.state)
        assertEquals("标题 (1).flac", done.detail)
        assertEquals("标题", done.displayTitle)
        assertEquals(uri, done.outputUri)
        assertTrue(done.hasCover)
        assertTrue(cover.contentEquals(queue.cover(ids[0])))
        assertEquals("转换失败，文件可能已损坏或无法读取。", queue.find(ids[1])!!.detail)
        assertNull(queue.cover(ids[1]))
        assertEquals(1, queue.count(JobState.WAITING))
        assertTrue(queue.retryFailed())
        assertEquals(2, queue.count(JobState.WAITING))
        queue.clearCompleted()
        assertEquals(2, queue.jobs.size)
        assertTrue(queue.remove(ids[1]))
        queue.clear()
        assertEquals("准备好，随时开始", queue.statusTitle)
        assertEquals("支持文件夹批量导入", queue.statusDetail)
    }

    @Test
    fun otherThreadsAreRefused() {
        var error: Throwable? = null
        thread { error = runCatching { queue.jobs }.exceptionOrNull() }.join()
        assertTrue(error is IllegalStateException)
        queue.close()
        assertThrows(IllegalStateException::class.java) { queue.jobs }
    }
}
