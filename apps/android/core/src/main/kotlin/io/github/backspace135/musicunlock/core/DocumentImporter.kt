package io.github.backspace135.musicunlock.core

import android.content.ContentResolver
import android.content.Intent
import android.net.Uri
import android.os.Build
import android.os.ParcelFileDescriptor
import android.provider.DocumentsContract
import android.provider.OpenableColumns
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import org.json.JSONObject
import java.io.File
import java.io.FileNotFoundException
import java.security.MessageDigest
import java.util.concurrent.ConcurrentHashMap

/** A file found by an import, for [AudioQueue.add]. [path] is the core's identity of the file (see [ImportScan]). */
data class ImportedFile(val path: String, val size: Long)

/**
 * The result of an import, the core's `ImportScan`: supported files in natural order and how many other files were
 * skipped. Pass it to [AudioQueue.add], which drops files already queued and returns the notice to show.
 *
 * The core works with paths while Android hands out `content://` URIs, so each document gets a stable path derived
 * from its URI: `<cache>/musicunlock/<run>/imports/<hash of the URI>/<display name>`. Adding the same URI twice
 * gives the same path, which the queue reports as already queued; the display name keeps the row's title and
 * format. [AudioConverter] maps the path back to the URI and reads it through a file descriptor.
 */
data class ImportScan(val files: List<ImportedFile>, val skipped: Int)

/**
 * Turns what the user picked or shared into an [ImportScan], with the rules of the core's import.cpp. Every
 * function runs on [Dispatchers.IO]; call them from a coroutine (e.g. `viewModelScope.launch`).
 */
object DocumentImporter {
    private val resolver: ContentResolver get() = MusicUnlockCore.context.contentResolver

    /**
     * Files picked with `ActivityResultContracts.OpenMultipleDocuments()`. Launch it for all MIME types, since
     * encrypted formats have none (see the README); unsupported files are counted as skipped.
     */
    suspend fun scanDocuments(uris: List<Uri>): ImportScan = withContext(Dispatchers.IO) {
        val found = mutableListOf<Found>()
        var skipped = 0
        for (uri in uris) {
            val (name, size) = describe(uri)
            if (AudioFormats.isSupportedFileName(name)) found += Found(name, uri, name, size) else skipped++
        }
        scan(found, skipped)
    }

    /**
     * A folder picked with `ActivityResultContracts.OpenDocumentTree()`: every supported file below it,
     * recursively. Like import.cpp, names starting with "." are skipped (folders are not entered), unreadable
     * folders are ignored, and other files are counted as skipped.
     */
    suspend fun scanTree(treeUri: Uri): ImportScan = withContext(Dispatchers.IO) {
        val found = mutableListOf<Found>()
        var skipped = 0
        val rootId = DocumentsContract.getTreeDocumentId(treeUri)
        val rootName = describe(DocumentsContract.buildDocumentUriUsingTree(treeUri, rootId)).first
        val pending = ArrayDeque(listOf(rootId to rootName))
        while (pending.isNotEmpty()) {
            val (folderId, folderPath) = pending.removeFirst()
            val children = DocumentsContract.buildChildDocumentsUriUsingTree(treeUri, folderId)
            val cursor = try {
                resolver.query(children, childColumns, null, null, null)
            } catch (_: Exception) {
                null  // unreadable folders are ignored
            } ?: continue
            cursor.use {
                while (it.moveToNext()) {
                    val id = it.getString(0) ?: continue
                    val name = it.getString(1) ?: continue
                    if (name.startsWith(".")) continue
                    val logical = "$folderPath/$name"
                    if (it.getString(2) == DocumentsContract.Document.MIME_TYPE_DIR) {
                        pending.addLast(id to logical)
                    } else if (AudioFormats.isSupportedFileName(name)) {
                        val size = if (it.isNull(3)) 0 else it.getLong(3)
                        found += Found(logical, DocumentsContract.buildDocumentUriUsingTree(treeUri, id), name, size)
                    } else {
                        skipped++
                    }
                }
            }
        }
        scan(found, skipped)
    }

    /**
     * Files shared to the app (`ACTION_SEND`, `ACTION_SEND_MULTIPLE`) or opened with it (`ACTION_VIEW`); null when
     * the intent carries none. The sender's read grant ends with the receiving activity, so supported files are
     * copied into the app cache first; the copy is deleted once converted, removed from the queue, or at the next
     * start.
     */
    suspend fun scanIntent(intent: Intent): ImportScan? = withContext(Dispatchers.IO) {
        val uris = sharedUris(intent)
        if (uris.isEmpty()) return@withContext null
        val found = mutableListOf<Found>()
        var skipped = 0
        for (uri in uris) {
            val (name, size) = describe(uri)
            if (!AudioFormats.isSupportedFileName(name)) {
                skipped++
                continue
            }
            val copy = File(ImportedSources.pathFor(uri, name))
            val copied = try {
                copy.parentFile?.mkdirs()
                resolver.openInputStream(uri)?.use { input -> copy.outputStream().use { input.copyTo(it) } } != null
            } catch (_: Exception) {
                false
            }
            // Without a copy the conversion still tries the URI, and reports why it cannot read it.
            if (!copied) copy.delete()
            found += Found(name, uri, name, if (copied) copy.length() else size, copied)
        }
        scan(found, skipped)
    }

    /**
     * Real directories, scanned by the core's `scan_import`: dot names are skipped, unreadable folders are
     * ignored, only supported extensions are kept, and the result is naturally sorted. Missing directories are
     * left out, so they are not counted as skipped. The originals stay where they are; the queue only remembers
     * them, and clearing a job does not delete the file.
     */
    suspend fun scanDirectories(directories: List<File>): ImportScan = withContext(Dispatchers.IO) {
        val existing = directories.filter { it.isDirectory }
        if (existing.isEmpty()) return@withContext ImportScan(emptyList(), 0)
        val json = JSONObject(nativeScanImport(existing.map { it.absolutePath }.toTypedArray()))
        val array = json.getJSONArray("files")
        val files = List(array.length()) { index ->
            val item = array.getJSONObject(index)
            val path = item.getString("path")
            val file = File(path)
            // The core's path is the queue identity. deleteOnRelease is false: this is the user's file.
            ImportedSources.remember(path, ImportedSources.Source(Uri.fromFile(file), file, file.name, deleteOnRelease = false))
            ImportedFile(path, item.getLong("size"))
        }
        ImportScan(files, json.getInt("skipped"))
    }

    @JvmStatic private external fun nativeScanImport(directories: Array<String>): String

    private class Found(val logical: String, val uri: Uri, val name: String, val size: Long, val copied: Boolean = false)

    private val childColumns = arrayOf(
        DocumentsContract.Document.COLUMN_DOCUMENT_ID,
        DocumentsContract.Document.COLUMN_DISPLAY_NAME,
        DocumentsContract.Document.COLUMN_MIME_TYPE,
        DocumentsContract.Document.COLUMN_SIZE,
    )

    /** Like scan_import: deduplicated, in natural order of the path (here the path within the picked folder). */
    private fun scan(found: List<Found>, skipped: Int): ImportScan {
        val files = AudioFormats.sortedNaturally(found) { it.logical }.mapNotNull { item ->
            val path = ImportedSources.pathFor(item.uri, item.name)
            val file = if (item.copied) File(path) else null
            ImportedSources.remember(path, ImportedSources.Source(item.uri, file, item.name))
            ImportedFile(path, item.size)
        }.distinctBy { it.path }
        return ImportScan(files, skipped)
    }

    private fun describe(uri: Uri): Pair<String, Long> {
        var name: String? = null
        var size = -1L
        try {
            resolver.query(uri, arrayOf(OpenableColumns.DISPLAY_NAME, OpenableColumns.SIZE), null, null, null)?.use {
                if (it.moveToFirst()) {
                    if (!it.isNull(0)) name = it.getString(0)
                    if (!it.isNull(1)) size = it.getLong(1)
                }
            }
        } catch (_: Exception) {
            // Fall back to the URI's last segment; the conversion reports an unreadable file.
        }
        if (size < 0) {
            size = try {
                resolver.openFileDescriptor(uri, "r")?.use { it.statSize } ?: 0
            } catch (_: Exception) {
                0
            }
        }
        val fallback = uri.lastPathSegment?.substringAfterLast('/') ?: "音频"
        return (name ?: fallback) to size.coerceAtLeast(0)
    }

    private fun sharedUris(intent: Intent): List<Uri> {
        val uris = mutableListOf<Uri>()
        when (intent.action) {
            Intent.ACTION_SEND -> intent.stream()?.let(uris::add)
            Intent.ACTION_SEND_MULTIPLE -> uris += intent.streams()
            Intent.ACTION_VIEW -> intent.data?.let(uris::add)
        }
        if (uris.isEmpty()) {
            val clip = intent.clipData
            for (index in 0 until (clip?.itemCount ?: 0)) clip?.getItemAt(index)?.uri?.let(uris::add)
        }
        return uris.filter { it.scheme == ContentResolver.SCHEME_CONTENT || it.scheme == ContentResolver.SCHEME_FILE }
            .distinct()
    }

    @Suppress("DEPRECATION")
    private fun Intent.stream(): Uri? =
        if (Build.VERSION.SDK_INT >= 33) getParcelableExtra(Intent.EXTRA_STREAM, Uri::class.java)
        else getParcelableExtra(Intent.EXTRA_STREAM)

    @Suppress("DEPRECATION")
    private fun Intent.streams(): List<Uri> =
        (if (Build.VERSION.SDK_INT >= 33) getParcelableArrayListExtra(Intent.EXTRA_STREAM, Uri::class.java)
        else getParcelableArrayListExtra(Intent.EXTRA_STREAM)).orEmpty()
}

/** The documents behind import paths, for the converter. In memory, like the queue that holds the paths. */
internal object ImportedSources {
    /**
     * [file] is either the cached copy of a shared file, or a download read in place. [deleteOnRelease] is
     * true only for copies this process made; a file the user already had is never deleted.
     */
    class Source(val uri: Uri, val file: File?, val name: String, val deleteOnRelease: Boolean = true) {
        /** Set once a queue holds the path; only such sources are forgotten when no queue holds them any more. */
        @Volatile
        var queued = false
    }

    private val sources = ConcurrentHashMap<String, Source>()

    /** The stable path of a document: same URI, same path. */
    fun pathFor(uri: Uri, name: String): String {
        val digest = MessageDigest.getInstance("SHA-256").digest(uri.toString().toByteArray())
        val hash = digest.take(12).joinToString("") { "%02x".format(it) }
        val safe = name.replace('/', '_').replace('\u0000', '_').ifEmpty { "音频" }
        return File(CacheFolders.imports, "$hash/$safe").path
    }

    fun remember(path: String, source: Source) {
        sources[path] = source
    }

    fun find(path: String): Source? = sources[path]

    fun open(source: Source): ParcelFileDescriptor = try {
        source.file?.takeIf { it.isFile }?.let { ParcelFileDescriptor.open(it, ParcelFileDescriptor.MODE_READ_ONLY) }
            ?: MusicUnlockCore.context.contentResolver.openFileDescriptor(source.uri, "r")
            ?: throw FileNotFoundException()
    } catch (error: Exception) {
        // SecurityException when the grant is gone, FileNotFoundException when the document was moved or deleted.
        throw DecodeException(DecodeException.Kind.INVALID, "无法读取文件（文件已被移动、删除，或不再允许访问，请重新添加）")
    }

    fun markQueued(paths: Collection<String>) {
        for (path in paths) sources[path]?.queued = true
    }

    /** Forgets queued imports that no queue holds any more, deleting their cached copies. */
    fun retainOnly(paths: Set<String>) {
        for ((path, source) in sources) {
            if (source.queued && path !in paths && sources.remove(path, source)) source.deleteCopy()
        }
    }

    /** Deletes the cached copy of a converted shared file; a download read in place is left alone. */
    fun releaseCopy(path: String) {
        sources[path]?.deleteCopy()
    }

    private fun Source.deleteCopy() {
        if (!deleteOnRelease) return
        file?.let { it.delete(); it.parentFile?.delete() }
    }
}
