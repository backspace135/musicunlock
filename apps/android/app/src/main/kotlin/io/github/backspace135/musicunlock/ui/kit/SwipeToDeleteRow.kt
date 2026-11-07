package io.github.backspace135.musicunlock.ui.kit

import androidx.compose.animation.core.animate
import androidx.compose.animation.core.animateFloatAsState
import androidx.compose.foundation.background
import androidx.compose.foundation.gestures.awaitEachGesture
import androidx.compose.foundation.gestures.awaitFirstDown
import androidx.compose.foundation.gestures.awaitHorizontalTouchSlopOrCancellation
import androidx.compose.foundation.gestures.horizontalDrag
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.size
import androidx.compose.material3.Icon
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableFloatStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.rememberUpdatedState
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.graphicsLayer
import androidx.compose.ui.hapticfeedback.HapticFeedbackType
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.input.pointer.positionChange
import androidx.compose.ui.input.pointer.util.VelocityTracker
import androidx.compose.ui.input.pointer.util.addPointerInputChange
import androidx.compose.ui.layout.layout
import androidx.compose.ui.layout.onSizeChanged
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.platform.LocalHapticFeedback
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.CustomAccessibilityAction
import androidx.compose.ui.semantics.customActions
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.unit.Constraints
import androidx.compose.ui.unit.dp
import io.github.backspace135.musicunlock.R
import io.github.backspace135.musicunlock.ui.theme.MuMotion
import io.github.backspace135.musicunlock.ui.theme.MuTheme
import kotlin.math.max
import kotlin.math.roundToInt
import kotlinx.coroutines.Job
import kotlinx.coroutines.launch

/**
 * Row that is removed by swiping left: the ink action ([label], default "删除") is revealed under the
 * finger, and releasing past it (or flinging left) slides the row out and calls [onDelete]. Releasing
 * earlier springs the row back. [onDelete] must remove the item from the list; with `animateItem()` on
 * the lazy item the gap then closes smoothly. A right swipe is left alone, so the page can still be
 * swiped back. TalkBack users get the same action as a custom action. [background] must be opaque and
 * match what the row sits on.
 */
@Composable
fun SwipeToDeleteRow(
    onDelete: () -> Unit,
    modifier: Modifier = Modifier,
    label: String = stringResource(R.string.common_delete),
    enabled: Boolean = true,
    background: Color = MuTheme.colors.surface,
    content: @Composable () -> Unit,
) {
    val colors = MuTheme.colors
    val haptics = LocalHapticFeedback.current
    val scope = rememberCoroutineScope()
    val currentOnDelete by rememberUpdatedState(onDelete)
    val currentEnabled by rememberUpdatedState(enabled)
    val actionWidth = with(LocalDensity.current) { 88.dp.toPx() }
    var width by remember { mutableFloatStateOf(0f) }
    var offset by remember { mutableFloatStateOf(0f) }
    var armed by remember { mutableStateOf(false) }
    var settle: Job? by remember { mutableStateOf(null) }
    val iconScale by animateFloatAsState(if (armed) 1.15f else 1f, MuMotion.toggle(), label = "deleteArmed")

    // Disabled while shifted (for example the job just started): spring back.
    LaunchedEffect(enabled) {
        if (!enabled && offset != 0f) animate(offset, 0f, 0f, MuMotion.pageSettle()) { value, _ -> offset = value }
    }

    fun slideOut(velocity: Float) {
        settle = scope.launch {
            animate(offset, -width, velocity, MuMotion.pageSettle()) { value, _ -> offset = value }
            currentOnDelete()
        }
    }

    Box(
        modifier
            .onSizeChanged { width = it.width.toFloat() }
            .semantics {
                customActions = listOf(CustomAccessibilityAction(label) { if (enabled) slideOut(0f); enabled })
            },
    ) {
        // The action behind the row, as wide as the revealed part (at least the action width).
        Box(
            Modifier
                .matchParentSize()
                .graphicsLayer { alpha = if (offset < 0f) 1f else 0f }
                .background(colors.text),
        ) {
            Column(
                Modifier
                    .align(Alignment.CenterEnd)
                    .fillMaxHeight()
                    .layout { measurable, constraints ->
                        val w = max(actionWidth, -offset).roundToInt().coerceAtMost(constraints.maxWidth)
                        val placeable = measurable.measure(Constraints.fixed(w, constraints.maxHeight))
                        layout(w, placeable.height) { placeable.place(0, 0) }
                    },
                horizontalAlignment = Alignment.CenterHorizontally,
                verticalArrangement = Arrangement.Center,
            ) {
                Icon(
                    MuIcons.Delete,
                    null,
                    Modifier.size(22.dp).graphicsLayer {
                        scaleX = iconScale
                        scaleY = iconScale
                    },
                    tint = colors.onText,
                )
                Text(label, style = MuTheme.type.caption, color = colors.onText)
            }
        }
        Box(
            Modifier
                .graphicsLayer { translationX = offset }
                .background(background)
                .pointerInput(Unit) {
                    awaitEachGesture {
                        val down = awaitFirstDown(requireUnconsumed = false)
                        if (!currentEnabled) return@awaitEachGesture
                        val tracker = VelocityTracker()
                        tracker.addPointerInputChange(down)
                        val drag = awaitHorizontalTouchSlopOrCancellation(down.id) { change, over ->
                            // Only a left swipe (or moving an already shifted row) belongs to the row.
                            if (over < 0f || offset < 0f) {
                                change.consume()
                                settle?.cancel()
                                offset = (offset + over).coerceIn(-width, 0f)
                            }
                        } ?: return@awaitEachGesture
                        tracker.addPointerInputChange(drag)
                        val completed = horizontalDrag(drag.id) { change ->
                            tracker.addPointerInputChange(change)
                            offset = (offset + change.positionChange().x).coerceIn(-width, 0f)
                            val nowArmed = -offset > actionWidth
                            if (nowArmed != armed) {
                                armed = nowArmed
                                haptics.performHapticFeedback(HapticFeedbackType.GestureThresholdActivate)
                            }
                            change.consume()
                        }
                        val velocity = tracker.calculateVelocity().x
                        val fling = velocity < -1500f && -offset > actionWidth / 3
                        if (completed && currentEnabled && (armed || fling)) {
                            slideOut(velocity)
                        } else {
                            settle = scope.launch { animate(offset, 0f, velocity, MuMotion.pageSettle()) { value, _ -> offset = value } }
                        }
                        armed = false
                    }
                },
        ) {
            content()
        }
    }
}
