package io.github.backspace135.musicunlock.core

import android.content.Intent
import android.database.Cursor
import android.database.MatrixCursor
import android.net.Uri
import android.os.CancellationSignal
import android.os.ParcelFileDescriptor
import android.provider.DocumentsContract
import android.provider.DocumentsContract.Document
import android.provider.DocumentsContract.Root
import android.provider.DocumentsProvider
import android.webkit.MimeTypeMap
import androidx.test.platform.app.InstrumentationRegistry
import java.io.File
import java.io.FileNotFoundException

/**
 * A document provider over `cacheDir/test-documents`, standing in for a file manager. Document IDs are paths
 * relative to that folder. Like ExternalStorageProvider it renames a new document whose name is taken
 * ("name (1).ext"), and appends the MIME type's extension when the name lacks it. Documents named "*.pipe.*" are
 * read through a pipe, like a cloud provider's, so readers cannot map them.
 */
class TestDocumentsProvider : DocumentsProvider() {
    private val base: File get() = rootFolder(checkNotNull(context))

    override fun onCreate(): Boolean = true

    override fun queryRoots(projection: Array<out String>?): Cursor =
        MatrixCursor(projection ?: arrayOf(Root.COLUMN_ROOT_ID, Root.COLUMN_DOCUMENT_ID, Root.COLUMN_TITLE)).apply {
            newRow().add(Root.COLUMN_ROOT_ID, "root").add(Root.COLUMN_DOCUMENT_ID, "").add(Root.COLUMN_TITLE, "测试")
        }

    override fun queryDocument(documentId: String, projection: Array<out String>?): Cursor =
        MatrixCursor(projection ?: defaultColumns).apply { addDocument(this, documentId) }

    override fun queryChildDocuments(parentDocumentId: String, projection: Array<out String>?,
                                     sortOrder: String?): Cursor {
        val folder = file(parentDocumentId)
        if (folder.name == "unreadable") throw SecurityException("unreadable")
        return MatrixCursor(projection ?: defaultColumns).apply {
            folder.listFiles()?.forEach { addDocument(this, id(it)) }
        }
    }

    override fun isChildDocument(parentDocumentId: String, documentId: String): Boolean =
        parentDocumentId.isEmpty() || documentId.startsWith("$parentDocumentId/")

    override fun openDocument(documentId: String, mode: String, signal: CancellationSignal?): ParcelFileDescriptor {
        val file = file(documentId)
        if (!file.name.contains(".pipe.") || mode != "r") {
            return ParcelFileDescriptor.open(file, ParcelFileDescriptor.parseMode(mode))
        }
        val (read, write) = ParcelFileDescriptor.createPipe()
        Thread {
            ParcelFileDescriptor.AutoCloseOutputStream(write).use { output -> file.inputStream().use { it.copyTo(output) } }
        }.start()
        return read
    }

    override fun createDocument(parentDocumentId: String, mimeType: String, displayName: String): String {
        val folder = file(parentDocumentId)
        val extension = MimeTypeMap.getSingleton().getExtensionFromMimeType(mimeType)
        var name = displayName
        if (extension != null && !name.endsWith(".$extension", ignoreCase = true)) name += ".$extension"
        val stem = name.substringBeforeLast('.')
        val suffix = name.substringAfterLast('.', "")
        var target = File(folder, name)
        var counter = 1
        while (target.exists()) target = File(folder, "$stem (${counter++})${if (suffix.isEmpty()) "" else ".$suffix"}")
        if (mimeType == Document.MIME_TYPE_DIR) target.mkdirs() else target.createNewFile()
        return id(target)
    }

    override fun deleteDocument(documentId: String) {
        file(documentId).deleteRecursively()
    }

    private fun file(id: String) = if (id.isEmpty()) base else File(base, id)
    private fun id(file: File) = file.relativeTo(base).path

    private fun addDocument(cursor: MatrixCursor, id: String) {
        val file = file(id)
        if (!file.exists()) throw FileNotFoundException(id)
        val folder = file.isDirectory
        val flags = if (folder) Document.FLAG_DIR_SUPPORTS_CREATE else Document.FLAG_SUPPORTS_DELETE or Document.FLAG_SUPPORTS_WRITE
        cursor.newRow()
            .add(Document.COLUMN_DOCUMENT_ID, id)
            .add(Document.COLUMN_DISPLAY_NAME, if (id.isEmpty()) "测试文件夹" else file.name)
            .add(Document.COLUMN_MIME_TYPE, if (folder) Document.MIME_TYPE_DIR else "application/octet-stream")
            .add(Document.COLUMN_SIZE, if (folder) null else file.length())
            .add(Document.COLUMN_FLAGS, flags)
    }

    companion object {
        /** `<test package>.documents`, as in androidTest/AndroidManifest.xml; the test package carries the branch suffix. */
        val AUTHORITY: String by lazy { "${InstrumentationRegistry.getInstrumentation().context.packageName}.documents" }

        private val defaultColumns = arrayOf(Document.COLUMN_DOCUMENT_ID, Document.COLUMN_DISPLAY_NAME,
            Document.COLUMN_MIME_TYPE, Document.COLUMN_SIZE, Document.COLUMN_FLAGS)

        fun rootFolder(context: android.content.Context) = File(context.cacheDir, "test-documents")

        /** A fresh, empty documents folder. */
        fun reset(context: android.content.Context): File =
            rootFolder(context).apply { deleteRecursively(); mkdirs() }

        fun documentUri(relativePath: String): Uri = DocumentsContract.buildDocumentUri(AUTHORITY, relativePath)

        /** The tree URI of a folder, as OpenDocumentTree would return it. */
        fun treeUri(relativePath: String): Uri = DocumentsContract.buildTreeDocumentUri(AUTHORITY, relativePath)

        /** A folder as the user would pick it for output: created, with a read / write grant like a picker's. */
        fun pickedFolder(relativePath: String): OutputFolder.Picked {
            val context = Fixtures.context
            File(rootFolder(context), relativePath).mkdirs()
            val tree = treeUri(relativePath)
            context.grantUriPermission(context.packageName, tree, Intent.FLAG_GRANT_READ_URI_PERMISSION or
                Intent.FLAG_GRANT_WRITE_URI_PERMISSION or Intent.FLAG_GRANT_PREFIX_URI_PERMISSION)
            return OutputFolder.Picked(tree)
        }
    }
}
