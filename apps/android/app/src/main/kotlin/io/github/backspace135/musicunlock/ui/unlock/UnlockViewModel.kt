package io.github.backspace135.musicunlock.ui.unlock

import androidx.annotation.StringRes
import androidx.compose.runtime.Immutable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import androidx.lifecycle.ViewModel
import androidx.lifecycle.viewModelScope
import io.github.backspace135.musicunlock.R
import kotlinx.coroutines.Job
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch

// FAKE DATA (#30). #31 replaces this file: the queue, states, counts and texts come from the core's
// AudioQueue instead. Keep the screen talking to a ViewModel like this one.

enum class JobState { Pending, Running, Done, Failed }

enum class QueueFilter(@StringRes val label: Int) {
    All(R.string.unlock_filter_all),
    Pending(R.string.unlock_filter_pending),
    Finished(R.string.unlock_filter_finished),
    Unfinished(R.string.unlock_filter_unfinished),
}

@Immutable
data class FakeJob(
    val id: Long,
    val title: String,
    val platform: String,
    val format: String,
    val sizeMb: Float,
    val state: JobState = JobState.Pending,
    val output: String? = null,
    val detail: String? = null,
)

/** A banner text as a string resource plus its arguments, so the ViewModel needs no Context. */
@Immutable
data class Notice(@StringRes val text: Int, val args: List<Any> = emptyList())

class UnlockViewModel : ViewModel() {
    var jobs by mutableStateOf(SampleJobs.take(5))
        private set
    var filter by mutableStateOf(QueueFilter.All)
    var notice by mutableStateOf<Notice?>(null)
    var running by mutableStateOf(false)
        private set
    var batchTotal by mutableStateOf(0)
        private set
    var batchFinished by mutableStateOf(0)
        private set
    private var batch: Job? = null
    private var nextSample = 5

    val visibleJobs: List<FakeJob>
        get() = jobs.filter {
            when (filter) {
                QueueFilter.All -> true
                QueueFilter.Pending -> it.state == JobState.Pending || it.state == JobState.Running
                QueueFilter.Finished -> it.state == JobState.Done
                QueueFilter.Unfinished -> it.state == JobState.Failed
            }
        }
    val pendingCount: Int get() = jobs.count { it.state == JobState.Pending }
    val failedCount: Int get() = jobs.count { it.state == JobState.Failed }
    val finishedCount: Int get() = jobs.count { it.state == JobState.Done }

    fun addFiles() {
        val added = (0 until 2).map {
            val sample = SampleJobs[nextSample++ % SampleJobs.size]
            sample.copy(id = System.nanoTime() + it)
        }
        jobs = added + jobs
        filter = QueueFilter.All
        notice = Notice(R.string.unlock_added_notice_format, listOf(added.size))
    }

    fun remove(id: Long) {
        jobs = jobs.filterNot { it.id == id }
    }

    fun clearFinished() {
        jobs = jobs.filterNot { it.state == JobState.Done }
    }

    fun clearQueue() {
        jobs = emptyList()
        notice = null
    }

    fun start() {
        if (running || pendingCount == 0) return
        val ids = jobs.filter { it.state == JobState.Pending }.map { it.id }
        batchTotal = ids.size
        batchFinished = 0
        running = true
        batch = viewModelScope.launch {
            try {
                for (id in ids) {
                    if (jobs.none { it.id == id }) continue
                    update(id) { it.copy(state = JobState.Running) }
                    delay(1200)
                    update(id) {
                        if (it.title.contains("损坏")) {
                            it.copy(state = JobState.Failed, detail = "转换失败，文件可能已损坏或无法读取。")
                        } else {
                            it.copy(state = JobState.Done, output = if (it.format.contains("FLAC")) "FLAC" else "MP3")
                        }
                    }
                    batchFinished++
                }
                notice = Notice(R.string.unlock_finished_notice_format, listOf(finishedCount, failedCount))
            } finally {
                jobs = jobs.map { if (it.state == JobState.Running) it.copy(state = JobState.Pending) else it }
                running = false
            }
        }
    }

    fun stop() {
        batch?.cancel()
    }

    fun retryFailed() {
        jobs = jobs.map { if (it.state == JobState.Failed) it.copy(state = JobState.Pending, detail = null, title = it.title.replace("（损坏）", "")) else it }
        filter = QueueFilter.All
        start()
    }

    private fun update(id: Long, change: (FakeJob) -> FakeJob) {
        jobs = jobs.map { if (it.id == id) change(it) else it }
    }
}

private val SampleJobs = listOf(
    FakeJob(1, "海边的风 - 林夕晨", "网易云", "NCM", 28.4f),
    FakeJob(2, "Morning Light - Aster", "QQ 音乐", "MFLAC", 31.2f),
    FakeJob(3, "雨后（损坏） - 陈一", "QQ 音乐", "MGG", 6.1f),
    FakeJob(4, "Paper Planes - Nova", "网易云", "NCM", 9.8f),
    FakeJob(5, "旧时光 - 许诺", "酷狗", "KGM", 7.4f),
    FakeJob(6, "Glass City - Lumen", "QQ 音乐", "QMCFLAC", 26.7f),
    FakeJob(7, "长夜 - 白桦", "网易云", "NCM", 11.3f),
    FakeJob(8, "Northbound - Kite", "酷我", "KWM", 8.2f),
)
