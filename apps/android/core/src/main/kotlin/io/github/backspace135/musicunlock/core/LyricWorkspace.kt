package io.github.backspace135.musicunlock.core

import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import org.json.JSONObject

/**
 * The lyrics workspace (lyric_workspace.h `LyricWorkspace`): search results, the checked set, edited documents,
 * the export batch and every text the screen shows. Mirrors LyricsModel.swift and the Windows LyricsView.
 *
 * Not thread-safe, like the core's: call it only from the thread that created it (the main thread). Calls that
 * start work return a request (`SearchRequest`, `LyricsRequest`, `ExportBatch`); run it with [MusicApi] on a
 * background coroutine and hand the result back (`searchFinished` / `lyricsLoaded` / `exportSucceeded`, or the
 * `…Failed` call with the exception's message). Results of a request that has since been replaced are ignored by
 * the core; do not filter them yourself. [changes] ticks after every change.
 *
 * Holds native memory: call [close] when done. Edits live only in memory, as on Windows.
 */
class LyricWorkspace : AutoCloseable {
    private var handle = nativeCreate()
    private val owner = Thread.currentThread()
    private val version = MutableStateFlow(0L)
    private var cache: Pair<Long, Texts>? = null
    private val outputs = mutableMapOf<String, ExportedFile>()

    /** Increments after every change made through this object. */
    val changes: StateFlow<Long> = version.asStateFlow()

    init {
        // export_current / export_checked create this folder; files are written to staging folders, then published.
        nativeSetOutputDirectory(checked(), CacheFolders.lyricExports.path)
    }

    // Search bar

    var provider: MusicProvider
        get() = MusicProvider.entries[state[0].toInt()]
        set(value) = change { nativeSetProvider(checked(), value.ordinal) }

    var kind: SearchKind
        get() = SearchKind.entries[state[1].toInt()]
        set(value) = change { nativeSetKind(checked(), value.ordinal) }

    var query: String
        get() = texts.query
        set(value) = change { nativeSetQuery(checked(), value) }

    /** A new search (or the next page with [more]). Null when the query is unusable; then [notice] says why. */
    fun search(more: Boolean = false): SearchRequest? = request(nativeSearch(checked(), more), ModelJson::searchRequest)

    /** Lists the songs of an album or playlist in place of the results. */
    fun open(collection: MusicCollection): SearchRequest? =
        request(nativeOpen(checked(), ModelJson.json(collection).toString()), ModelJson::searchRequest)

    /** "查找这首歌的歌词" from the audio queue: searches songs by the track's title. */
    fun searchForAudio(title: String): SearchRequest? =
        request(nativeSearchForAudio(checked(), title), ModelJson::searchRequest)

    /** Applies the results; returns the lyrics to fetch for the first song of a new search. */
    fun searchFinished(request: SearchRequest, result: SearchResult): LyricsRequest? = request(
        nativeSearchFinished(checked(), ModelJson.json(request).toString(), ModelJson.json(result).toString()),
        ModelJson::lyricsRequest,
    )

    fun searchFailed(request: SearchRequest, message: String) =
        change { nativeSearchFailed(checked(), ModelJson.json(request).toString(), message) }

    fun cancelSearch() = change { nativeCancelSearch(checked()) }

    val searching: Boolean get() = state[2] != 0L
    val hasMore: Boolean get() = state[3] != 0L

    val songs: List<LyricSong> get() = texts.songs
    val collections: List<MusicCollection> get() = texts.collections
    val selected: LyricSong? get() = texts.selected
    fun isSelected(songId: String): Boolean = selected?.id == songId

    /** Previews a song (separate from checking it). Returns the lyrics to fetch when not cached. */
    fun select(songId: String, reload: Boolean = false): LyricsRequest? =
        request(nativeSelect(checked(), songId, reload), ModelJson::lyricsRequest)

    /** Fetches the selected song's lyrics again, replacing the edit. */
    fun retryLyrics(): LyricsRequest? = request(nativeRetryLyrics(checked()), ModelJson::lyricsRequest)

    val canRetry: Boolean get() = state[11] != 0L

    fun lyricsLoaded(request: LyricsRequest, document: LyricDocument) = change {
        nativeLyricsLoaded(checked(), ModelJson.json(request).toString(), ModelJson.json(document).toString())
    }

    fun lyricsFailed(request: LyricsRequest, message: String) =
        change { nativeLyricsFailed(checked(), ModelJson.json(request).toString(), message) }

    val loadingLyrics: Boolean get() = state[4] != 0L

    // Batch selection

    fun toggle(songId: String) = change { nativeToggle(checked(), songId) }

    /** Checks every listed song, or unchecks them all when they already are. */
    fun selectAll() = change { nativeSelectAll(checked()) }

    val allChecked: Boolean get() = state[13] != 0L
    fun isChecked(songId: String): Boolean = nativeIsChecked(checked(), songId)
    val checkedCount: Int get() = state[9].toInt()

    /** Shows a local file's text ([LocalLyrics.read]) as a new song at the top of the list. False while exporting. */
    fun importLocal(title: String, text: String): Boolean = nativeImportLocal(checked(), title, text).also { changed() }

    // Editor

    val document: LyricDocument get() = texts.document

    /** The editable text of a tab; the export preview for [LyricTab.PREVIEW]. */
    fun text(tab: LyricTab): String = nativeText(checked(), tab.ordinal)

    /** Editing the original drops the platform's word timing, which no longer matches. Use "\n" line breaks. */
    fun edit(tab: LyricTab, text: String) = change { nativeEdit(checked(), tab.ordinal, text) }

    val lineCount: Int get() = state[10].toInt()

    // Export

    var options: LyricOptions
        get() = texts.options
        set(value) = change { nativeSetOptions(checked(), value.toJson()) }

    /** Rendered with the current options; empty when nothing is selected or the options are invalid. */
    val preview: String get() = texts.preview

    /** Why the current options cannot be exported, e.g. "请至少选择一种歌词内容。". */
    val previewError: String? get() = texts.previewError

    val canExportCurrent: Boolean get() = state[12] != 0L

    /** The preview text for the clipboard (sets the "已复制" notice), or empty. */
    fun copyPreview(): String = nativeCopyPreview(checked()).also { changed() }

    /** Starts a serial batch with the selected song; null (maybe with a notice) when it cannot. */
    fun exportCurrent(): ExportBatch? = request(nativeExportCurrent(checked()), ModelJson::exportBatch)

    /** Starts a serial batch with the checked songs. */
    fun exportChecked(): ExportBatch? = request(nativeExportChecked(checked()), ModelJson::exportBatch)

    fun exportSucceeded(job: ExportJob, outcome: ExportOutcome) = change {
        outcome.file?.let { outputs[it.location] = it }
        nativeExportSucceeded(checked(), ModelJson.json(job).toString(), ModelJson.json(outcome.document).toString(),
            outcome.file?.location)
    }

    fun exportFailed(job: ExportJob, message: String) =
        change { nativeExportFailed(checked(), ModelJson.json(job).toString(), message) }

    /** The current song finishes, then the batch stops. Also cancel the export's [CancelToken]. */
    fun requestStop() = change { nativeRequestStop(checked()) }

    val stopRequested: Boolean get() = state[6] != 0L

    /** Ends the batch and sets the summary notice ("导出完成：成功 18 首，跳过 1 首，失败 1 首。"). */
    fun endExport() = change { nativeEndExport(checked()) }

    val exporting: Boolean get() = state[5] != 0L
    val progress: Int get() = state[7].toInt()
    val exportTotal: Int get() = state[8].toInt()

    /** "{歌名}：{原因}" for each failed song of the last batch. */
    val failures: List<String> get() = texts.failures

    /** The files the last batch wrote. */
    val exported: List<ExportedFile> get() = texts.exported.mapNotNull { outputs[it] }

    val notice: String get() = texts.notice

    /** Errors from outside the model, such as an unreadable local file. */
    fun showNotice(text: String) = change { nativeShowNotice(checked(), text) }

    fun dismissNotice() = change { nativeDismissNotice(checked()) }

    // Wording

    /** Album / playlist name, or "搜索结果". */
    val resultsTitle: String get() = texts.resultsTitle

    /** "2 首歌曲，已选 1 首". */
    val resultsSubtitle: String get() = texts.resultsSubtitle

    /** "正在读取网易云音乐…". */
    val loadingText: String get() = texts.loadingText

    /** "正在导出 1 / 3". */
    val progressText: String get() = texts.progressText

    /** "2 首导出失败，展开查看". */
    val failuresTitle: String get() = texts.failuresTitle

    /** "导出所选 2 首". */
    val exportCheckedLabel: String get() = texts.exportCheckedLabel

    override fun close() {
        check(Thread.currentThread() === owner) { THREAD_MESSAGE }
        if (handle == 0L) return
        nativeDestroy(handle)
        handle = 0
    }

    private class Texts(
        val query: String,
        val songs: List<LyricSong>,
        val collections: List<MusicCollection>,
        val selected: LyricSong?,
        val document: LyricDocument,
        val options: LyricOptions,
        val preview: String,
        val previewError: String?,
        val failures: List<String>,
        val exported: List<String>,
        val notice: String,
        val resultsTitle: String,
        val resultsSubtitle: String,
        val loadingText: String,
        val progressText: String,
        val failuresTitle: String,
        val exportCheckedLabel: String,
    )

    private val state: LongArray get() = nativeState(checked())

    private val texts: Texts
        get() {
            val current = version.value
            cache?.let { (at, texts) -> if (at == current) return texts }
            val o = JSONObject(nativeTexts(checked()))
            return Texts(
                o.getString("query"),
                ModelJson.list(o.getJSONArray("songs"), ModelJson::song),
                ModelJson.list(o.getJSONArray("collections"), ModelJson::collection),
                o.optJSONObject("selected")?.let(ModelJson::song),
                ModelJson.document(o.getJSONObject("document")),
                LyricOptions.fromJson(o.getJSONObject("options").toString()),
                o.getString("preview"),
                if (o.isNull("previewError")) null else o.getString("previewError"),
                ModelJson.strings(o.getJSONArray("failures")),
                ModelJson.strings(o.getJSONArray("exported")),
                o.getString("notice"),
                o.getString("resultsTitle"),
                o.getString("resultsSubtitle"),
                o.getString("loadingText"),
                o.getString("progressText"),
                o.getString("failuresTitle"),
                o.getString("exportCheckedLabel"),
            ).also { cache = current to it }
        }

    private fun checked(): Long {
        check(Thread.currentThread() === owner) { THREAD_MESSAGE }
        check(handle != 0L) { "LyricWorkspace 已关闭" }
        return handle
    }

    private fun changed() {
        version.value++
    }

    private inline fun change(block: () -> Unit) {
        block()
        changed()
    }

    private fun <T> request(json: String?, parse: (JSONObject) -> T): T? {
        changed()
        return json?.let { parse(JSONObject(it)) }
    }

    companion object {
        private const val THREAD_MESSAGE = "LyricWorkspace 只能在创建它的线程（主线程）上调用"

        init {
            MusicUnlockCore.load()
        }

        /** The artist, or "未知歌手". */
        fun songArtist(song: LyricSong): String = nativeSongArtist(ModelJson.json(song).toString())

        /** "歌手 / 专辑". */
        fun songDetail(song: LyricSong): String = nativeSongDetail(ModelJson.json(song).toString())

        @JvmStatic private external fun nativeCreate(): Long
        @JvmStatic private external fun nativeDestroy(workspace: Long)
        @JvmStatic private external fun nativeState(workspace: Long): LongArray
        @JvmStatic private external fun nativeTexts(workspace: Long): String
        @JvmStatic private external fun nativeIsChecked(workspace: Long, id: String): Boolean
        @JvmStatic private external fun nativeSetProvider(workspace: Long, provider: Int)
        @JvmStatic private external fun nativeSetKind(workspace: Long, kind: Int)
        @JvmStatic private external fun nativeSetQuery(workspace: Long, query: String)
        @JvmStatic private external fun nativeSearch(workspace: Long, more: Boolean): String?
        @JvmStatic private external fun nativeOpen(workspace: Long, collection: String): String?
        @JvmStatic private external fun nativeSearchForAudio(workspace: Long, title: String): String?
        @JvmStatic private external fun nativeSearchFinished(workspace: Long, request: String, result: String): String?
        @JvmStatic private external fun nativeSearchFailed(workspace: Long, request: String, message: String)
        @JvmStatic private external fun nativeCancelSearch(workspace: Long)
        @JvmStatic private external fun nativeSelect(workspace: Long, id: String, reload: Boolean): String?
        @JvmStatic private external fun nativeRetryLyrics(workspace: Long): String?
        @JvmStatic private external fun nativeLyricsLoaded(workspace: Long, request: String, document: String)
        @JvmStatic private external fun nativeLyricsFailed(workspace: Long, request: String, message: String)
        @JvmStatic private external fun nativeToggle(workspace: Long, id: String)
        @JvmStatic private external fun nativeSelectAll(workspace: Long)
        @JvmStatic private external fun nativeImportLocal(workspace: Long, title: String, text: String): Boolean
        @JvmStatic private external fun nativeText(workspace: Long, tab: Int): String
        @JvmStatic private external fun nativeEdit(workspace: Long, tab: Int, text: String)
        @JvmStatic private external fun nativeSetOptions(workspace: Long, options: String)
        @JvmStatic private external fun nativeSetOutputDirectory(workspace: Long, directory: String)
        @JvmStatic private external fun nativeCopyPreview(workspace: Long): String
        @JvmStatic private external fun nativeExportCurrent(workspace: Long): String?
        @JvmStatic private external fun nativeExportChecked(workspace: Long): String?
        @JvmStatic private external fun nativeExportSucceeded(workspace: Long, job: String, document: String, file: String?)
        @JvmStatic private external fun nativeExportFailed(workspace: Long, job: String, message: String)
        @JvmStatic private external fun nativeRequestStop(workspace: Long)
        @JvmStatic private external fun nativeEndExport(workspace: Long)
        @JvmStatic private external fun nativeShowNotice(workspace: Long, text: String)
        @JvmStatic private external fun nativeDismissNotice(workspace: Long)
        @JvmStatic private external fun nativeSongArtist(song: String): String
        @JvmStatic private external fun nativeSongDetail(song: String): String
    }
}
