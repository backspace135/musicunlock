package io.github.backspace135.musicunlock.core

import android.content.ContentProvider
import android.content.ContentValues
import android.database.Cursor
import android.net.Uri

/**
 * Hands the application context to [MusicUnlockCore] when the process starts, before `Application.onCreate`,
 * so no feature has to remember an init call. Declared in this module's manifest; it serves no data.
 */
internal class CoreInitializer : ContentProvider() {
    override fun onCreate(): Boolean {
        MusicUnlockCore.attach(checkNotNull(context))
        return true
    }

    override fun query(uri: Uri, projection: Array<out String>?, selection: String?, selectionArgs: Array<out String>?,
                       sortOrder: String?): Cursor? = null

    override fun getType(uri: Uri): String? = null
    override fun insert(uri: Uri, values: ContentValues?): Uri? = null
    override fun delete(uri: Uri, selection: String?, selectionArgs: Array<out String>?): Int = 0
    override fun update(uri: Uri, values: ContentValues?, selection: String?, selectionArgs: Array<out String>?): Int = 0
}
