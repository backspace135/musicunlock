package io.github.backspace135.musicunlock.core

import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.update
import org.json.JSONObject

/**
 * The audio workspace's export settings, saved in `settings.json` with the Windows keys: `outputDirectory` (""
 * for `Music/音匣`, else the picked folder's URI) and `namingPolicy` ("original" / "artistTitle" / "title"; unknown
 * values are "original"). Changes are saved right away. Any thread.
 */
object UnlockSettings {
    data class Values(val outputFolder: OutputFolder, val naming: NamingPolicy)

    private val file = SettingsFile("settings.json")
    private val state: MutableStateFlow<Values> by lazy { MutableStateFlow(load()) }

    /** The current settings; collect it to show them. */
    val values: StateFlow<Values> get() = state.asStateFlow()

    fun setOutputFolder(folder: OutputFolder) = change { it.copy(outputFolder = folder) }

    fun setNaming(naming: NamingPolicy) = change { it.copy(naming = naming) }

    /**
     * Switches back to `Music/音匣` when the saved picked folder was deleted or its grant revoked. Returns true when
     * it did, so the caller can tell the user. Call it before showing the settings and before each batch.
     */
    fun fallBackIfUnusable(): Boolean {
        val folder = values.value.outputFolder
        if (folder !is OutputFolder.Picked || OutputFolders.isUsable(folder)) return false
        setOutputFolder(OutputFolder.DefaultMusic)
        return true
    }

    internal fun flush() = file.flush()

    @Synchronized
    private fun change(transform: (Values) -> Values) {
        state.update(transform)
        val value = state.value
        file.write(JSONObject()
            .put("outputDirectory", OutputFolders.settingValue(value.outputFolder))
            .put("namingPolicy", value.naming.id))
    }

    private fun load(): Values {
        val json = file.read()
        return Values(
            OutputFolders.fromSetting(json.optString("outputDirectory"), OutputFolder.DefaultMusic),
            NamingPolicy.fromId(json.optString("namingPolicy")),
        )
    }
}
