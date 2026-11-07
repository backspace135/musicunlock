package io.github.backspace135.musicunlock.ui.kit

import androidx.compose.animation.AnimatedContent
import androidx.compose.animation.core.LinearEasing
import androidx.compose.animation.core.RepeatMode
import androidx.compose.animation.core.animateFloat
import androidx.compose.animation.core.animateFloatAsState
import androidx.compose.animation.core.infiniteRepeatable
import androidx.compose.animation.core.rememberInfiniteTransition
import androidx.compose.animation.core.spring
import androidx.compose.animation.core.tween
import androidx.compose.animation.fadeIn
import androidx.compose.animation.fadeOut
import androidx.compose.animation.scaleIn
import androidx.compose.animation.togetherWith
import androidx.compose.foundation.Canvas
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.size
import androidx.compose.material3.LocalContentColor
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.CornerRadius
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.PathEffect
import androidx.compose.ui.graphics.StrokeCap
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.progressBarRangeInfo
import androidx.compose.ui.semantics.ProgressBarRangeInfo
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import io.github.backspace135.musicunlock.ui.theme.MuTheme

/** Job state as a glyph, as on macOS and Windows. Always show the state's text next to it as well. */
enum class JobGlyph {
    /** Dashed circle. */
    Pending,
    /** Spinner. */
    Running,
    /** Filled circle with a check; bounces once when a job finishes. */
    Done,
    /** Ring with an exclamation mark. */
    Failed,
}

/**
 * State glyph for queue rows. Changing [state] pops the new glyph in from 60 %; [JobGlyph.Done] overshoots
 * once. [contentDescription] is the state text (for example the core's `state_label`).
 */
@Composable
fun StatusGlyph(state: JobGlyph, contentDescription: String?, modifier: Modifier = Modifier, size: Dp = 20.dp) {
    val semantics = if (contentDescription != null) Modifier.semantics { this.contentDescription = contentDescription } else Modifier
    AnimatedContent(
        targetState = state,
        modifier = modifier.then(semantics).size(size),
        transitionSpec = {
            val grow = if (targetState == JobGlyph.Done) spring<Float>(0.4f, 700f) else tween(250)
            (fadeIn(tween(150)) + scaleIn(grow, initialScale = 0.6f)) togetherWith fadeOut(tween(100))
        },
        contentAlignment = Alignment.Center,
        label = "statusGlyph",
    ) { glyph ->
        val ink = MuTheme.colors.text
        val onInk = MuTheme.colors.onText
        when (glyph) {
            JobGlyph.Running -> MuSpinner(size = size)
            JobGlyph.Pending -> Canvas(Modifier.size(size)) {
                val stroke = 1.5.dp.toPx()
                drawCircle(
                    ink,
                    this.size.minDimension / 2 - stroke,
                    style = Stroke(stroke, pathEffect = PathEffect.dashPathEffect(floatArrayOf(2.5.dp.toPx(), 2.dp.toPx()))),
                )
            }
            JobGlyph.Done -> Canvas(Modifier.size(size)) {
                drawCircle(ink)
                val box = this.size.minDimension * 0.56f
                drawCheck(Offset((this.size.width - box) / 2, (this.size.height - box) / 2), box, 1f, onInk, 2.dp.toPx())
            }
            JobGlyph.Failed -> Canvas(Modifier.size(size)) {
                val stroke = 1.5.dp.toPx()
                val d = this.size.minDimension
                drawCircle(ink, d / 2 - stroke / 2, style = Stroke(stroke))
                drawLine(ink, Offset(d / 2, d * 0.27f), Offset(d / 2, d * 0.58f), 2.dp.toPx(), StrokeCap.Round)
                drawCircle(ink, 1.2.dp.toPx(), Offset(d / 2, d * 0.73f))
            }
        }
    }
}

/** Small indeterminate spinner ("正在扫描文件…", loading rows). Pair it with text saying what is loading. */
@Composable
fun MuSpinner(modifier: Modifier = Modifier, size: Dp = 20.dp, color: Color = LocalContentColor.current) {
    val transition = rememberInfiniteTransition(label = "spinner")
    val rotation by transition.animateFloat(
        0f,
        360f,
        infiniteRepeatable(tween(900, easing = LinearEasing), RepeatMode.Restart),
        label = "spinnerRotation",
    )
    Canvas(modifier.size(size)) {
        val stroke = 2.dp.toPx()
        drawArc(
            color,
            startAngle = rotation - 90f,
            sweepAngle = 270f,
            useCenter = false,
            topLeft = Offset(stroke / 2, stroke / 2),
            size = Size(this.size.width - stroke, this.size.height - stroke),
            style = Stroke(stroke, cap = StrokeCap.Round),
        )
    }
}

/** Determinate progress bar, 4 dp capsule. [progress] is 0..1; changes ease in without overshoot. */
@Composable
fun MuProgressBar(progress: Float, modifier: Modifier = Modifier) {
    val shown by animateFloatAsState(progress.coerceIn(0f, 1f), spring(1f, 300f), label = "progress")
    val colors = MuTheme.colors
    Box(
        modifier
            .fillMaxWidth()
            .height(4.dp)
            .semantics { progressBarRangeInfo = ProgressBarRangeInfo(progress.coerceIn(0f, 1f), 0f..1f) },
    ) {
        Canvas(Modifier.fillMaxWidth().height(4.dp)) {
            val radius = CornerRadius(size.height / 2)
            drawRoundRect(colors.fill, cornerRadius = radius)
            if (shown > 0f) drawRoundRect(colors.text, size = Size(size.width * shown, size.height), cornerRadius = radius)
        }
    }
}
