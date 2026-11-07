package io.github.backspace135.musicunlock.core

import android.net.Uri
import android.provider.OpenableColumns
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext

/** A local LRC / TXT read for [LyricWorkspace.importLocal]: [title] is the file name without its extension. */
data class LocalLyricFile(val title: String, val text: String)

/** Opening local lyric files ("打开本地歌词"), with the core's read_lyric_file rules. */
object LocalLyrics {
    /** Whether a picked file is a lyric file the workspace opens: ".lrc" or ".txt". */
    fun isLyricFile(name: String): Boolean = name.substringAfterLast('.', "").lowercase() in setOf("lrc", "txt")

    /** The display name of a picked document, to check with [isLyricFile]. */
    suspend fun displayName(uri: Uri): String = withContext(Dispatchers.IO) { name(uri) }

    /**
     * Reads and decodes a picked file (UTF-8, UTF-16 with BOM, GB18030 / GBK; at most 5 MB). Runs on
     * [Dispatchers.IO]; throws [LyricException] with the message to show (`workspace.showNotice(error.message)`).
     */
    suspend fun read(uri: Uri): LocalLyricFile = withContext(Dispatchers.IO) {
        val bytes = try {
            MusicUnlockCore.context.contentResolver.openInputStream(uri)?.use { input ->
                // One byte over the core's limit is enough for it to say the file is too large.
                val limit = 5_000_001
                val buffer = java.io.ByteArrayOutputStream()
                val chunk = ByteArray(1 shl 16)
                while (buffer.size() < limit) {
                    val count = input.read(chunk, 0, minOf(chunk.size, limit - buffer.size()))
                    if (count < 0) break
                    buffer.write(chunk, 0, count)
                }
                buffer.toByteArray()
            } ?: throw LyricException("无法读取歌词文件。")
        } catch (error: LyricException) {
            throw error
        } catch (_: Exception) {
            throw LyricException("无法读取歌词文件。")
        }
        LocalLyricFile(name(uri).substringBeforeLast('.'), Lyrics.decodeLyricFile(bytes))
    }

    private fun name(uri: Uri): String = try {
        MusicUnlockCore.context.contentResolver.query(uri, arrayOf(OpenableColumns.DISPLAY_NAME), null, null, null)
            ?.use { if (it.moveToFirst()) it.getString(0) else null }
    } catch (_: Exception) {
        null
    } ?: uri.lastPathSegment?.substringAfterLast('/').orEmpty()
}
