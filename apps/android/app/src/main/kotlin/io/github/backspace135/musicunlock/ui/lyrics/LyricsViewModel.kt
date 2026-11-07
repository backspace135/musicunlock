package io.github.backspace135.musicunlock.ui.lyrics

import android.content.Context
import android.net.Uri
import androidx.lifecycle.ViewModel
import io.github.backspace135.musicunlock.core.LyricWorkspace
import io.github.backspace135.musicunlock.core.MusicCollection
import io.github.backspace135.musicunlock.core.MusicProvider
import io.github.backspace135.musicunlock.core.SearchKind

/**
 * 界面只通过这里转发给 [LyricsSession]。工作区不跟页面走，所以这个 ViewModel 被清掉也不影响导出和编辑。
 */
class LyricsViewModel : ViewModel() {
    private val session = LyricsSession

    val workspace: LyricWorkspace get() = session.workspace
    val hasKeywordParent: Boolean get() = session.hasKeywordParent

    fun setQuery(value: String) {
        if (value != workspace.query) workspace.query = value
    }

    fun setProvider(provider: MusicProvider) {
        if (provider != workspace.provider) workspace.provider = provider
    }

    fun setKind(kind: SearchKind) {
        if (kind != workspace.kind) workspace.kind = kind
    }

    fun search() = session.search()

    fun loadMore() = session.search(more = true)

    fun cancelSearch() = session.cancelSearch()

    fun open(collection: MusicCollection) = session.open(collection)

    fun returnToKeyword() = session.returnToKeyword()

    fun select(songId: String, reload: Boolean = false) = session.select(songId, reload)

    fun toggle(songId: String) = workspace.toggle(songId)

    fun selectAll() = workspace.selectAll()

    fun dismissNotice() = workspace.dismissNotice()

    fun importLocal(uri: Uri, unsupported: String, onOpened: (String) -> Unit) =
        session.importLocal(uri, unsupported, onOpened)

    fun exportChecked(context: Context, folderFallback: String) = session.export(checked = true, context, folderFallback)

    fun exportCurrent(context: Context, folderFallback: String) = session.export(checked = false, context, folderFallback)

    fun stopExport() = session.stopExport()

    fun copyLyrics(context: Context) = session.copyLyrics(context)

    fun notifyFolderFallback(notice: String) = session.notifyFolderFallback(notice)
}
