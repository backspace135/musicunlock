package io.github.backspace135.musicunlock.ui.kit

import android.os.SystemClock
import androidx.compose.animation.core.Animatable
import androidx.compose.foundation.combinedClickable
import androidx.compose.foundation.interaction.InteractionSource
import androidx.compose.foundation.interaction.MutableInteractionSource
import androidx.compose.foundation.interaction.PressInteraction
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.remember
import androidx.compose.ui.Modifier
import androidx.compose.ui.composed
import androidx.compose.ui.graphics.graphicsLayer
import androidx.compose.ui.semantics.Role
import io.github.backspace135.musicunlock.ui.theme.MuMotion
import kotlinx.coroutines.Job
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch

/**
 * Shrinks the element while [interactionSource] is pressed and springs it back with a small overshoot.
 * A quick tap still shows the press for [MuMotion.pressMinMillis]. Place it before `background` / `clip`
 * so the whole element scales.
 */
fun Modifier.pressScale(interactionSource: InteractionSource, pressedScale: Float = MuMotion.pressScale): Modifier = composed {
    val scale = remember { Animatable(1f) }
    LaunchedEffect(interactionSource, pressedScale) {
        var pressedAt = 0L
        var release: Job? = null
        interactionSource.interactions.collect { interaction ->
            when (interaction) {
                is PressInteraction.Press -> {
                    release?.cancel()
                    pressedAt = SystemClock.uptimeMillis()
                    launch { scale.animateTo(pressedScale, MuMotion.pressDown()) }
                }
                is PressInteraction.Release, is PressInteraction.Cancel -> {
                    val held = SystemClock.uptimeMillis() - pressedAt
                    release = launch {
                        delay((MuMotion.pressMinMillis - held).coerceAtLeast(0))
                        scale.animateTo(1f, MuMotion.press())
                    }
                }
            }
        }
    }
    graphicsLayer {
        scaleX = scale.value
        scaleY = scale.value
    }
}

/**
 * Clickable with the spring press feedback instead of a ripple. Use it for buttons, cards and anything
 * that is not a list row (rows use [ListRow], which highlights instead of scaling).
 */
fun Modifier.pressable(
    onClick: () -> Unit,
    enabled: Boolean = true,
    onLongClick: (() -> Unit)? = null,
    role: Role? = Role.Button,
    onClickLabel: String? = null,
    pressedScale: Float = MuMotion.pressScale,
): Modifier = composed {
    val source = remember { MutableInteractionSource() }
    pressScale(source, pressedScale).combinedClickable(
        interactionSource = source,
        indication = null,
        enabled = enabled,
        onClickLabel = onClickLabel,
        role = role,
        onLongClick = onLongClick,
        onClick = onClick,
    )
}
