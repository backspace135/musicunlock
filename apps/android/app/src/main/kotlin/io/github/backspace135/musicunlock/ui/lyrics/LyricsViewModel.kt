package io.github.backspace135.musicunlock.ui.lyrics

import androidx.compose.runtime.Immutable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import androidx.lifecycle.ViewModel
import androidx.lifecycle.viewModelScope
import kotlinx.coroutines.Job
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch

// FAKE DATA (#30). #32 replaces this file with the core's LyricWorkspace.

@Immutable
data class FakeSong(val id: String, val title: String, val artist: String, val album: String, val duration: String)

class LyricsViewModel : ViewModel() {
    var query by mutableStateOf("")
    var results by mutableStateOf<List<FakeSong>>(emptyList())
        private set
    var checked by mutableStateOf<Set<String>>(emptySet())
        private set
    var selectionMode by mutableStateOf(false)
        private set
    var currentId by mutableStateOf<String?>(null)
    var format by mutableIntStateOf(0)
    var exporting by mutableStateOf(false)
        private set
    var exported by mutableIntStateOf(0)
        private set
    private var export: Job? = null

    fun search() {
        results = SampleSongs.shuffled().take(6)
        checked = emptySet()
        selectionMode = false
    }

    fun toggle(id: String) {
        checked = if (id in checked) checked - id else checked + id
        if (checked.isEmpty()) selectionMode = false
    }

    fun beginSelection(id: String) {
        selectionMode = true
        if (id !in checked) checked = checked + id
    }

    fun endSelection() {
        selectionMode = false
        checked = emptySet()
    }

    fun toggleAll() {
        checked = if (checked.size == results.size) emptySet() else results.map { it.id }.toSet()
        selectionMode = checked.isNotEmpty()
    }

    fun exportSelected() {
        if (exporting) {
            export?.cancel()
            return
        }
        exporting = true
        exported = 0
        export = viewModelScope.launch {
            try {
                repeat(checked.size) {
                    delay(700)
                    exported++
                }
            } finally {
                exporting = false
            }
        }
    }
}

internal val SampleSongs = listOf(
    FakeSong("s1", "海边的风", "林夕晨", "潮汐", "3:58"),
    FakeSong("s2", "Morning Light", "Aster", "Daybreak", "4:12"),
    FakeSong("s3", "雨后", "陈一", "小城故事", "3:31"),
    FakeSong("s4", "Paper Planes", "Nova", "Paper Planes", "3:05"),
    FakeSong("s5", "旧时光", "许诺", "旧时光", "4:40"),
    FakeSong("s6", "Glass City", "Lumen", "Neon Rain", "3:47"),
    FakeSong("s7", "长夜", "白桦", "北方", "5:02"),
    FakeSong("s8", "Northbound", "Kite", "Northbound", "3:20"),
)

internal val SampleLyrics = """
    [00:12.300]海风吹过安静的长街
    [00:17.850]灯一盏一盏亮起来
    [00:23.100]你说潮水会记得
    [00:28.640]所有没说出口的话
    [00:34.020]
    [00:35.400]Morning light on the window
    [00:40.910]We were young and we were slow
""".trimIndent()
