package io.github.backspace135.musicunlock.core

import android.net.Uri

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
