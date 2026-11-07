package io.github.backspace135.musicunlock.core

import android.Manifest
import android.content.Intent
import android.content.pm.PackageManager
import android.net.Uri
import android.os.Build
import android.os.Environment
import android.provider.DocumentsContract
import androidx.core.content.ContextCompat

/**
 * Where converted audio or exported lyrics are written. Saved in settings as [OutputFolders.settingValue]:
 * an empty string for the default, otherwise the picked folder's tree URI (the Windows `outputDirectory` key).
 */
sealed interface OutputFolder {
    /** Converted audio's default: `Music/音匣`, through MediaStore. Players find the files right away. */
    data object DefaultMusic : OutputFolder

    /** Exported lyrics' default: `Download/音匣/歌词` (Android keeps non-media files to Download / Documents). */
    data object DefaultLyrics : OutputFolder

    /** A folder picked with `ActivityResultContracts.OpenDocumentTree()`; see [OutputFolders.remember]. */
    data class Picked(val treeUri: Uri) : OutputFolder
}

/** A folder's name and where it is, for the settings row: "音匣" / "内部存储/Music". */
data class FolderLabel(val name: String, val location: String)

/** Helpers for [OutputFolder]: settings form, labels, permissions and checks. Quick; any thread. */
object OutputFolders {
    private val context get() = MusicUnlockCore.context

    /** The settings value: "" for a default folder, else the tree URI. */
    fun settingValue(folder: OutputFolder): String = (folder as? OutputFolder.Picked)?.treeUri?.toString().orEmpty()

    /** The folder saved as [value]; [default] for an empty value. Check it with [isUsable] before use. */
    fun fromSetting(value: String?, default: OutputFolder): OutputFolder =
        if (value.isNullOrEmpty()) default else OutputFolder.Picked(Uri.parse(value))

    /**
     * Keeps read / write access to a folder the user just picked across restarts, and gives up the grant of
     * [previous] if it was another picked folder. Call it with the URI from `OpenDocumentTree`.
     */
    fun remember(treeUri: Uri, previous: OutputFolder? = null): OutputFolder.Picked {
        val flags = Intent.FLAG_GRANT_READ_URI_PERMISSION or Intent.FLAG_GRANT_WRITE_URI_PERMISSION
        context.contentResolver.takePersistableUriPermission(treeUri, flags)
        if (previous is OutputFolder.Picked && previous.treeUri != treeUri) {
            try {
                context.contentResolver.releasePersistableUriPermission(previous.treeUri, flags)
            } catch (_: SecurityException) {
                // Already gone.
            }
        }
        return OutputFolder.Picked(treeUri)
    }

    /**
     * Whether files can be written there now. A picked folder stops being usable when it is deleted or its grant
     * is revoked: fall back to the default and tell the user. Runs a quick provider query; avoid the main thread
     * in loops.
     */
    fun isUsable(folder: OutputFolder): Boolean = when (folder) {
        is OutputFolder.Picked -> FilePublisher.treeIsWritable(folder.treeUri)
        else -> requiredPermissions(folder).all {
            ContextCompat.checkSelfPermission(context, it) == PackageManager.PERMISSION_GRANTED
        }
    }

    /**
     * Runtime permissions to request before writing to [folder]: WRITE_EXTERNAL_STORAGE for the default folders on
     * Android 9 and older (API 26–28), nothing otherwise.
     */
    fun requiredPermissions(folder: OutputFolder): List<String> =
        if (folder !is OutputFolder.Picked && Build.VERSION.SDK_INT < Build.VERSION_CODES.Q) {
            listOf(Manifest.permission.WRITE_EXTERNAL_STORAGE)
        } else {
            emptyList()
        }

    /** The folder's name and location for display. */
    fun label(folder: OutputFolder): FolderLabel = when (folder) {
        OutputFolder.DefaultMusic -> FolderLabel("音匣", "内部存储/${Environment.DIRECTORY_MUSIC}")
        OutputFolder.DefaultLyrics -> FolderLabel("歌词", "内部存储/${Environment.DIRECTORY_DOWNLOADS}/音匣")
        is OutputFolder.Picked -> FilePublisher.treeLabel(folder.treeUri)
    }

    /** "Music/音匣/" style path for messages such as "请在文件管理中打开 {路径}". */
    fun path(folder: OutputFolder): String = label(folder).let { "${it.location}/${it.name}" }

    /**
     * An intent that shows the folder in the system file manager ("在文件管理中查看"). Starting it can still fail
     * with ActivityNotFoundException on devices whose file manager does not take folder links: then show the
     * [path] instead.
     */
    fun viewIntent(folder: OutputFolder): Intent {
        val uri = when (folder) {
            is OutputFolder.Picked ->
                DocumentsContract.buildDocumentUriUsingTree(folder.treeUri, DocumentsContract.getTreeDocumentId(folder.treeUri))
            else -> DocumentsContract.buildDocumentUri(
                FilePublisher.EXTERNAL_STORAGE_AUTHORITY, "primary:${FilePublisher.relativePath(folder).trimEnd('/')}")
        }
        return Intent(Intent.ACTION_VIEW)
            .setDataAndType(uri, DocumentsContract.Document.MIME_TYPE_DIR)
            .addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION or Intent.FLAG_ACTIVITY_NEW_TASK)
    }
}
