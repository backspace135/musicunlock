package io.github.backspace135.musicunlock.core

/**
 * The free functions of the core's audio.h, conversion.h, import.h and queue.h. Every call is quick and may run on
 * any thread. Use these instead of re-implementing a rule in Kotlin.
 */
object AudioFormats {
    init {
        MusicUnlockCore.load()
    }

    /** Lowercase extensions the core converts ("ncm", "qmcflac", "mflac", "kgm", ...). */
    val supportedExtensions: Set<String> by lazy { nativeSupportedExtensions().toSet() }

    /** Case-insensitive, without the dot: `isSupportedExtension("NCM")` is true. */
    fun isSupportedExtension(extension: String): Boolean = nativeIsSupportedExtension(extension)

    /** Whether a file of this name is imported: the rule of import.cpp (".ncm" alone has no extension). */
    fun isSupportedFileName(name: String): Boolean = nativeIsSupportedFileName(name)

    /** The platform a format comes from: "网易云", "QQ 音乐", "酷狗", "酷我", "虾米". */
    fun platformFor(extension: String): String = nativePlatformFor(extension)

    /** Finder / Explorer order: case-insensitive, digit runs compared by value ("2" before "10"). */
    fun naturalLess(a: String, b: String): Boolean = nativeNaturalLess(a, b)

    /** [naturalLess] as a Comparator, for `sortedWith`. */
    val naturalOrder: Comparator<String> = Comparator { a, b ->
        when {
            naturalLess(a, b) -> -1
            naturalLess(b, a) -> 1
            else -> 0
        }
    }

    /** Sorts with one native call: stable, by [naturalLess] of [key]. */
    fun <T> sortedNaturally(items: List<T>, key: (T) -> String): List<T> {
        val order = nativeNaturalOrder(Array(items.size) { key(items[it]) })
        return order.map { items[it] }
    }

    /** "512 字节", "12 KB", "28.4 MB", "1.25 GB" (decimal units, like Finder). */
    fun formatFileSize(bytes: Long): String = nativeFormatFileSize(bytes)

    /** A file name stem that is safe everywhere (also on Windows), at most 180 UTF-16 units; never empty. */
    fun sanitizeFileStem(name: String): String = nativeSanitizeFileStem(name)

    /** "name.ext" for suffix 0, then "name (1).ext", "name (2).ext"...: how exports number colliding names. */
    fun numberedFileName(stem: String, extension: String, suffix: Int): String =
        nativeNumberedFileName(stem, extension, suffix)

    /** How many numbered names an export tries before giving up. */
    val maxNameSuffix: Int by lazy { nativeMaxNameSuffix() }

    internal fun namingLabel(policy: NamingPolicy): String = nativeNamingLabel(policy.ordinal)
    internal fun stateLabel(state: JobState): String = nativeStateLabel(state.ordinal)
    internal fun filterLabel(filter: QueueFilter): String = nativeFilterLabel(filter.ordinal)
    internal fun filterIncludes(filter: QueueFilter, state: JobState): Boolean =
        nativeFilterIncludes(filter.ordinal, state.ordinal)

    @JvmStatic private external fun nativeSupportedExtensions(): Array<String>
    @JvmStatic private external fun nativeIsSupportedExtension(extension: String): Boolean
    @JvmStatic private external fun nativeIsSupportedFileName(name: String): Boolean
    @JvmStatic private external fun nativePlatformFor(extension: String): String
    @JvmStatic private external fun nativeNaturalLess(a: String, b: String): Boolean
    @JvmStatic private external fun nativeNaturalOrder(keys: Array<String>): IntArray
    @JvmStatic private external fun nativeFormatFileSize(bytes: Long): String
    @JvmStatic private external fun nativeSanitizeFileStem(name: String): String
    @JvmStatic private external fun nativeNumberedFileName(stem: String, extension: String, suffix: Int): String
    @JvmStatic private external fun nativeMaxNameSuffix(): Int
    @JvmStatic private external fun nativeNamingLabel(policy: Int): String
    @JvmStatic private external fun nativeStateLabel(state: Int): String
    @JvmStatic private external fun nativeFilterLabel(filter: Int): String
    @JvmStatic private external fun nativeFilterIncludes(filter: Int, state: Int): Boolean
}

/** How converted files are named (conversion.h `NamingPolicy`). [id] is the value saved in settings. */
enum class NamingPolicy(val id: String) {
    ORIGINAL("original"),
    ARTIST_TITLE("artistTitle"),
    TITLE("title");

    /** "保留原文件名" / "歌手 - 歌曲名" / "歌曲名". */
    val label: String get() = AudioFormats.namingLabel(this)

    companion object {
        /** The policy saved as [id]; unknown values are [ORIGINAL], as on Windows. */
        fun fromId(id: String?): NamingPolicy = entries.firstOrNull { it.id == id } ?: ORIGINAL
    }
}

/** A queued file's state (queue.h `JobState`). */
enum class JobState {
    WAITING,
    RUNNING,
    DONE,
    FAILED;

    /** "待转换" / "转换中" / "已完成" / "未完成". */
    val label: String get() = AudioFormats.stateLabel(this)
}

/** The queue's filters (queue.h `QueueFilter`), in display order. */
enum class QueueFilter {
    ALL,
    WAITING,
    DONE,
    FAILED;

    /** "全部" / "待转换" / "已完成" / "未完成". */
    val label: String get() = AudioFormats.filterLabel(this)

    fun includes(state: JobState): Boolean = AudioFormats.filterIncludes(this, state)
}
