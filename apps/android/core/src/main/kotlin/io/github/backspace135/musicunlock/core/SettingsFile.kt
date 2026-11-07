package io.github.backspace135.musicunlock.core

import org.json.JSONObject
import java.io.File
import java.util.concurrent.Executors

/**
 * A JSON settings file in the app's private files folder, with the Windows app's key names. Reads once; writes go
 * to a background thread, through a sibling file swapped in, so a crash never leaves half a file. Best effort:
 * an unreadable or unwritable file must not interrupt the user, so failures fall back to the defaults.
 */
internal class SettingsFile(private val name: String) {
    private val file: File get() = File(MusicUnlockCore.context.filesDir, name)

    fun read(): JSONObject = try {
        JSONObject(file.readText())
    } catch (_: Exception) {
        JSONObject()
    }

    fun write(json: JSONObject) {
        val text = json.toString(2)
        writer.execute {
            try {
                val target = file
                val temporary = File(target.path + ".tmp")
                temporary.writeText(text)
                temporary.renameTo(target)
            } catch (_: Exception) {
                // Keep the in-memory value; the next change tries again.
            }
        }
    }

    /** Waits until the writes so far are on disk (tests). */
    fun flush() {
        writer.submit {}.get()
    }

    companion object {
        private val writer = Executors.newSingleThreadExecutor { Thread(it, "musicunlock-settings") }
    }
}
