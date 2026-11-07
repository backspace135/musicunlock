package io.github.backspace135.musicunlock.ui.kit

import androidx.compose.animation.core.animateFloatAsState
import androidx.compose.foundation.Canvas
import androidx.compose.foundation.interaction.MutableInteractionSource
import androidx.compose.foundation.interaction.collectIsPressedAsState
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.selection.toggleable
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.remember
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.alpha
import androidx.compose.ui.geometry.CornerRadius
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.Path
import androidx.compose.ui.graphics.PathMeasure
import androidx.compose.ui.graphics.StrokeCap
import androidx.compose.ui.graphics.StrokeJoin
import androidx.compose.ui.graphics.drawscope.DrawScope
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.graphics.lerp
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.unit.dp
import io.github.backspace135.musicunlock.ui.theme.MuMotion
import io.github.backspace135.musicunlock.ui.theme.MuTheme
import androidx.compose.ui.util.lerp as lerpFloat

/**
 * On / off switch. The thumb springs across, stretches while pressed, and shows a check mark when on, so
 * the state reads without colour. Inside a [SwitchRow] pass `onCheckedChange = null` and the row's
 * [interactionSource]; the row handles the tap.
 */
@Composable
fun MuSwitch(
    checked: Boolean,
    onCheckedChange: ((Boolean) -> Unit)?,
    modifier: Modifier = Modifier,
    enabled: Boolean = true,
    interactionSource: MutableInteractionSource? = null,
) {
    val source = interactionSource ?: remember { MutableInteractionSource() }
    val pressed by source.collectIsPressedAsState()
    val position by animateFloatAsState(if (checked) 1f else 0f, MuMotion.toggle(), label = "switchPosition")
    val stretch by animateFloatAsState(if (pressed && enabled) 1f else 0f, MuMotion.toggle(), label = "switchStretch")
    val colors = MuTheme.colors
    val toggle = if (onCheckedChange != null) {
        Modifier.toggleable(checked, source, indication = null, enabled = enabled, role = Role.Switch, onValueChange = onCheckedChange)
    } else {
        Modifier
    }
    Canvas(modifier.then(toggle).alpha(if (enabled) 1f else DisabledAlpha).size(52.dp, 32.dp)) {
        val inset = 3.dp.toPx()
        val thumbHeight = size.height - inset * 2
        val thumbWidth = thumbHeight + 6.dp.toPx() * stretch
        val track = lerp(colors.separator, colors.text, position.coerceIn(0f, 1f))
        drawRoundRect(track, cornerRadius = CornerRadius(size.height / 2))
        val x = lerpFloat(inset, size.width - inset - thumbWidth, position)
        val thumb = lerp(colors.thumb, colors.onText, position.coerceIn(0f, 1f))
        drawRoundRect(
            Color.Black.copy(alpha = 0.12f),
            topLeft = Offset(x, inset + 1.dp.toPx()),
            size = Size(thumbWidth, thumbHeight),
            cornerRadius = CornerRadius(thumbHeight / 2),
        )
        drawRoundRect(thumb, topLeft = Offset(x, inset), size = Size(thumbWidth, thumbHeight), cornerRadius = CornerRadius(thumbHeight / 2))
        val mark = position.coerceIn(0f, 1f)
        if (mark > 0f) {
            val box = 12.dp.toPx()
            drawCheck(
                topLeft = Offset(x + (thumbWidth - box) / 2, inset + (thumbHeight - box) / 2),
                box = box,
                progress = mark,
                color = colors.text,
                width = 2.dp.toPx(),
            )
        }
    }
}

/**
 * Round check box for multi-select (Telegram style). Checking fills the circle with a spring bounce and
 * draws the check mark; unchecked is an empty ring. Inside a row pass `onCheckedChange = null` and let the
 * row toggle.
 */
@Composable
fun MuCheckbox(
    checked: Boolean,
    onCheckedChange: ((Boolean) -> Unit)?,
    modifier: Modifier = Modifier,
    enabled: Boolean = true,
) {
    val fill by animateFloatAsState(if (checked) 1f else 0f, MuMotion.toggle(), label = "checkFill")
    val colors = MuTheme.colors
    val toggle = if (onCheckedChange != null) {
        Modifier.pressable({ onCheckedChange(!checked) }, enabled = enabled, role = Role.Checkbox, pressedScale = 0.85f)
    } else {
        Modifier
    }
    Canvas(modifier.then(toggle).alpha(if (enabled) 1f else DisabledAlpha).size(22.dp)) {
        val radius = size.minDimension / 2
        drawCircle(colors.secondary, radius - 0.75.dp.toPx(), style = Stroke(1.5.dp.toPx()))
        if (fill > 0f) {
            drawCircle(colors.text, radius * fill.coerceAtMost(1.15f))
            val box = size.minDimension * 0.56f
            drawCheck(
                topLeft = Offset((size.width - box) / 2, (size.height - box) / 2),
                box = box,
                progress = ((fill - 0.3f) / 0.7f).coerceIn(0f, 1f),
                color = colors.onText,
                width = 2.dp.toPx(),
            )
        }
    }
}

/** Draws the first [progress] of a check mark inside a [box]-sized square. */
internal fun DrawScope.drawCheck(topLeft: Offset, box: Float, progress: Float, color: Color, width: Float) {
    val path = Path().apply {
        moveTo(topLeft.x + box * 0.1f, topLeft.y + box * 0.52f)
        lineTo(topLeft.x + box * 0.4f, topLeft.y + box * 0.8f)
        lineTo(topLeft.x + box * 0.92f, topLeft.y + box * 0.22f)
    }
    val drawn = if (progress >= 1f) {
        path
    } else {
        val measure = PathMeasure().apply { setPath(path, false) }
        Path().also { measure.getSegment(0f, measure.length * progress, it, true) }
    }
    drawPath(drawn, color, style = Stroke(width, cap = StrokeCap.Round, join = StrokeJoin.Round))
}
