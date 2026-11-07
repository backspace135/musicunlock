package io.github.backspace135.musicunlock.ui.lyrics

import android.content.ClipData
import android.content.ClipboardManager
import android.content.Context
import android.net.Uri
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import io.github.backspace135.musicunlock.core.AccountStore
import io.github.backspace135.musicunlock.core.CancelToken
import io.github.backspace135.musicunlock.core.CoreCancelledException
import io.github.backspace135.musicunlock.core.CoreException
import io.github.backspace135.musicunlock.core.LocalLyrics
import io.github.backspace135.musicunlock.core.LyricException
import io.github.backspace135.musicunlock.core.LyricOptions
import io.github.backspace135.musicunlock.core.LyricSettings
import io.github.backspace135.musicunlock.core.LyricWorkspace
import io.github.backspace135.musicunlock.core.Lyrics
import io.github.backspace135.musicunlock.core.LyricsRequest
import io.github.backspace135.musicunlock.core.MusicApi
import io.github.backspace135.musicunlock.core.MusicCollection
import io.github.backspace135.musicunlock.core.MusicProvider
import io.github.backspace135.musicunlock.core.OutputFolders
import io.github.backspace135.musicunlock.core.ProcessingService
import io.github.backspace135.musicunlock.core.SearchKind
import io.github.backspace135.musicunlock.core.SearchRequest
import kotlinx.coroutines.MainScope
import kotlinx.coroutines.launch

/**
 * 歌词工作区和它发出的请求。进程级持有：开着「不保留活动」离开界面后，编辑内容和导出还要在。
 * [LyricWorkspace] 只在主线程上用；网络在 [MusicApi] 里切到后台。
 */
internal object LyricsSession {
    val workspace = LyricWorkspace()
    private val scope = MainScope()
    private val api = MusicApi.shared
    private var searchToken = CancelToken()
    private var lyricsToken = CancelToken()
    private var exportToken = CancelToken()

    /** 最近一次关键词搜索。从列表点进专辑 / 歌单时，用它做返回。 */
    private var keywordSearch: KeywordSearch? = null

    /** 非空表示当前列表是从关键词结果点进去的合集，系统返回要重新搜这三项。 */
    private var returnTarget by mutableStateOf<KeywordSearch?>(null)

    val hasKeywordParent: Boolean get() = returnTarget != null

    init {
        workspace.options = LyricSettings.values.value.options
        scope.launch {
            ProcessingService.stopRequests.collect { task ->
                if (task == ProcessingService.Task.LYRICS) stopExport()
            }
        }
    }

    fun search(more: Boolean = false) {
        val request = workspace.search(more) ?: return
        if (!request.more) {
            returnTarget = null
            if (request.link == null) keywordSearch = KeywordSearch(request.text, request.provider, request.kind)
        }
        launchSearch(request)
    }

    fun open(collection: MusicCollection) {
        val keyword = keywordSearch
        val request = workspace.open(collection) ?: return
        returnTarget = keyword
        launchSearch(request)
    }

    /** 用记住的关键词、平台、类型再搜一次。没有上一层时返回 false。 */
    fun returnToKeyword(): Boolean {
        val target = returnTarget ?: return false
        if (workspace.exporting) return false
        returnTarget = null
        workspace.provider = target.provider
        workspace.kind = target.kind
        workspace.query = target.query
        search()
        return true
    }

    fun cancelSearch() {
        searchToken.cancel()
        workspace.cancelSearch()
    }

    fun select(songId: String, reload: Boolean = false) {
        workspace.select(songId, reload)?.let(::fetch)
    }

    fun setOptions(options: LyricOptions) {
        val fixed = if (options.includePinyin && !Lyrics.pinyinSupported) options.copy(includePinyin = false) else options
        LyricSettings.setOptions(fixed)
        workspace.options = fixed
    }

    fun setOutputFolder(uri: Uri) {
        if (workspace.exporting) return
        val current = LyricSettings.values.value.outputFolder
        LyricSettings.setOutputFolder(OutputFolders.remember(uri, current))
    }

    /** 选过的文件夹失效时退回默认位置。真的退回了才提示。 */
    fun notifyFolderFallback(notice: String) {
        if (LyricSettings.fallBackIfUnusable()) workspace.showNotice(notice)
    }

    fun importLocal(uri: Uri, unsupported: String, onOpened: (String) -> Unit) {
        if (workspace.exporting) return
        scope.launch {
            val name = LocalLyrics.displayName(uri)
            if (!LocalLyrics.isLyricFile(name)) {
                workspace.showNotice(unsupported)
                return@launch
            }
            try {
                val file = LocalLyrics.read(uri)
                if (workspace.importLocal(file.title, file.text)) {
                    cancelLyrics()
                    workspace.selected?.id?.let(onOpened)
                }
            } catch (error: LyricException) {
                workspace.showNotice(error.message)
            }
        }
    }

    fun export(checked: Boolean, context: Context, folderFallback: String) {
        notifyFolderFallback(folderFallback)
        workspace.options = LyricSettings.values.value.options
        val batch = (if (checked) workspace.exportChecked() else workspace.exportCurrent()) ?: return
        cancelLyrics()
        exportToken.cancel()
        exportToken = CancelToken()
        val token = exportToken
        val app = context.applicationContext
        val folder = LyricSettings.values.value.outputFolder
        val cookies = MusicProvider.entries.associateWith { AccountStore.cookieHeader(it) }
        val task = ProcessingService.Task.LYRICS
        scope.launch {
            try {
                try {
                    ProcessingService.start(app, task, ProcessingService.Progress(workspace.progressText, 0, batch.jobs.size))
                } catch (_: RuntimeException) {
                    // 通知发不出去也继续导出，结束时再收起服务。
                }
                for (job in batch.jobs) {
                    if (token.isCancelled) break
                    try {
                        val outcome = api.exportSong(job, batch.options, folder, cookies.getValue(job.song.provider), token)
                        workspace.exportSucceeded(job, outcome)
                    } catch (_: CoreCancelledException) {
                        break
                    } catch (error: CoreException) {
                        workspace.exportFailed(job, error.message)
                    }
                    ProcessingService.update(task, ProcessingService.Progress(workspace.progressText, workspace.progress, workspace.exportTotal))
                }
            } finally {
                workspace.endExport()
                ProcessingService.stop(app, task)
            }
        }
    }

    fun stopExport() {
        workspace.requestStop()
        exportToken.cancel()
    }

    fun copyLyrics(context: Context) {
        val text = workspace.copyPreview()
        if (text.isNotEmpty()) copyText(context, text)
    }

    private fun launchSearch(request: SearchRequest) {
        searchToken.cancel()
        searchToken = CancelToken()
        if (!request.more) cancelLyrics()
        val token = searchToken
        scope.launch {
            try {
                val result = api.runSearch(request, AccountStore.cookieHeader(request.provider), token)
                workspace.searchFinished(request, result)?.let(::fetch)
            } catch (_: CoreCancelledException) {
            } catch (error: CoreException) {
                workspace.searchFailed(request, error.message)
            }
        }
    }

    private fun fetch(request: LyricsRequest) {
        cancelLyrics()
        val token = lyricsToken
        scope.launch {
            try {
                val document = api.lyrics(request.song, AccountStore.cookieHeader(request.song.provider), token)
                workspace.lyricsLoaded(request, document)
            } catch (_: CoreCancelledException) {
            } catch (error: CoreException) {
                workspace.lyricsFailed(request, error.message)
            }
        }
    }

    private fun cancelLyrics() {
        lyricsToken.cancel()
        lyricsToken = CancelToken()
    }

    private data class KeywordSearch(val query: String, val provider: MusicProvider, val kind: SearchKind)
}

internal fun copyText(context: Context, text: String) {
    val clipboard = context.getSystemService(Context.CLIPBOARD_SERVICE) as ClipboardManager
    clipboard.setPrimaryClip(ClipData.newPlainText("lyrics", text))
}

/** 编辑框把换行统一成 `\n`，和核心、Windows 一致。 */
internal fun lyricNewlines(text: String): String = text.replace("\r\n", "\n").replace('\r', '\n')
