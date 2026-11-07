package io.github.backspace135.musicunlock.core

import android.net.Uri
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import org.json.JSONArray
import java.util.Collections
import java.util.WeakHashMap

/**
 * One row of the queue (queue.h `AudioJob`), a snapshot. The text fields are the core's wording; show them as is.
 */
data class AudioJob(
    val id: Long,
    /** The import path (see [ImportScan]); pass it to [AudioConverter.convert]. */
    val source: String,
    val size: Long,
    val state: JobState,
    /** Where the converted file went, e.g. "Music/音匣/歌名.flac"; empty until done. */
    val output: String,
    /** "flac", "mp3"...; empty until done. */
    val outputExtension: String,
    /** The title from the restored tags, if any. */
    val title: String,
    /** The output file name when done, the failure reason when failed. */
    val detail: String,
    /** Tag title, else the source file name without extension. */
    val displayTitle: String,
    /** "NCM", "MFLAC"... */
    val sourceFormat: String,
    /** "网易云 · 28.4 MB". */
    val subtitle: String,
    /** The whole row for screen readers. */
    val spokenSummary: String,
    /** Whether [AudioQueue.cover] has a cover for this job. */
    val hasCover: Boolean,
    /** The converted file, for "打开" and "分享"; null until done. */
    val outputUri: Uri?,
)

/**
 * The conversion queue (queue.h `AudioQueue`): jobs, filters, serial batches and the user-facing wording.
 *
 * Not thread-safe, like the core's: call it only from the thread that created it (the main thread). Each call
 * is quick; the conversion itself is [AudioConverter.convert], on a background dispatcher. [changes] ticks after
 * every change, so a screen can collect it and re-read [jobs] and the status texts.
 *
 * Holds native memory: call [close] when done (e.g. in `onCleared`). To survive the activity being recreated or
 * destroyed while a batch runs, keep the queue in an app-scoped object rather than a ViewModel (see the README).
 */
class AudioQueue : AutoCloseable {
    private var handle = nativeCreate()
    private val owner = Thread.currentThread()
    private val outputs = mutableMapOf<Long, Uri>()
    private val version = MutableStateFlow(0L)
    private var snapshot: Pair<Long, List<AudioJob>>? = null

    @Volatile
    private var sources: Set<String> = emptySet()

    /** Increments after every change made through this object. */
    val changes: StateFlow<Long> = version.asStateFlow()

    init {
        synchronized(live) { live += this }
    }

    /** Adds files not already queued. Returns the notice to show; empty means clear it. */
    fun add(scan: ImportScan): String {
        val paths = scan.files.map { it.path }
        ImportedSources.markQueued(paths)
        val notice = nativeAdd(checked(), paths.toTypedArray(), scan.files.map { it.size }.toLongArray(), scan.skipped)
        changed()
        sources = jobs.mapTo(HashSet()) { it.source }
        return notice
    }

    /** All jobs in queue order. */
    val jobs: List<AudioJob>
        get() {
            val current = version.value
            snapshot?.let { (at, jobs) -> if (at == current) return jobs }
            return parseJobs(nativeJobs(checked())).also { snapshot = current to it }
        }

    /** The jobs a filter shows. */
    fun visible(filter: QueueFilter): List<AudioJob> = jobs.filter { filter.includes(it.state) }

    fun find(id: Long): AudioJob? = jobs.firstOrNull { it.id == id }

    fun count(state: JobState): Int = jobs.count { it.state == state }

    /** The cover restored from the tags of a converted job (JPEG or PNG bytes), or null. */
    fun cover(id: Long): ByteArray? = nativeCover(checked(), id)

    /** Removes a job; running jobs stay. */
    fun remove(id: Long): Boolean = nativeRemove(checked(), id).also { changed(pruned = true) }

    fun clearCompleted() {
        nativeClearCompleted(checked())
        changed(pruned = true)
    }

    /** Removes every job; ignored while a batch runs. */
    fun clear() {
        nativeClear(checked())
        changed(pruned = true)
    }

    /** Starts a serial batch over the waiting jobs; empty when one is running or nothing waits. */
    fun beginBatch(): List<Long> = nativeBeginBatch(checked()).toList().also { changed() }

    fun jobStarted(id: Long) {
        nativeJobStarted(checked(), id)
        changed()
    }

    fun jobSucceeded(id: Long, file: ConvertedFile) {
        nativeJobSucceeded(checked(), id, file.location, file.extension, file.title, file.artist, file.album, file.cover)
        outputs[id] = file.uri
        changed()
    }

    /** [message] is the exception's message (see [failureMessage]). */
    fun jobFailed(id: Long, message: String) {
        nativeJobFailed(checked(), id, message)
        changed()
    }

    /** The current file finishes, then the batch stops. */
    fun requestStop() {
        nativeRequestStop(checked())
        changed()
    }

    /** Ends the batch; returns the notice to show (the stop message), or empty. */
    fun endBatch(): String = nativeEndBatch(checked()).also { changed() }

    /** Failed jobs go back to waiting. False while a batch runs. */
    fun retryFailed(): Boolean = nativeRetryFailed(checked()).also { changed() }

    private val status: LongArray get() = nativeStatus(checked())
    val running: Boolean get() = status[0] != 0L
    val stopRequested: Boolean get() = status[1] != 0L
    val batchFinished: Int get() = status[2].toInt()
    val batchCount: Int get() = status[3].toInt()
    val totalSize: Long get() = status[4]

    /** Footer text: "正在转换 2 / 5", "1 个待转换，1 个已完成，1 个未完成", "准备好，随时开始". */
    val statusTitle: String get() = nativeStatusTitle(checked())

    /** Second footer line: the total size, or the folder import hint for an empty queue. */
    val statusDetail: String get() = nativeStatusDetail(checked())

    override fun close() {
        check(Thread.currentThread() === owner) { THREAD_MESSAGE }
        if (handle == 0L) return
        nativeDestroy(handle)
        handle = 0
        sources = emptySet()
        synchronized(live) { live -= this }
        prune()
    }

    private fun checked(): Long {
        check(Thread.currentThread() === owner) { THREAD_MESSAGE }
        check(handle != 0L) { "AudioQueue 已关闭" }
        return handle
    }

    private fun changed(pruned: Boolean = false) {
        version.value++
        if (pruned) {
            val current = jobs
            outputs.keys.retainAll(current.mapTo(HashSet()) { it.id })
            sources = current.mapTo(HashSet()) { it.source }
            prune()
        }
    }

    private fun parseJobs(json: String): List<AudioJob> {
        val array = JSONArray(json)
        return List(array.length()) { index ->
            val item = array.getJSONObject(index)
            val id = item.getLong("id")
            AudioJob(
                id = id,
                source = item.getString("source"),
                size = item.getLong("size"),
                state = JobState.entries[item.getInt("state")],
                output = item.getString("output"),
                outputExtension = item.getString("outputExtension"),
                title = item.getString("title"),
                detail = item.getString("detail"),
                displayTitle = item.getString("displayTitle"),
                sourceFormat = item.getString("sourceFormat"),
                subtitle = item.getString("subtitle"),
                spokenSummary = item.getString("spokenSummary"),
                hasCover = item.getBoolean("hasCover"),
                outputUri = outputs[id],
            )
        }
    }

    companion object {
        private const val THREAD_MESSAGE = "AudioQueue 只能在创建它的线程（主线程）上调用"

        init {
            MusicUnlockCore.load()
        }

        private val live: MutableSet<AudioQueue> = Collections.newSetFromMap(WeakHashMap())

        /** Drops the imports no open queue holds any more (and the cached copies of shared files). */
        private fun prune() {
            val paths = synchronized(live) { live.toList() }.flatMapTo(HashSet()) { it.sources }
            ImportedSources.retainOnly(paths)
        }

        /**
         * The text for [jobFailed]: the exception's message, or the Windows wording when it is empty or garbled.
         */
        fun failureMessage(error: Throwable): String {
            val message = error.message.orEmpty().trim()
            return if (message.isEmpty() || '�' in message) "转换失败，文件可能已损坏或无法读取。" else message
        }

        @JvmStatic private external fun nativeCreate(): Long
        @JvmStatic private external fun nativeDestroy(queue: Long)
        @JvmStatic private external fun nativeAdd(queue: Long, paths: Array<String>, sizes: LongArray, skipped: Int): String
        @JvmStatic private external fun nativeJobs(queue: Long): String
        @JvmStatic private external fun nativeCover(queue: Long, id: Long): ByteArray?
        @JvmStatic private external fun nativeRemove(queue: Long, id: Long): Boolean
        @JvmStatic private external fun nativeClearCompleted(queue: Long)
        @JvmStatic private external fun nativeClear(queue: Long)
        @JvmStatic private external fun nativeBeginBatch(queue: Long): LongArray
        @JvmStatic private external fun nativeJobStarted(queue: Long, id: Long)
        @JvmStatic private external fun nativeJobSucceeded(queue: Long, id: Long, path: String, extension: String,
                                                           title: String, artist: String, album: String,
                                                           cover: ByteArray?)
        @JvmStatic private external fun nativeJobFailed(queue: Long, id: Long, message: String)
        @JvmStatic private external fun nativeRequestStop(queue: Long)
        @JvmStatic private external fun nativeEndBatch(queue: Long): String
        @JvmStatic private external fun nativeRetryFailed(queue: Long): Boolean
        @JvmStatic private external fun nativeStatus(queue: Long): LongArray
        @JvmStatic private external fun nativeStatusTitle(queue: Long): String
        @JvmStatic private external fun nativeStatusDetail(queue: Long): String
    }
}
