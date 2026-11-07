package io.github.backspace135.musicunlock.core

import android.icu.text.Transliterator
import android.os.Build
import java.nio.ByteBuffer
import java.nio.charset.CharacterCodingException
import java.nio.charset.Charset
import java.nio.charset.CodingErrorAction

/**
 * The core's platform text conversion (`mandarin_to_latin`, `gb18030_to_utf8`), called from C++ on whichever thread
 * the core runs on. Kept by consumer-rules.pro.
 */
internal object PlatformText {
    // ICU transliterators are not thread-safe, and compiling Han-Latin is costly: keep one per thread.
    private val hanLatin = ThreadLocal<Transliterator?>()

    /** Pinyin with tone marks, as macOS kCFStringTransformMandarinLatin; null before API 29 (no android.icu). */
    @JvmStatic
    fun mandarinToLatin(text: String): String? {
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.Q) return null
        val transliterator = hanLatin.get() ?: Transliterator.getInstance("Han-Latin").also { hanLatin.set(it) }
        return transliterator.transliterate(text)
    }

    /** GBK / GB18030 text; null when the bytes are not valid GB18030. */
    @JvmStatic
    fun gb18030ToUtf8(bytes: ByteArray): String? = try {
        Charset.forName("GB18030").newDecoder()
            .onMalformedInput(CodingErrorAction.REPORT)
            .onUnmappableCharacter(CodingErrorAction.REPORT)
            .decode(ByteBuffer.wrap(bytes))
            .toString()
    } catch (_: CharacterCodingException) {
        null
    }
}
