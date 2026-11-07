package io.github.backspace135.musicunlock.ui.kit

import androidx.activity.compose.PredictiveBackHandler
import androidx.compose.animation.core.animate
import androidx.compose.foundation.background
import androidx.compose.foundation.gestures.Orientation
import androidx.compose.foundation.gestures.detectTapGestures
import androidx.compose.foundation.gestures.draggable
import androidx.compose.foundation.gestures.rememberDraggableState
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.BoxWithConstraints
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ColumnScope
import androidx.compose.foundation.layout.WindowInsets
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.ime
import androidx.compose.foundation.layout.navigationBars
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.union
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.layout.windowInsetsPadding
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.Stable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableFloatStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.rememberUpdatedState
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.draw.drawBehind
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.graphicsLayer
import androidx.compose.ui.input.nestedscroll.NestedScrollConnection
import androidx.compose.ui.input.nestedscroll.NestedScrollSource
import androidx.compose.ui.input.nestedscroll.nestedScroll
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.layout.onSizeChanged
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.semantics.paneTitle
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.unit.Velocity
import androidx.compose.ui.unit.dp
import io.github.backspace135.musicunlock.ui.theme.MuMotion
import io.github.backspace135.musicunlock.ui.theme.MuShapes
import io.github.backspace135.musicunlock.ui.theme.MuSpacing
import io.github.backspace135.musicunlock.ui.theme.MuTheme
import kotlin.coroutines.cancellation.CancellationException
import kotlinx.coroutines.Job
import kotlinx.coroutines.launch

/**
 * Modal bottom sheet over the whole app. Drag the handle, the title or any non-scrolling part to move
 * it; a scrolling list inside hands over to the sheet once it is at its top. On release the sheet keeps
 * the finger's velocity and snaps open or closed; the scrim darkens and clears with the sheet's position.
 * Tapping the scrim, dragging it down, or system back calls [onDismissRequest]; set [visible] to false
 * there. The sheet is at most 92 % of the screen tall; put long content in a `LazyColumn` or
 * `Column(Modifier.verticalScroll(...))`.
 */
@Composable
fun MuBottomSheet(
    visible: Boolean,
    onDismissRequest: () -> Unit,
    modifier: Modifier = Modifier,
    title: String? = null,
    content: @Composable ColumnScope.() -> Unit,
) {
    val state = remember { SheetState() }
    val scope = rememberCoroutineScope()
    var present by remember { mutableStateOf(false) }
    val currentVisible by rememberUpdatedState(visible)
    val currentDismiss by rememberUpdatedState(onDismissRequest)

    LaunchedEffect(visible) {
        if (visible) {
            present = true
        } else if (present) {
            state.animateTo(state.height, 0f)
            present = false
            state.reset()
        }
    }
    if (!present) return

    fun settle(velocity: Float) {
        state.launch(scope) {
            if (sheetShouldDismiss(state.offset, velocity, state.height, MuMotion.sheetProjection)) {
                state.animateTo(state.height, velocity)
                currentDismiss()
                // The caller kept the sheet: come back up.
                if (currentVisible) state.animateTo(0f, 0f)
            } else {
                state.animateTo(0f, velocity)
            }
        }
    }

    val connection = remember(state) { SheetScrollConnection(state, ::settle) }

    Overlay {
        PredictiveBackHandler(enabled = visible) { events ->
            try {
                events.collect { event -> state.offset = state.height * 0.2f * event.progress }
                currentDismiss()
            } catch (cancelled: CancellationException) {
                state.launch(scope) { state.animateTo(0f, 0f) }
                throw cancelled
            }
        }
        val colors = MuTheme.colors
        BoxWithConstraints(Modifier.fillMaxSize()) {
            val maxSheetHeight = maxHeight * 0.92f
            Box(
                Modifier
                    .fillMaxSize()
                    .drawBehind { drawRect(colors.scrim, alpha = state.fraction) }
                    .pointerInput(Unit) { detectTapGestures { if (currentVisible) currentDismiss() } },
            )
            Column(
                modifier
                    .align(Alignment.BottomCenter)
                    .fillMaxWidth()
                    .heightIn(max = maxSheetHeight)
                    .onSizeChanged { size ->
                        val opening = state.height == 0f
                        state.height = size.height.toFloat()
                        if (opening) {
                            state.offset = state.height
                            state.launch(scope) { state.animateTo(0f, 0f) }
                        }
                    }
                    .graphicsLayer {
                        translationY = state.offset
                        // Invisible until measured, so it never flashes fully open on the first frame.
                        alpha = if (state.height == 0f) 0f else 1f
                    }
                    .clip(MuShapes.sheet)
                    .background(colors.elevated)
                    .semantics { if (title != null) paneTitle = title }
                    .nestedScroll(connection)
                    .draggable(
                        rememberDraggableState { delta -> state.dragBy(delta) },
                        Orientation.Vertical,
                        onDragStarted = { state.stop() },
                        onDragStopped = { velocity -> settle(velocity) },
                    )
                    .windowInsetsPadding(WindowInsets.navigationBars.union(WindowInsets.ime)),
            ) {
                Box(Modifier.fillMaxWidth().padding(top = MuSpacing.s, bottom = MuSpacing.xs), contentAlignment = Alignment.Center) {
                    Box(Modifier.width(36.dp).height(4.dp).clip(MuShapes.capsule).background(colors.secondary.copy(alpha = 0.4f)))
                }
                if (title != null) {
                    Text(
                        title,
                        style = MuTheme.type.title,
                        modifier = Modifier.padding(horizontal = MuSpacing.page + MuSpacing.xs, vertical = MuSpacing.s),
                    )
                }
                content()
            }
        }
    }
}

/** True when a sheet released at [offset] (0 = open, [height] = closed) with [velocity] should close. */
internal fun sheetShouldDismiss(offset: Float, velocity: Float, height: Float, projectionSeconds: Float): Boolean {
    if (height <= 0f) return true
    return offset + velocity * projectionSeconds > height / 2
}

@Stable
private class SheetState {
    var height by mutableFloatStateOf(0f)
    /** Distance the sheet is pushed down from fully open; negative while over-dragged upwards. */
    var offset by mutableFloatStateOf(0f)
    private var job: Job? = null

    /** 0 when closed, 1 when open: drives the scrim. */
    val fraction: Float get() = if (height <= 0f) 0f else (1f - offset / height).coerceIn(0f, 1f)

    fun launch(scope: kotlinx.coroutines.CoroutineScope, block: suspend SheetState.() -> Unit) {
        job?.cancel()
        job = scope.launch { block() }
    }

    fun stop() {
        job?.cancel()
    }

    /** Moves by a finger delta. Above the open position it resists, like a rubber band. */
    fun dragBy(delta: Float) {
        val resisted = if (offset + delta < 0f) delta * 0.25f else delta
        offset = (offset + resisted).coerceIn(-height * 0.05f, height)
    }

    suspend fun animateTo(target: Float, velocity: Float) {
        animate(offset, target, velocity, MuMotion.sheet()) { value, _ -> offset = value }
    }

    fun reset() {
        height = 0f
        offset = 0f
    }
}

/** Lets a list inside the sheet move the sheet when the list cannot scroll further. */
private class SheetScrollConnection(private val state: SheetState, private val settle: (Float) -> Unit) : NestedScrollConnection {
    override fun onPreScroll(available: Offset, source: NestedScrollSource): Offset {
        if (available.y < 0f && state.offset > 0f && source == NestedScrollSource.UserInput) {
            val before = state.offset
            state.offset = (state.offset + available.y).coerceAtLeast(0f)
            return Offset(0f, state.offset - before)
        }
        return Offset.Zero
    }

    override fun onPostScroll(consumed: Offset, available: Offset, source: NestedScrollSource): Offset {
        if (available.y > 0f && source == NestedScrollSource.UserInput) {
            val before = state.offset
            state.offset = (state.offset + available.y).coerceAtMost(state.height)
            return Offset(0f, state.offset - before)
        }
        return Offset.Zero
    }

    override suspend fun onPreFling(available: Velocity): Velocity {
        if (state.offset > 0f) {
            settle(available.y)
            return available
        }
        return Velocity.Zero
    }
}
