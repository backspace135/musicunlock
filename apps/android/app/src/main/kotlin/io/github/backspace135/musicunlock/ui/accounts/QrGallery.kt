package io.github.backspace135.musicunlock.ui.accounts

import android.content.ContentValues
import android.content.Context
import android.graphics.Bitmap
import android.net.Uri
import android.os.Build
import android.os.Environment
import android.provider.MediaStore
import java.io.File

/** The login QR saved under Pictures/音匣, deleted once it is no longer the code on screen. */
internal object QrGallery {
    fun save(context: Context, bitmap: Bitmap): Uri? {
        return if (Build.VERSION.SDK_INT >= 29) insertPending(context, bitmap) else insertLegacy(bitmap)
    }

    fun delete(context: Context, uri: Uri) {
        runCatching {
            if (uri.scheme == "file") uri.path?.let { File(it).delete() }
            else context.contentResolver.delete(uri, null, null)
        }
    }

    private fun insertPending(context: Context, bitmap: Bitmap): Uri? {
        val resolver = context.contentResolver
        val values = ContentValues().apply {
            put(MediaStore.MediaColumns.DISPLAY_NAME, fileName())
            put(MediaStore.MediaColumns.MIME_TYPE, "image/png")
            put(MediaStore.MediaColumns.RELATIVE_PATH, "Pictures/音匣")
            put(MediaStore.MediaColumns.IS_PENDING, 1)
        }
        val uri = resolver.insert(MediaStore.Images.Media.getContentUri(MediaStore.VOLUME_EXTERNAL_PRIMARY), values)
            ?: return null
        val written = runCatching {
            resolver.openOutputStream(uri)?.use { bitmap.compress(Bitmap.CompressFormat.PNG, 100, it) } == true
        }.getOrDefault(false)
        if (!written) {
            resolver.delete(uri, null, null)
            return null
        }
        values.clear()
        values.put(MediaStore.MediaColumns.IS_PENDING, 0)
        resolver.update(uri, values, null, null)
        return uri
    }

    private fun insertLegacy(bitmap: Bitmap): Uri? {
        val dir = File(Environment.getExternalStoragePublicDirectory(Environment.DIRECTORY_PICTURES), "音匣")
        if (!dir.isDirectory && !dir.mkdirs()) return null
        val file = File(dir, fileName())
        val written = runCatching {
            file.outputStream().use { bitmap.compress(Bitmap.CompressFormat.PNG, 100, it) }
        }.isSuccess
        if (!written) {
            file.delete()
            return null
        }
        return Uri.fromFile(file)
    }

    private fun fileName(): String = "netease-login-qr.png"
}
