package io.github.backspace135.musicunlock.ui.theme

import android.content.SharedPreferences
import android.graphics.Color as AndroidColor
import androidx.activity.ComponentActivity
import androidx.activity.SystemBarStyle
import androidx.activity.enableEdgeToEdge
import androidx.compose.animation.core.Animatable
import androidx.compose.animation.core.tween
import androidx.compose.foundation.isSystemInDarkTheme
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.runtime.Composable
import androidx.compose.runtime.CompositionLocalProvider
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.SideEffect
import androidx.compose.runtime.Stable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.runtime.staticCompositionLocalOf
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.drawWithContent
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Rect
import androidx.compose.ui.graphics.ClipOp
import androidx.compose.ui.graphics.ImageBitmap
import androidx.compose.ui.graphics.Path
import androidx.compose.ui.graphics.drawscope.clipPath
import androidx.compose.ui.graphics.layer.drawLayer
import androidx.compose.ui.graphics.rememberGraphicsLayer
import androidx.compose.ui.platform.LocalContext
import kotlin.math.hypot
import kotlinx.coroutines.launch

/** Theme choice in 设置 → 外观. */
enum class ThemeMode(internal val key: String) {
    System("system"),
    Light("light"),
    Dark("dark"),
    ;

    fun isDark(systemDark: Boolean): Boolean = when (this) {
        System -> systemDark
        Light -> false
        Dark -> true
    }

    internal companion object {
        fun fromKey(key: String?): ThemeMode = entries.firstOrNull { it.key == key } ?: System
    }
}

/**
 * Holds and saves the [ThemeMode]. [setMode] switches with a circular reveal of the new theme growing
 * from `origin` (root coordinates, usually the centre of the control that was tapped).
 */
@Stable
class ThemeController(private val prefs: SharedPreferences) {
    var mode by mutableStateOf(ThemeMode.fromKey(prefs.getString(PrefKey, null)))
        private set

    internal var transition: ((ThemeMode, Offset?) -> Unit)? = null

    fun setMode(mode: ThemeMode, origin: Offset? = null) {
        if (mode == this.mode) return
        transition?.invoke(mode, origin) ?: apply(mode)
    }

    internal fun apply(mode: ThemeMode) {
        this.mode = mode
        prefs.edit().putString(PrefKey, mode.key).apply()
    }

    private companion object {
        const val PrefKey = "themeMode"
    }
}

val LocalThemeController = staticCompositionLocalOf<ThemeController> { error("No ThemeController: use MuThemeHost") }

/**
 * Root of the app: [MuTheme] for the controller's mode, the system bar icon colours, and the theme reveal.
 * When the mode changes the current frame is captured, the new theme is drawn underneath, and the captured
 * frame is cut away by a growing circle, so the switch is a visible transition rather than a flash.
 */
@Composable
fun MuThemeHost(controller: ThemeController, content: @Composable () -> Unit) {
    val systemDark = isSystemInDarkTheme()
    val dark = controller.mode.isDark(systemDark)
    val layer = rememberGraphicsLayer()
    val reveal = remember { Animatable(1f) }
    var snapshot by remember { mutableStateOf<ImageBitmap?>(null) }
    var origin by remember { mutableStateOf(Offset.Zero) }
    val scope = rememberCoroutineScope()

    SideEffect {
        controller.transition = { mode, at ->
            if (snapshot != null || mode.isDark(systemDark) == dark) {
                controller.apply(mode)
            } else {
                scope.launch {
                    snapshot = layer.toImageBitmap()
                    origin = at ?: Offset(layer.size.width / 2f, layer.size.height / 2f)
                    reveal.snapTo(0f)
                    controller.apply(mode)
                    try {
                        reveal.animateTo(1f, tween(MuMotion.themeRevealMillis, easing = MuMotion.Smooth))
                    } finally {
                        snapshot = null
                    }
                }
            }
        }
    }

    val activity = LocalContext.current as? ComponentActivity
    LaunchedEffect(dark, activity) {
        val transparent = AndroidColor.TRANSPARENT
        val style = if (dark) SystemBarStyle.dark(transparent) else SystemBarStyle.light(transparent, transparent)
        activity?.enableEdgeToEdge(statusBarStyle = style, navigationBarStyle = style)
    }

    CompositionLocalProvider(LocalThemeController provides controller) {
        MuTheme(dark) {
            Box(
                Modifier
                    .fillMaxSize()
                    .drawWithContent {
                        layer.record { this@drawWithContent.drawContent() }
                        drawLayer(layer)
                        val frame = snapshot ?: return@drawWithContent
                        val farthest = maxOf(
                            hypot(origin.x, origin.y),
                            hypot(size.width - origin.x, origin.y),
                            hypot(origin.x, size.height - origin.y),
                            hypot(size.width - origin.x, size.height - origin.y),
                        )
                        val circle = Path().apply { addOval(Rect(origin, farthest * reveal.value)) }
                        clipPath(circle, ClipOp.Difference) { drawImage(frame) }
                    },
            ) {
                content()
            }
        }
    }
}
