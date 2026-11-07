package io.github.backspace135.musicunlock.core

import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.update
import org.json.JSONObject

/**
 * The lyric export settings, saved in `lyrics.json` like the Windows app: `options` in the core's JSON form
 * (`format`, `layout`, `encoding`, `includeOriginal`…, invalid values fall back to the defaults) and
 * `outputDirectory` ("" for `Download/音匣/歌词`, else the picked folder's URI). Changes are saved right away.
 * Any thread.
 */
object LyricSettings {
    data class Values(val options: LyricOptions, val outputFolder: OutputFolder)

    private val file = SettingsFile("lyrics.json")
    private val state: MutableStateFlow<Values> by lazy { MutableStateFlow(load()) }

    /** The current settings; collect it, and pass `options` to [LyricWorkspace.options]. */
    val values: StateFlow<Values> get() = state.asStateFlow()

    fun setOptions(options: LyricOptions) = change { it.copy(options = options) }

    fun setOutputFolder(folder: OutputFolder) = change { it.copy(outputFolder = folder) }

    /** Switches back to `Download/音匣/歌词` when a picked folder is gone. True when it did: tell the user. */
    fun fallBackIfUnusable(): Boolean {
        val folder = values.value.outputFolder
        if (folder !is OutputFolder.Picked || OutputFolders.isUsable(folder)) return false
        setOutputFolder(OutputFolder.DefaultLyrics)
        return true
    }

    internal fun flush() = file.flush()

    @Synchronized
    private fun change(transform: (Values) -> Values) {
        state.update(transform)
        val value = state.value
        file.write(JSONObject()
            .put("options", JSONObject(value.options.toJson()))
            .put("outputDirectory", OutputFolders.settingValue(value.outputFolder)))
    }

    private fun load(): Values {
        val json = file.read()
        var options = LyricOptions.fromJson(json.optJSONObject("options")?.toString() ?: "{}")
        // No system pinyin before Android 10: drop the option instead of showing the core's Windows-specific error.
        if (options.includePinyin && !Lyrics.pinyinSupported) options = options.copy(includePinyin = false)
        return Values(options, OutputFolders.fromSetting(json.optString("outputDirectory"), OutputFolder.DefaultLyrics))
    }
}
