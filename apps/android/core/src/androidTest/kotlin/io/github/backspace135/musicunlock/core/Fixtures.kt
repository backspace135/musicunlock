package io.github.backspace135.musicunlock.core

import android.content.Context
import android.net.Uri
import android.os.Build
import android.provider.MediaStore
import androidx.test.platform.app.InstrumentationRegistry
import java.io.ByteArrayOutputStream
import java.io.File
import java.io.OutputStream
import java.nio.ByteBuffer
import java.nio.ByteOrder
import java.util.Base64
import javax.crypto.Cipher
import javax.crypto.spec.SecretKeySpec

/** Test inputs: the shared QMC vectors, synthetic NCM / UC containers, and documents to import them from. */
object Fixtures {
    val context: Context get() = InstrumentationRegistry.getInstrumentation().targetContext

    /** The container of each shared QMC vector (raw + suffix), with the extension the core expects. */
    val qmcContainers = listOf(
        "mflac_map" to "mflac",
        "mflac_rc4" to "mflac",
        "mflac0_rc4" to "mflac0",
        "mgg_map" to "mgg",
        "qmc0_static" to "qmc0",
    )

    fun asset(name: String): ByteArray = context.assets.open(name).use { it.readBytes() }

    fun qmcContainer(name: String): ByteArray = asset("qmc/${name}_raw.bin") + asset("qmc/${name}_suffix.bin")
    fun qmcTarget(name: String): ByteArray = asset("qmc/${name}_target.bin")

    /** A silent PCM WAV, as the core tests' wave_fixture. */
    fun wave(samples: Int = 160): ByteArray {
        val data = ByteBuffer.allocate(44 + samples).order(ByteOrder.LITTLE_ENDIAN)
        data.put("RIFF".toByteArray()).putInt(36 + samples).put("WAVEfmt ".toByteArray()).putInt(16)
        data.putShort(1).putShort(1).putInt(8000).putInt(16000).putShort(2).putShort(16)
        data.put("data".toByteArray()).putInt(samples)
        return data.array()
    }

    /** NetEase cache (.uc): the audio XOR 0xA3. */
    fun uc(audio: ByteArray): ByteArray = ByteArray(audio.size) { (audio[it].toInt() xor 0xa3).toByte() }

    /** An NCM container around [audio] with the given tags and cover, built like the core tests' ncm_fixture. */
    fun ncm(audio: ByteArray, title: String, artist: String, cover: ByteArray? = null): ByteArray {
        val out = ByteArrayOutputStream(audio.size + 1024)
        writeNcm(out, audio.size.toLong(), title, artist, cover) { offset, buffer, count ->
            audio.copyInto(buffer, 0, offset.toInt(), offset.toInt() + count)
        }
        return out.toByteArray()
    }

    /**
     * Streams an NCM container whose [size] bytes of audio come from [audio] (fill `count` bytes at `offset`), so
     * a container larger than the test's heap can be written straight to a file.
     */
    fun writeNcm(out: OutputStream, size: Long, title: String, artist: String, cover: ByteArray?,
                 audio: (offset: Long, buffer: ByteArray, count: Int) -> Unit) {
        val key = "fixture-key".toByteArray()
        val encryptedKey = aes("neteasecloudmusic".toByteArray() + key, "hzHRAmso5kInbaxW".toByteArray())
            .map { (it.toInt() xor 0x64).toByte() }.toByteArray()
        val json = "music:{\"musicName\":\"$title\",\"artist\":[[\"$artist\",1]],\"album\":\"测试专辑\",\"format\":\"flac\"}"
        val meta = Base64.getEncoder().encodeToString(aes(json.toByteArray(), "#14ljk_!\\]&0U<'(".toByteArray()))
        val encryptedMeta = "163 key(Don't modify):$meta".toByteArray().map { (it.toInt() xor 0x63).toByte() }.toByteArray()
        val box = IntArray(256) { it }
        var j = 0
        for (i in 0 until 256) {
            j = (box[i] + j + key[i % key.size]) and 255
            box[i] = box[j].also { box[j] = box[i] }
        }
        val mask = ByteArray(256) { index ->
            val i = (index + 1) and 255
            val a = box[i]
            val b = box[(i + a) and 255]
            box[(a + b) and 255].toByte()
        }
        out.write("CTENFDAM".toByteArray())
        out.write(byteArrayOf(0, 0))
        out.writeLe32(encryptedKey.size)
        out.write(encryptedKey)
        out.writeLe32(encryptedMeta.size)
        out.write(encryptedMeta)
        out.write(ByteArray(5))
        out.writeLe32(cover?.size ?: 0)
        out.writeLe32(cover?.size ?: 0)
        cover?.let(out::write)
        val buffer = ByteArray(1 shl 16)  // a multiple of 256, so the key stream restarts with each chunk
        var offset = 0L
        while (offset < size) {
            val count = minOf(buffer.size.toLong(), size - offset).toInt()
            audio(offset, buffer, count)
            for (i in 0 until count) buffer[i] = (buffer[i].toInt() xor mask[i and 255].toInt()).toByte()
            out.write(buffer, 0, count)
            offset += count
        }
    }

    /** Fills a FLAC stream of any size: "fLaC", a STREAMINFO block, then (fake) frame bytes. */
    fun flacBytes(offset: Long, buffer: ByteArray, count: Int) {
        for (i in 0 until count) {
            val position = offset + i
            buffer[i] = when {
                position < 4 -> "fLaC"[position.toInt()].code.toByte()
                position == 4L -> 0x80.toByte()  // last block, STREAMINFO
                position == 7L -> 34
                position < 42 -> 0
                else -> (position * 31).toByte()
            }
        }
    }

    /** A FLAC stream of [frames] bytes after its header. */
    fun flac(frames: Int): ByteArray = ByteArray(42 + frames).also { flacBytes(0, it, it.size) }

    private fun aes(input: ByteArray, key: ByteArray): ByteArray =
        Cipher.getInstance("AES/ECB/PKCS5Padding").apply { init(Cipher.ENCRYPT_MODE, SecretKeySpec(key, "AES")) }
            .doFinal(input)

    private fun OutputStream.writeLe32(value: Int) {
        write(ByteBuffer.allocate(4).order(ByteOrder.LITTLE_ENDIAN).putInt(value).array())
    }

    /** Writes a document into the test provider and returns its URI. */
    fun document(relativePath: String, data: ByteArray): Uri {
        val file = File(TestDocumentsProvider.rootFolder(context), relativePath)
        file.parentFile?.mkdirs()
        file.writeBytes(data)
        return TestDocumentsProvider.documentUri(relativePath)
    }

    fun read(uri: Uri): ByteArray = context.contentResolver.openInputStream(uri)!!.use { it.readBytes() }

    /** Deletes the MediaStore entries the tests created in the default folders. */
    fun deletePublished(uris: Collection<Uri>) {
        for (uri in uris) {
            try {
                context.contentResolver.delete(uri, null, null)
            } catch (_: Exception) {
            }
        }
    }

    /** Names in Music/音匣 (MediaStore) that start with [prefix]. */
    fun musicNames(prefix: String): List<String> {
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.Q) return emptyList()
        val names = mutableListOf<String>()
        context.contentResolver.query(
            MediaStore.Audio.Media.getContentUri(MediaStore.VOLUME_EXTERNAL_PRIMARY),
            arrayOf(MediaStore.MediaColumns._ID, MediaStore.MediaColumns.DISPLAY_NAME),
            "${MediaStore.MediaColumns.RELATIVE_PATH} = ? AND ${MediaStore.MediaColumns.DISPLAY_NAME} LIKE ?",
            arrayOf("Music/音匣/", "$prefix%"), null,
        )?.use { while (it.moveToNext()) names += it.getString(1) }
        return names.sorted()
    }
}
