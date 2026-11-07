package io.github.backspace135.musicunlock.core

import android.os.Process
import java.io.File
import java.util.UUID

/**
 * The app cache folders the bridge works in, under `cacheDir/musicunlock/<run>/`: `staging/` holds a conversion's or
 * export's output until it is published, `imports/` holds copies of files shared to the app. Each process run uses
 * its own `<run>` folder and deletes the others when it starts, so whatever a failed conversion or a killed app left
 * behind is gone by the next start.
 */
internal object CacheFolders {
    private val run = "${Process.myPid()}-${System.currentTimeMillis()}"

    private val root: File get() = File(MusicUnlockCore.context.cacheDir, "musicunlock")
    private val current: File get() = File(root, run)

    val imports: File get() = File(current, "imports")

    /** The lyric workspace's output folder in the core's eyes; exports write to staging folders and publish. */
    val lyricExports: File get() = File(current, "lyrics")

    /** Runs [block] with a new, empty staging folder and deletes the folder afterwards. */
    inline fun <T> withStaging(block: (File) -> T): T {
        val folder = newStaging()
        check(folder.mkdirs()) { "无法创建临时文件夹" }
        try {
            return block(folder)
        } finally {
            folder.deleteRecursively()
        }
    }

    fun newStaging(): File = File(current, "staging/${UUID.randomUUID()}")

    /** Deletes the folders of earlier runs. Runs on a background thread at process start. */
    fun deleteEarlierRuns() {
        root.listFiles()?.filter { it.name != run }?.forEach { it.deleteRecursively() }
    }
}
