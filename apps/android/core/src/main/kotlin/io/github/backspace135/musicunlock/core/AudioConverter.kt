package io.github.backspace135.musicunlock.core

import android.net.Uri
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import java.io.File

/**
 * A converted file, written to its output folder. Pass it to [AudioQueue.jobSucceeded]; [uri] opens or shares it
 * (grant read access with `Intent.FLAG_GRANT_READ_URI_PERMISSION`).
 */
class ConvertedFile(
    val uri: Uri,
    /** The output file name, e.g. "歌名 (1).flac". */
    val name: String,
    /** Display path of the output, e.g. "Music/音匣/歌名 (1).flac". */
    val location: String,
    /** "flac", "mp3", "ogg", "m4a"... */
    val extension: String,
    val title: String,
    val artist: String,
    val album: String,
    /** The cover from the restored tags (JPEG or PNG bytes), or null. */
    val cover: ByteArray?,
)

/** What the core returns for one conversion; constructed from JNI (see consumer-rules.pro). */
internal class ConvertedAudio(
    val path: String,
    val extension: String,
    val title: String,
    val artist: String,
    val album: String,
    val cover: ByteArray?,
)

/** Converts queued files with the core's `convert_file` rules: never touches the source, never overwrites. */
object AudioConverter {
    private const val TOO_MANY_NAMES = "输出目录同名文件过多，请选择其他目录"

    init {
        MusicUnlockCore.load()
    }

    @Volatile
    private var maskLoaded = false

    /**
     * Converts the job whose [AudioJob.source] is [source] into [folder], naming it by [naming]. Runs on
     * [Dispatchers.IO] and takes a while for large files; convert one file at a time. Throws [CoreException] (usually
     * [DecodeException]) with the reason to show: pass `AudioQueue.failureMessage(error)` to
     * [AudioQueue.jobFailed].
     *
     * The source is read through a file descriptor (memory-mapped, not copied through Java); the core writes the
     * result into an empty staging folder in the app cache, then it is published to [folder] with the core's
     * " (n)" numbering and the staging folder is deleted.
     */
    suspend fun convert(source: String, folder: OutputFolder, naming: NamingPolicy): ConvertedFile =
        withContext(Dispatchers.IO) {
            val imported = ImportedSources.find(source)
                ?: throw DecodeException(DecodeException.Kind.INVALID, "无法读取文件（请重新添加这个文件）")
            loadMask()
            CacheFolders.withStaging { staging ->
                val audio = ImportedSources.open(imported).use { descriptor ->
                    nativeConvertDescriptor(descriptor.fd, imported.name, staging.path, naming.ordinal)
                }
                val staged = File(audio.path)
                val stem = staged.name.removeSuffix(".${audio.extension}")
                val published = FilePublisher.publish(staged, stem, audio.extension, folder, TOO_MANY_NAMES)
                ImportedSources.releaseCopy(source)
                ConvertedFile(published.uri, published.name, published.location, audio.extension, audio.title,
                    audio.artist, audio.album, audio.cover)
            }
        }

    /** KGM / VPR need kgm.mask, which ships as an asset; without it they fail with the core's message. */
    private fun loadMask() {
        if (maskLoaded) return
        synchronized(this) {
            if (maskLoaded) return
            val mask = try {
                MusicUnlockCore.context.assets.open("kgm.mask").use { it.readBytes() }
            } catch (_: Exception) {
                null
            }
            if (mask != null) nativeSetKgmMask(mask)
            maskLoaded = true
        }
    }

    @JvmStatic private external fun nativeSetKgmMask(mask: ByteArray)
    @JvmStatic private external fun nativeConvertDescriptor(fd: Int, sourceName: String, directory: String,
                                                            naming: Int): ConvertedAudio
}
