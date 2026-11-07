package io.github.backspace135.musicunlock.core

import org.json.JSONArray
import org.json.JSONObject

/** A music platform (lyrics.h `MusicProvider`). Labels come from the core. */
enum class MusicProvider {
    NETEASE,
    QQ;

    /** "网易云音乐" / "QQ 音乐". */
    val title: String get() = Lyrics.label(Lyrics.PROVIDER_TITLE, ordinal)

    /** "https://music.163.com/" / "https://y.qq.com/". */
    val home: String get() = Lyrics.label(Lyrics.PROVIDER_HOME, ordinal)

    /** "netease" / "qq": the session file name and the id prefix of songs. */
    val id: String get() = Lyrics.label(Lyrics.PROVIDER_ID, ordinal)

    /** Whether a cookie domain belongs to this platform. */
    fun owns(domain: String): Boolean = Lyrics.providerOwns(this, domain)
}

/** What a search looks for (lyrics.h `SearchKind`). */
enum class SearchKind {
    SONG,
    ALBUM,
    PLAYLIST;

    /** "歌曲" / "专辑" / "歌单". */
    val label: String get() = Lyrics.label(Lyrics.SEARCH_KIND, ordinal)
}

/** Export file format. [id] is the settings value. */
enum class LyricFormat(val id: String) {
    LRC("lrc"),
    SRT("srt"),
    TXT("txt"),
    VTT("vtt");

    /** "LRC" / "SRT" / "TXT" / "VTT". */
    val label: String get() = Lyrics.label(Lyrics.FORMAT_LABEL, ordinal)

    /** "lrc", "srt"... */
    val extension: String get() = Lyrics.label(Lyrics.FORMAT_EXTENSION, ordinal)
}

/** How translations and romanization are laid out. [id] is the settings value. */
enum class LyricLayout(val id: String) {
    STAGGER("stagger"),
    MERGE("merge"),
    SEPARATE("separate");

    /** "逐行交错" / "同一行合并" / "分段排列". */
    val label: String get() = Lyrics.label(Lyrics.LAYOUT, ordinal)
}

/** Text encoding of exported files. [id] is the settings value. */
enum class LyricEncoding(val id: String) {
    UTF8("utf8"),
    UTF8_BOM("utf8_bom"),
    UTF16LE("utf16le");

    /** "UTF-8" / "UTF-8 BOM" / "UTF-16 LE". */
    val label: String get() = Lyrics.label(Lyrics.ENCODING, ordinal)
}

/** The editor's tabs (lyric_workspace.h `LyricTab`). */
enum class LyricTab {
    ORIGINAL,
    TRANSLATION,
    ROMANIZATION,
    PREVIEW;

    /** "原文" / "译文" / "罗马音" / "导出预览". */
    val label: String get() = Lyrics.label(Lyrics.TAB, ordinal)
}

/** A song from a search or a resolved link. [id] ("netease:123") identifies it in [LyricWorkspace] calls. */
data class LyricSong(
    val provider: MusicProvider,
    val songId: String,
    val title: String,
    val artist: String,
    val album: String,
    val durationMs: Long,
    val numericId: String,
    val coverUrl: String,
    val id: String,
) {
    /** "3:36"; empty when unknown. */
    val duration: String get() = Lyrics.formatDuration(durationMs)
}

/** An album or playlist in search results; open it with [LyricWorkspace.open]. */
data class MusicCollection(
    val provider: MusicProvider,
    val collectionId: String,
    val kind: SearchKind,
    val title: String,
    val subtitle: String,
    val id: String,
)

/** A song's lyrics: the original and the platform's translation, romanization and word timing. */
data class LyricDocument(
    val original: String = "",
    val translation: String = "",
    val romanization: String = "",
    val wordTimed: String = "",
    /** True when the original has no lyric text (instrumental or missing). */
    val isEmpty: Boolean = original.isBlank(),
)

/** One parsed line; [milliseconds] is null for untimed lines. */
data class LyricLine(val milliseconds: Long?, val text: String)

/**
 * Export settings (lyrics.h `LyricOptions`), with the core's defaults. Saved by [LyricSettings] in the core's JSON
 * form, so invalid values fall back the same way on every platform.
 */
data class LyricOptions(
    val format: LyricFormat = LyricFormat.LRC,
    val layout: LyricLayout = LyricLayout.STAGGER,
    val encoding: LyricEncoding = LyricEncoding.UTF8,
    val includeOriginal: Boolean = true,
    val includeTranslation: Boolean = true,
    val includeRomanization: Boolean = false,
    val includePinyin: Boolean = false,
    val ignoreEmpty: Boolean = true,
    val skipInstrumental: Boolean = true,
    val wordTiming: Boolean = false,
    /** LRC millisecond digits, 1–3. */
    val precision: Int = 3,
    /** Milliseconds, ±86400000; positive delays the lyrics. */
    val offset: Long = 0,
    val separator: String = " / ",
    /** The file name template, with the placeholders name, singer, album, id and index in "${'$'}{...}". */
    val filename: String = "\${name} - \${singer}",
) {
    /** The core's JSON form (options_to_json). */
    fun toJson(): String = JSONObject()
        .put("format", format.id).put("layout", layout.id).put("encoding", encoding.id)
        .put("includeOriginal", includeOriginal).put("includeTranslation", includeTranslation)
        .put("includeRomanization", includeRomanization).put("includePinyin", includePinyin)
        .put("ignoreEmpty", ignoreEmpty).put("skipInstrumental", skipInstrumental).put("wordTiming", wordTiming)
        .put("precision", precision).put("offset", offset).put("separator", separator).put("filename", filename)
        .toString()

    companion object {
        /** Reads the core's JSON form; unknown or invalid fields get the defaults, values are clamped by the core. */
        fun fromJson(json: String): LyricOptions {
            val o = JSONObject(Lyrics.normalizeOptions(json))
            return LyricOptions(
                format = LyricFormat.entries.first { it.id == o.getString("format") },
                layout = LyricLayout.entries.first { it.id == o.getString("layout") },
                encoding = LyricEncoding.entries.first { it.id == o.getString("encoding") },
                includeOriginal = o.getBoolean("includeOriginal"),
                includeTranslation = o.getBoolean("includeTranslation"),
                includeRomanization = o.getBoolean("includeRomanization"),
                includePinyin = o.getBoolean("includePinyin"),
                ignoreEmpty = o.getBoolean("ignoreEmpty"),
                skipInstrumental = o.getBoolean("skipInstrumental"),
                wordTiming = o.getBoolean("wordTiming"),
                precision = o.getInt("precision"),
                offset = o.getLong("offset"),
                separator = o.getString("separator"),
                filename = o.getString("filename"),
            )
        }
    }
}

/** A platform link or ID (lyrics.h `MusicLink`); see [Lyrics.parseLink]. */
data class MusicLink(val provider: MusicProvider, val kind: SearchKind, val identifier: String)

/** A search or link resolution for [MusicApi.runSearch], from [LyricWorkspace.search] and friends. */
data class SearchRequest(
    val serial: Long,
    val text: String,
    val provider: MusicProvider,
    val kind: SearchKind,
    val page: Int,
    val more: Boolean,
    val link: MusicLink?,
    val collectionTitle: String,
)

/** What a search returned, for [LyricWorkspace.searchFinished]. */
data class SearchResult(val songs: List<LyricSong>, val collections: List<MusicCollection>, val hasMore: Boolean)

/** One song's lyrics to fetch with [MusicApi.lyrics], then [LyricWorkspace.lyricsLoaded]. */
data class LyricsRequest(val serial: Long, val song: LyricSong)

/** One song of an export batch. [document] is the edited or cached lyrics; null means fetch them. */
data class ExportJob(val song: LyricSong, val index: Int, val document: LyricDocument?)

/** The songs and settings frozen when an export starts ([LyricWorkspace.exportCurrent] / [LyricWorkspace.exportChecked]). */
data class ExportBatch(val jobs: List<ExportJob>, val options: LyricOptions)

/** A written lyric file: a content URI, its name and a display path such as "Download/音匣/歌词/晴天 - 周杰伦.lrc". */
data class ExportedFile(val uri: android.net.Uri, val name: String, val location: String)

/** One exported song: the lyrics used, and the file, or null when it was skipped as having no lyrics. */
data class ExportOutcome(val document: LyricDocument, val file: ExportedFile?)

/** JSON forms shared with model_json.h. */
internal object ModelJson {
    fun song(o: JSONObject) = LyricSong(
        MusicProvider.entries[o.getInt("provider")], o.getString("songId"), o.getString("title"),
        o.getString("artist"), o.getString("album"), o.getLong("durationMs"), o.getString("numericId"),
        o.getString("coverUrl"), o.getString("id"),
    )

    fun json(song: LyricSong): JSONObject = JSONObject()
        .put("provider", song.provider.ordinal).put("songId", song.songId).put("title", song.title)
        .put("artist", song.artist).put("album", song.album).put("durationMs", song.durationMs)
        .put("numericId", song.numericId).put("coverUrl", song.coverUrl)

    fun collection(o: JSONObject) = MusicCollection(
        MusicProvider.entries[o.getInt("provider")], o.getString("collectionId"), SearchKind.entries[o.getInt("kind")],
        o.getString("title"), o.getString("subtitle"), o.getString("id"),
    )

    fun json(collection: MusicCollection): JSONObject = JSONObject()
        .put("provider", collection.provider.ordinal).put("collectionId", collection.collectionId)
        .put("kind", collection.kind.ordinal).put("title", collection.title).put("subtitle", collection.subtitle)

    fun document(o: JSONObject) = LyricDocument(
        o.getString("original"), o.getString("translation"), o.getString("romanization"), o.getString("wordTimed"),
        o.optBoolean("empty", o.getString("original").isBlank()),
    )

    fun json(document: LyricDocument): JSONObject = JSONObject()
        .put("original", document.original).put("translation", document.translation)
        .put("romanization", document.romanization).put("wordTimed", document.wordTimed)

    fun link(o: JSONObject) =
        MusicLink(MusicProvider.entries[o.getInt("provider")], SearchKind.entries[o.getInt("kind")], o.getString("identifier"))

    fun json(link: MusicLink): JSONObject =
        JSONObject().put("provider", link.provider.ordinal).put("kind", link.kind.ordinal).put("identifier", link.identifier)

    fun searchRequest(o: JSONObject) = SearchRequest(
        o.getLong("serial"), o.getString("text"), MusicProvider.entries[o.getInt("provider")],
        SearchKind.entries[o.getInt("kind")], o.getInt("page"), o.getBoolean("more"),
        o.optJSONObject("link")?.let(::link), o.getString("collectionTitle"),
    )

    fun json(request: SearchRequest): JSONObject = JSONObject()
        .put("serial", request.serial).put("text", request.text).put("provider", request.provider.ordinal)
        .put("kind", request.kind.ordinal).put("page", request.page).put("more", request.more)
        .put("link", request.link?.let(::json) ?: JSONObject.NULL).put("collectionTitle", request.collectionTitle)

    fun searchResult(o: JSONObject) = SearchResult(
        list(o.getJSONArray("songs"), ::song), list(o.getJSONArray("collections"), ::collection), o.getBoolean("hasMore"),
    )

    fun json(result: SearchResult): JSONObject = JSONObject()
        .put("songs", JSONArray(result.songs.map(::json))).put("collections", JSONArray(result.collections.map(::json)))
        .put("hasMore", result.hasMore)

    fun lyricsRequest(o: JSONObject) = LyricsRequest(o.getLong("serial"), song(o.getJSONObject("song")))

    fun json(request: LyricsRequest): JSONObject = JSONObject().put("serial", request.serial).put("song", json(request.song))

    fun exportJob(o: JSONObject) =
        ExportJob(song(o.getJSONObject("song")), o.getInt("index"), o.optJSONObject("document")?.let(::document))

    fun json(job: ExportJob): JSONObject = JSONObject().put("song", json(job.song)).put("index", job.index)
        .put("document", job.document?.let(::json) ?: JSONObject.NULL)

    fun exportBatch(o: JSONObject) =
        ExportBatch(list(o.getJSONArray("jobs"), ::exportJob), LyricOptions.fromJson(o.getJSONObject("options").toString()))

    fun <T> list(array: JSONArray, item: (JSONObject) -> T): List<T> = List(array.length()) { item(array.getJSONObject(it)) }

    fun strings(array: JSONArray): List<String> = List(array.length()) { array.getString(it) }
}
