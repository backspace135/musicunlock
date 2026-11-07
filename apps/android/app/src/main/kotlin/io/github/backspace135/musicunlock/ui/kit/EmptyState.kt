package io.github.backspace135.musicunlock.ui.kit

import androidx.compose.animation.core.RepeatMode
import androidx.compose.animation.core.animateFloat
import androidx.compose.animation.core.infiniteRepeatable
import androidx.compose.animation.core.rememberInfiniteTransition
import androidx.compose.animation.core.tween
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ColumnScope
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.material3.Icon
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.graphicsLayer
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import io.github.backspace135.musicunlock.ui.theme.MuMotion
import io.github.backspace135.musicunlock.ui.theme.MuSpacing
import io.github.backspace135.musicunlock.ui.theme.MuTheme

/**
 * Centred placeholder for an empty list or a filter without results: icon in a grey disc (or your own
 * [artwork]), title, optional [body] (may contain "\n"), then [actions] (buttons, chips, a caption).
 * [drift] floats the artwork slowly up and down, like the macOS empty-state disc (2.8 s each way).
 */
@Composable
fun EmptyState(
    title: String,
    modifier: Modifier = Modifier,
    body: String? = null,
    icon: ImageVector? = null,
    artwork: (@Composable () -> Unit)? = null,
    drift: Boolean = false,
    actions: @Composable ColumnScope.() -> Unit = {},
) {
    val colors = MuTheme.colors
    Column(
        modifier.fillMaxWidth().padding(horizontal = MuSpacing.xxl, vertical = MuSpacing.xl),
        horizontalAlignment = Alignment.CenterHorizontally,
        verticalArrangement = Arrangement.Center,
    ) {
        val lift = if (drift) driftOffset() else null
        Box(
            Modifier.graphicsLayer { translationY = lift?.invoke() ?: 0f },
            contentAlignment = Alignment.Center,
        ) {
            when {
                artwork != null -> artwork()
                icon != null -> Box(Modifier.size(88.dp).clip(CircleShape).background(colors.fill), contentAlignment = Alignment.Center) {
                    Icon(icon, null, Modifier.size(40.dp), tint = colors.text)
                }
            }
        }
        Spacer(Modifier.height(MuSpacing.l))
        Text(title, style = MuTheme.type.headline, textAlign = TextAlign.Center)
        if (body != null) {
            Text(
                body,
                style = MuTheme.type.subtitle,
                color = colors.secondary,
                textAlign = TextAlign.Center,
                modifier = Modifier.padding(top = MuSpacing.s),
            )
        }
        Spacer(Modifier.height(MuSpacing.l))
        actions()
    }
}

/** Returns a reader for the drift offset in px, so the motion only invalidates the graphics layer. */
@Composable
private fun driftOffset(): () -> Float {
    val transition = rememberInfiniteTransition(label = "drift")
    val value = transition.animateFloat(
        0f,
        1f,
        infiniteRepeatable(tween(2800, easing = MuMotion.Smooth), RepeatMode.Reverse),
        label = "driftValue",
    )
    val distance = with(LocalDensity.current) { 5.dp.toPx() }
    return { -distance * value.value }
}
