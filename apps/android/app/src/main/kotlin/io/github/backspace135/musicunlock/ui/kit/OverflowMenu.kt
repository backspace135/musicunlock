package io.github.backspace135.musicunlock.ui.kit

import androidx.activity.compose.BackHandler
import androidx.compose.animation.AnimatedVisibility
import androidx.compose.animation.core.spring
import androidx.compose.animation.core.updateTransition
import androidx.compose.animation.fadeIn
import androidx.compose.animation.fadeOut
import androidx.compose.animation.scaleIn
import androidx.compose.animation.scaleOut
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.gestures.detectTapGestures
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.runtime.Composable
import androidx.compose.runtime.Immutable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.draw.shadow
import androidx.compose.ui.geometry.Rect
import androidx.compose.ui.graphics.TransformOrigin
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.layout.Layout
import androidx.compose.ui.layout.boundsInRoot
import androidx.compose.ui.layout.onGloballyPositioned
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.unit.Constraints
import androidx.compose.ui.unit.dp
import io.github.backspace135.musicunlock.R
import io.github.backspace135.musicunlock.ui.theme.MuShapes
import io.github.backspace135.musicunlock.ui.theme.MuSpacing
import io.github.backspace135.musicunlock.ui.theme.MuTheme
import kotlin.math.roundToInt

/** One entry of an [OverflowMenu]. */
@Immutable
data class MenuItem(
    val label: String,
    val enabled: Boolean = true,
    val icon: ImageVector? = null,
    val onClick: () -> Unit,
)

/**
 * The "⋯" button of a top bar with its menu. The menu grows out of the button's corner with a spring and
 * closes when an item is chosen, the outside is tapped or back is pressed.
 */
@Composable
fun OverflowMenu(items: List<MenuItem>, modifier: Modifier = Modifier, contentDescription: String = stringResource(R.string.common_more)) {
    var expanded by remember { mutableStateOf(false) }
    var anchor by remember { mutableStateOf(Rect.Zero) }
    Box(modifier.onGloballyPositioned { anchor = it.boundsInRoot() }) {
        IconAction(MuIcons.More, contentDescription, { expanded = true })
    }
    val transition = updateTransition(expanded, label = "overflowMenu")
    if (!transition.currentState && !transition.targetState) return
    Overlay {
        BackHandler(enabled = expanded) { expanded = false }
        Box(Modifier.fillMaxSize().pointerInput(Unit) { detectTapGestures { expanded = false } })
        MenuPosition(anchor) {
            transition.AnimatedVisibility(
                { it },
                enter = fadeIn() + scaleIn(spring(0.72f, 700f), initialScale = 0.6f, transformOrigin = TransformOrigin(1f, 0f)),
                exit = fadeOut() + scaleOut(targetScale = 0.9f, transformOrigin = TransformOrigin(1f, 0f)),
            ) {
                val colors = MuTheme.colors
                Column(
                    Modifier
                        .width(MenuWidth)
                        .shadow(16.dp, MuShapes.medium, ambientColor = colors.scrim, spotColor = colors.scrim)
                        .clip(MuShapes.medium)
                        .background(colors.elevated)
                        .border(0.5.dp, colors.separator, MuShapes.medium)
                        .verticalScroll(rememberScrollState())
                        .padding(vertical = MuSpacing.xs),
                ) {
                    items.forEach { item ->
                        ListRow(
                            title = item.label,
                            leading = item.icon?.let { { RowIcon(it) } },
                            enabled = item.enabled,
                            onClick = {
                                expanded = false
                                item.onClick()
                            },
                        )
                    }
                }
            }
        }
    }
}

private val MenuWidth = 220.dp

/** Places the menu below the anchor, right-aligned with it, kept inside the screen. */
@Composable
private fun MenuPosition(anchor: Rect, content: @Composable () -> Unit) {
    Layout(content, Modifier.fillMaxSize()) { measurables, constraints ->
        val margin = 8.dp.roundToPx()
        val placeables = measurables.map { it.measure(Constraints(maxWidth = constraints.maxWidth - margin * 2, maxHeight = constraints.maxHeight - margin * 2)) }
        layout(constraints.maxWidth, constraints.maxHeight) {
            placeables.forEach { placeable ->
                val x = (anchor.right.roundToInt() - placeable.width).coerceIn(margin, constraints.maxWidth - placeable.width - margin)
                val y = (anchor.top.roundToInt() + 4.dp.roundToPx()).coerceIn(margin, constraints.maxHeight - placeable.height - margin)
                placeable.place(x, y)
            }
        }
    }
}
