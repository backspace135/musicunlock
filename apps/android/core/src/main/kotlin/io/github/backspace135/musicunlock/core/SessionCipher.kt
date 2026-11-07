package io.github.backspace135.musicunlock.core

import android.security.keystore.KeyGenParameterSpec
import android.security.keystore.KeyProperties
import java.security.KeyStore
import javax.crypto.Cipher
import javax.crypto.KeyGenerator
import javax.crypto.SecretKey
import javax.crypto.spec.GCMParameterSpec

/**
 * Encryption of saved login sessions (session.h `SessionCipher`). [open] returns null for data it cannot decrypt
 * or authenticate; [seal] throws when encryption is unavailable.
 */
interface SessionCipher {
    fun seal(plain: ByteArray): ByteArray
    fun open(sealed: ByteArray): ByteArray?
}

/**
 * AES-256-GCM with a key generated in the Android Keystore, which never leaves it (not exportable, not in
 * backups). A sealed session is a version byte, the 12-byte IV and the ciphertext with its tag. Removing the app
 * or clearing its data deletes the key, so old session files become unreadable and are ignored.
 */
class KeystoreSessionCipher(private val alias: String = DEFAULT_ALIAS) : SessionCipher {
    override fun seal(plain: ByteArray): ByteArray = try {
        val cipher = Cipher.getInstance(TRANSFORMATION)
        cipher.init(Cipher.ENCRYPT_MODE, key(create = true))
        byteArrayOf(VERSION) + cipher.iv + cipher.doFinal(plain)
    } catch (error: Exception) {
        throw LyricException("无法加密登录会话（${error.javaClass.simpleName}）。")
    }

    override fun open(sealed: ByteArray): ByteArray? = try {
        if (sealed.size < 1 + IV_SIZE + TAG_BYTES || sealed[0] != VERSION) {
            null
        } else {
            val key = key(create = false) ?: return null
            val cipher = Cipher.getInstance(TRANSFORMATION)
            cipher.init(Cipher.DECRYPT_MODE, key, GCMParameterSpec(TAG_BYTES * 8, sealed, 1, IV_SIZE))
            cipher.doFinal(sealed, 1 + IV_SIZE, sealed.size - 1 - IV_SIZE)
        }
    } catch (_: Exception) {
        null
    }

    /** Deletes the key; every session sealed with it becomes unreadable. */
    fun deleteKey() {
        KeyStore.getInstance(KEYSTORE).apply { load(null) }.deleteEntry(alias)
    }

    @Synchronized
    private fun key(create: Boolean): SecretKey? {
        val store = KeyStore.getInstance(KEYSTORE).apply { load(null) }
        (store.getKey(alias, null) as? SecretKey)?.let { return it }
        if (!create) return null
        val generator = KeyGenerator.getInstance(KeyProperties.KEY_ALGORITHM_AES, KEYSTORE)
        generator.init(
            KeyGenParameterSpec.Builder(alias, KeyProperties.PURPOSE_ENCRYPT or KeyProperties.PURPOSE_DECRYPT)
                .setBlockModes(KeyProperties.BLOCK_MODE_GCM)
                .setEncryptionPaddings(KeyProperties.ENCRYPTION_PADDING_NONE)
                .setKeySize(256)
                .setRandomizedEncryptionRequired(true)
                .build(),
        )
        return generator.generateKey()
    }

    companion object {
        const val DEFAULT_ALIAS = "musicunlock-session"
        private const val KEYSTORE = "AndroidKeyStore"
        private const val TRANSFORMATION = "AES/GCM/NoPadding"
        private const val VERSION: Byte = 1
        private const val IV_SIZE = 12
        private const val TAG_BYTES = 16
    }
}
