package io.github.backspace135.musicunlock.core

import org.json.JSONArray
import org.json.JSONObject

/** The free functions of the core's lyrics.h and lyric_workspace.h. Every call is quick and may run on any thread. */
object Lyrics {
    init {
        MusicUnlockCore.load()
    }

    /**
     * Decodes the bytes of a local LRC / TXT: UTF-8, UTF-16 with BOM, or GB18030 / GBK; strips BOMs.
     * Throws [LyricException] for files over 5 MB or text it cannot decode. See also [LocalLyrics.read].
     */
    fun decodeLyricFile(data: ByteArray): String = nativeDecodeLyricFile(data)

    /** Pinyin with tone marks ("nǐ hǎo"); null before Android 10 (API 29), which has no transliteration. */
    fun mandarinToLatin(text: String): String? = nativeMandarinToLatin(text)

    /** Whether "中文拼音" can be offered: Android 10 and later. Hide the option otherwise. */
    val pinyinSupported: Boolean by lazy { mandarinToLatin("中") != null }

    /** Lines of LRC text, as the core parses them. */
    fun parseLyrics(text: String): List<LyricLine> {
        val array = JSONArray(nativeParseLyrics(text))
        return List(array.length()) {
            val line = array.getJSONObject(it)
            LyricLine(if (line.isNull("milliseconds")) null else line.getLong("milliseconds"), line.getString("text"))
        }
    }

    /** NetEase YRC (word timing) as enhanced LRC. */
    fun enhancedLrcFromYrc(yrc: String): String = nativeEnhancedLrcFromYrc(yrc)

    /** "[01:02.345]" for LRC, "00:01:02,345" for SRT... */
    fun lyricTimestamp(milliseconds: Long, format: LyricFormat, precision: Int = 3): String =
        nativeLyricTimestamp(milliseconds, format.ordinal, precision)

    /** The export text of [document] with [options]; throws [LyricException] for options that cannot be exported. */
    fun renderLyrics(document: LyricDocument, options: LyricOptions, durationMs: Long = 0): String =
        nativeRenderLyrics(ModelJson.json(document).toString(), options.toJson(), durationMs)

    /** The file name (without extension) [nameTemplate] gives for [song], sanitized. */
    fun lyricFilename(song: LyricSong, nameTemplate: String, index: Int): String =
        nativeLyricFilename(ModelJson.json(song).toString(), nameTemplate, index)

    /** Bytes of [text] in [encoding], with the BOM the core writes. */
    fun encodeText(encoding: LyricEncoding, text: String): ByteArray = nativeEncodeText(encoding.ordinal, text)

    /**
     * Accepts an ID, a QQ 14-character mid, or text containing a full music.163.com / y.qq.com link (a pasted share
     * text works). Null when it is none of these. [LyricWorkspace.search] already does this; do not parse links in
     * the UI.
     */
    fun parseLink(input: String, provider: MusicProvider, kind: SearchKind): MusicLink? =
        nativeParseLink(input, provider.ordinal, kind.ordinal)?.let { ModelJson.link(JSONObject(it)) }

    /** "3:36"; empty for unknown durations. */
    fun formatDuration(milliseconds: Long): String = nativeFormatDuration(milliseconds)

    internal const val PROVIDER_TITLE = 0
    internal const val PROVIDER_HOME = 1
    internal const val PROVIDER_ID = 2
    internal const val SEARCH_KIND = 3
    internal const val FORMAT_LABEL = 4
    internal const val FORMAT_EXTENSION = 5
    internal const val LAYOUT = 6
    internal const val ENCODING = 7
    internal const val TAB = 8
    internal const val QR_MESSAGE = 9

    internal fun label(which: Int, index: Int): String = nativeLabel(which, index)
    internal fun providerOwns(provider: MusicProvider, domain: String): Boolean =
        nativeProviderOwns(provider.ordinal, domain)
    internal fun normalizeOptions(json: String): String = nativeNormalizeOptions(json)
    internal fun qrCode(text: String): String = nativeQrCode(text)
    internal fun qrState(code: Int): Int = nativeQrState(code)
    internal fun qrRefusal(code: Long, message: String): String = nativeQrRefusal(code, message)

    @JvmStatic private external fun nativeDecodeLyricFile(data: ByteArray): String
    @JvmStatic private external fun nativeMandarinToLatin(text: String): String?
    @JvmStatic private external fun nativeLabel(which: Int, index: Int): String
    @JvmStatic private external fun nativeProviderOwns(provider: Int, domain: String): Boolean
    @JvmStatic private external fun nativeNormalizeOptions(json: String): String
    @JvmStatic private external fun nativeEncodeText(encoding: Int, text: String): ByteArray
    @JvmStatic private external fun nativeParseLyrics(text: String): String
    @JvmStatic private external fun nativeEnhancedLrcFromYrc(yrc: String): String
    @JvmStatic private external fun nativeLyricTimestamp(milliseconds: Long, format: Int, precision: Int): String
    @JvmStatic private external fun nativeRenderLyrics(document: String, options: String, durationMs: Long): String
    @JvmStatic private external fun nativeLyricFilename(song: String, nameTemplate: String, index: Int): String
    @JvmStatic private external fun nativeParseLink(input: String, provider: Int, kind: Int): String?
    @JvmStatic private external fun nativeFormatDuration(milliseconds: Long): String
    @JvmStatic private external fun nativeQrCode(text: String): String
    @JvmStatic private external fun nativeQrState(code: Int): Int
    @JvmStatic private external fun nativeQrRefusal(code: Long, message: String): String
}
