package io.github.backspace135.musicunlock.core

/** The free functions of the core's lyrics.h. Every call is quick and may run on any thread. */
object Lyrics {
    init {
        MusicUnlockCore.load()
    }

    /**
     * Decodes the bytes of a local LRC / TXT: UTF-8, UTF-16 with BOM, or GB18030 / GBK; strips BOMs.
     * Throws [LyricException] for files over 5 MB or text it cannot decode.
     */
    fun decodeLyricFile(data: ByteArray): String = nativeDecodeLyricFile(data)

    /** Pinyin with tone marks ("nǐ hǎo"); null before Android 10 (API 29), which has no transliteration. */
    fun mandarinToLatin(text: String): String? = nativeMandarinToLatin(text)

    @JvmStatic private external fun nativeDecodeLyricFile(data: ByteArray): String
    @JvmStatic private external fun nativeMandarinToLatin(text: String): String?
}
