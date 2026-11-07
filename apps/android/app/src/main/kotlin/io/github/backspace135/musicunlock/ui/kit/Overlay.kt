package io.github.backspace135.musicunlock.ui.kit

import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.runtime.Composable
import androidx.compose.runtime.CompositionLocalContext
import androidx.compose.runtime.CompositionLocalProvider
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.SideEffect
import androidx.compose.runtime.Stable
import androidx.compose.runtime.currentCompositionLocalContext
import androidx.compose.runtime.getValue
import androidx.compose.runtime.key
import androidx.compose.runtime.mutableStateListOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.runtime.staticCompositionLocalOf
import androidx.compose.ui.Modifier

/**
 * Sheets, dialogs and menus draw in a layer above the whole app (pages, the bottom navigation, system bar
 * areas), wherever in the tree they are declared. [OverlayHostBox] provides that layer once at the root;
 * the components use [Overlay]. Pages never call either directly.
 */
@Stable
class OverlayHost internal constructor() {
    internal val layers = mutableStateListOf<OverlayLayer>()
}

internal class OverlayLayer {
    var content by mutableStateOf<@Composable () -> Unit>({})
    var locals by mutableStateOf<CompositionLocalContext?>(null)
}

private val LocalOverlayHost = staticCompositionLocalOf<OverlayHost?> { null }

@Composable
fun OverlayHostBox(modifier: Modifier = Modifier, content: @Composable () -> Unit) {
    val host = remember { OverlayHost() }
    CompositionLocalProvider(LocalOverlayHost provides host) {
        Box(modifier.fillMaxSize()) {
            content()
            host.layers.forEach { layer ->
                key(layer) {
                    val locals = layer.locals
                    if (locals != null) CompositionLocalProvider(locals) { layer.content() } else layer.content()
                }
            }
        }
    }
}

/**
 * Shows [content] in the root overlay while this call stays in composition. The content sees the same
 * composition locals as the caller (theme, navigator, ViewModel owner).
 */
@Composable
internal fun Overlay(content: @Composable () -> Unit) {
    val host = checkNotNull(LocalOverlayHost.current) { "Overlay needs OverlayHostBox at the root of the app" }
    val layer = remember { OverlayLayer() }
    val locals = currentCompositionLocalContext
    SideEffect {
        layer.content = content
        layer.locals = locals
    }
    DisposableEffect(host, layer) {
        host.layers += layer
        onDispose { host.layers -= layer }
    }
}
