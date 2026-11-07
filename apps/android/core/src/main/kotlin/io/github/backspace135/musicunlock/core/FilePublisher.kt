package io.github.backspace135.musicunlock.core

import android.content.ContentResolver
import android.content.ContentValues
import android.media.MediaScannerConnection
import android.net.Uri
import android.os.Build
import android.os.Environment
import android.provider.DocumentsContract
import android.provider.MediaStore
import android.webkit.MimeTypeMap
import androidx.annotation.RequiresApi
import androidx.core.content.FileProvider
import java.io.File
import java.io.IOException
import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit

/** A file written to its output folder: a content URI for "打开" / "分享", its name and a display path. */
internal class PublishedFile(val uri: Uri, val name: String, val location: String)

/**
 * Moves a file the core wrote into a staging folder to its output folder. The core never overwrites and numbers
 * colliding names " (n)"; MediaStore and document providers rename collisions their own way, so each candidate
 * name from [AudioFormats.numberedFileName] is created and checked, and a renamed one is deleted and the next number
 * tried. Blocking; call on a background thread.
 */
internal object FilePublisher {
    const val EXTERNAL_STORAGE_AUTHORITY = "com.android.externalstorage.documents"

    private val context get() = MusicUnlockCore.context
    private val resolver: ContentResolver get() = context.contentResolver

    /** "Music/音匣/" or "Download/音匣/歌词/": the default folders, relative to the shared storage root. */
    fun relativePath(folder: OutputFolder): String = when (folder) {
        OutputFolder.DefaultLyrics -> "${Environment.DIRECTORY_DOWNLOADS}/音匣/歌词/"
        else -> "${Environment.DIRECTORY_MUSIC}/音匣/"
    }

    /**
     * Publishes [staged] as "<stem>.<extension>", numbered on collision. [tooMany] is the core's message for when
     * every number is taken.
     */
    fun publish(staged: File, stem: String, extension: String, folder: OutputFolder, tooMany: String): PublishedFile {
        val mime = mimeType(extension, audio = folder == OutputFolder.DefaultMusic)
        return when {
            folder is OutputFolder.Picked -> publishToTree(staged, stem, extension, mime, folder.treeUri, tooMany)
            Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q ->
                publishToMediaStore(staged, stem, extension, mime, folder, tooMany)
            else -> publishToLegacyFolder(staged, stem, extension, mime, folder, tooMany)
        }
    }

    /**
     * The MIME type to create a file with. Providers append the type's extension when it does not match the name,
     * so unknown extensions get application/octet-stream, which keeps the name as is; MediaStore's audio collection
     * only takes audio types.
     */
    fun mimeType(extension: String, audio: Boolean): String {
        val known = MimeTypeMap.getSingleton().getMimeTypeFromExtension(extension.lowercase())
        return when {
            known != null && (!audio || known.startsWith("audio/")) -> known
            audio -> "audio/x-${extension.lowercase()}"
            else -> "application/octet-stream"
        }
    }

    @RequiresApi(Build.VERSION_CODES.Q)
    private fun publishToMediaStore(staged: File, stem: String, extension: String, mime: String,
                                    folder: OutputFolder, tooMany: String): PublishedFile {
        val collection = if (folder == OutputFolder.DefaultMusic) {
            MediaStore.Audio.Media.getContentUri(MediaStore.VOLUME_EXTERNAL_PRIMARY)
        } else {
            MediaStore.Downloads.getContentUri(MediaStore.VOLUME_EXTERNAL_PRIMARY)
        }
        val relative = relativePath(folder)
        for (suffix in 0 until AudioFormats.maxNameSuffix) {
            val name = AudioFormats.numberedFileName(stem, extension, suffix)
            // Not IS_PENDING: a pending item lives under a temporary file name, so MediaStore only notices a taken
            // name (and renames it its own way) when it is published. A plain insert is renamed right away.
            val values = ContentValues().apply {
                put(MediaStore.MediaColumns.DISPLAY_NAME, name)
                put(MediaStore.MediaColumns.MIME_TYPE, mime)
                put(MediaStore.MediaColumns.RELATIVE_PATH, relative)
            }
            val uri = try {
                resolver.insert(collection, values)
            } catch (error: Exception) {
                throw CoreException("无法创建输出文件（${error.message.orEmpty()}）")
            } ?: throw CoreException("无法创建输出文件（媒体库拒绝了写入）")
            if (displayName(uri) != name) {
                // Taken: MediaStore renamed it. Try the next number, as the core does.
                resolver.delete(uri, null, null)
                continue
            }
            write(staged, uri)
            return PublishedFile(uri, name, relative + name)
        }
        throw CoreException(tooMany)
    }

    private fun publishToLegacyFolder(staged: File, stem: String, extension: String, mime: String,
                                      folder: OutputFolder, tooMany: String): PublishedFile {
        val relative = relativePath(folder)
        @Suppress("DEPRECATION")
        val directory = File(Environment.getExternalStorageDirectory(), relative)
        if (!directory.isDirectory && !directory.mkdirs()) {
            throw CoreException("无法创建输出文件夹：$relative")
        }
        for (suffix in 0 until AudioFormats.maxNameSuffix) {
            val name = AudioFormats.numberedFileName(stem, extension, suffix)
            val target = File(directory, name)
            val created = try {
                target.createNewFile()  // exclusive, like the core's O_EXCL
            } catch (error: IOException) {
                throw CoreException("无法创建输出文件（${error.message.orEmpty()}）")
            }
            if (!created) continue
            try {
                staged.inputStream().use { input -> target.outputStream().use { input.copyTo(it) } }
            } catch (error: IOException) {
                target.delete()
                throw CoreException("写入输出文件失败（${error.message.orEmpty()}）")
            }
            return PublishedFile(scan(target, mime), name, relative + name)
        }
        throw CoreException(tooMany)
    }

    /** Adds a legacy file to MediaStore and returns its content URI (a FileProvider URI if scanning fails). */
    private fun scan(file: File, mime: String): Uri {
        val done = CountDownLatch(1)
        var scanned: Uri? = null
        MediaScannerConnection.scanFile(context, arrayOf(file.path), arrayOf(mime)) { _, uri ->
            scanned = uri
            done.countDown()
        }
        done.await(10, TimeUnit.SECONDS)
        return scanned ?: FileProvider.getUriForFile(context, "${context.packageName}.musicunlock-files", file)
    }

    private fun publishToTree(staged: File, stem: String, extension: String, mime: String, tree: Uri,
                              tooMany: String): PublishedFile {
        val directory = DocumentsContract.buildDocumentUriUsingTree(tree, DocumentsContract.getTreeDocumentId(tree))
        val label = treeLabel(tree)
        for (suffix in 0 until AudioFormats.maxNameSuffix) {
            val name = AudioFormats.numberedFileName(stem, extension, suffix)
            val document = try {
                DocumentsContract.createDocument(resolver, directory, mime, name)
            } catch (error: Exception) {
                throw CoreException("无法写入输出文件夹：${label.name}（${error.message.orEmpty()}）")
            } ?: throw CoreException("无法写入输出文件夹：${label.name}")
            if (displayName(document) != name) {
                DocumentsContract.deleteDocument(resolver, document)
                continue
            }
            write(staged, document)
            return PublishedFile(document, name, "${label.location}/${label.name}/$name")
        }
        throw CoreException(tooMany)
    }

    private fun write(staged: File, target: Uri) {
        try {
            val output = resolver.openOutputStream(target, "w") ?: throw IOException("无法打开输出文件")
            staged.inputStream().use { input -> output.use { input.copyTo(it, 1 shl 16) } }
        } catch (error: Exception) {
            try {
                if (DocumentsContract.isDocumentUri(context, target)) DocumentsContract.deleteDocument(resolver, target)
                else resolver.delete(target, null, null)
            } catch (_: Exception) {
                // Leave it; the pending MediaStore entry expires on its own.
            }
            throw CoreException("写入输出文件失败（${error.message.orEmpty()}）")
        }
    }

    private fun displayName(uri: Uri): String? = try {
        resolver.query(uri, arrayOf(MediaStore.MediaColumns.DISPLAY_NAME), null, null, null)?.use {
            if (it.moveToFirst()) it.getString(0) else null
        }
    } catch (_: Exception) {
        null
    }

    /** The provider refuses the query without a grant (SecurityException) and for a deleted folder. */
    fun treeIsWritable(tree: Uri): Boolean {
        return try {
            val root = DocumentsContract.buildDocumentUriUsingTree(tree, DocumentsContract.getTreeDocumentId(tree))
            resolver.query(root, arrayOf(DocumentsContract.Document.COLUMN_FLAGS), null, null, null)?.use {
                it.moveToFirst() && it.getInt(0) and DocumentsContract.Document.FLAG_DIR_SUPPORTS_CREATE != 0
            } ?: false
        } catch (_: Exception) {
            false
        }
    }

    fun treeLabel(tree: Uri): FolderLabel {
        val id = DocumentsContract.getTreeDocumentId(tree)
        val root = DocumentsContract.buildDocumentUriUsingTree(tree, id)
        val name = displayName(root) ?: id.substringAfterLast('/').substringAfter(':')
        if (tree.authority == EXTERNAL_STORAGE_AUTHORITY) {
            // "primary:Music/Sub" → 内部存储/Music; other volumes are "<uuid>:path".
            val volume = id.substringBefore(':')
            val parent = id.substringAfter(':', "").substringBeforeLast('/', "")
            val base = if (volume == "primary") "内部存储" else "存储卡"
            return FolderLabel(name, if (parent.isEmpty()) base else "$base/$parent")
        }
        val provider = context.packageManager.resolveContentProvider(tree.authority.orEmpty(), 0)
        return FolderLabel(name, provider?.loadLabel(context.packageManager)?.toString() ?: tree.authority.orEmpty())
    }
}
