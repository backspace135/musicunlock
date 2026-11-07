package io.github.backspace135.musicunlock.ui.nav

import androidx.activity.compose.PredictiveBackHandler
import androidx.compose.foundation.background
import androidx.compose.foundation.gestures.awaitEachGesture
import androidx.compose.foundation.gestures.awaitFirstDown
import androidx.compose.foundation.gestures.awaitHorizontalTouchSlopOrCancellation
import androidx.compose.foundation.gestures.horizontalDrag
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.runtime.Composable
import androidx.compose.runtime.CompositionLocalProvider
import androidx.compose.runtime.SideEffect
import androidx.compose.runtime.key
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.saveable.rememberSaveableStateHolder
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clipToBounds
import androidx.compose.ui.draw.drawWithContent
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.RectangleShape
import androidx.compose.ui.graphics.graphicsLayer
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.input.pointer.positionChange
import androidx.compose.ui.input.pointer.util.VelocityTracker
import androidx.compose.ui.input.pointer.util.addPointerInputChange
import androidx.compose.ui.layout.layout
import androidx.compose.ui.layout.onSizeChanged
import androidx.compose.ui.unit.dp
import androidx.lifecycle.viewmodel.compose.LocalViewModelStoreOwner
import io.github.backspace135.musicunlock.ui.theme.MuMotion
import io.github.backspace135.musicunlock.ui.theme.MuTheme
import kotlinx.coroutines.launch

/**
 * Shows the [navigator]'s pages, the top one over the one below.
 *
 * - Push / pop slide the top page in from / out to the right; the page below moves a third of the way
 *   (parallax) and dims.
 * - Swiping right anywhere on a page (when a child did not take the gesture) drags the page with the
 *   finger; on release it keeps the finger's velocity and either finishes going back or springs back.
 * - System back pops, and on Android 14+ the predictive back gesture drives the same transition.
 */
@Composable
fun PageStackHost(navigator: Navigator, modifier: Modifier = Modifier, content: @Composable (Route) -> Unit) {
    val holder = rememberSaveableStateHolder()
    val scope = rememberCoroutineScope()
    SideEffect { navigator.scope = scope }
    val background = MuTheme.colors.background

    PredictiveBackHandler(enabled = navigator.canGoBack) { events ->
        // A push or pop is still running: let it finish, then go back normally.
        if (!navigator.mutex.tryLock()) {
            events.collect {}
            navigator.pop()
            return@PredictiveBackHandler
        }
        var completed = false
        try {
            events.collect { event -> navigator.progress = 1f - event.progress }
            completed = true
        } finally {
            scope.launch {
                try {
                    if (completed) {
                        navigator.animateProgress(0f, 0f, MuMotion.page())
                        navigator.removeTop()
                    } else {
                        navigator.animateProgress(1f, 0f, MuMotion.pageSettle())
                    }
                } finally {
                    navigator.mutex.unlock()
                }
            }
        }
    }

    CompositionLocalProvider(LocalNavigator provides navigator) {
        Box(modifier.fillMaxSize().clipToBounds().onSizeChanged { navigator.width = it.width.toFloat().coerceAtLeast(1f) }) {
            val entries = navigator.entries
            val top = entries.lastIndex
            entries.forEachIndexed { index, entry ->
                key(entry.id) {
                    val position = when (index) {
                        top -> PagePosition.Top
                        top - 1 -> PagePosition.Below
                        else -> PagePosition.Hidden
                    }
                    CompositionLocalProvider(
                        LocalPageActive provides (position == PagePosition.Top),
                        LocalViewModelStoreOwner provides entry,
                    ) {
                        Box(
                            Modifier
                                .fillMaxSize()
                                .page(position, navigator, isRoot = index == 0)
                                .background(background)
                                .then(if (position == PagePosition.Top && index > 0) Modifier.swipeBack(navigator) else Modifier),
                        ) {
                            holder.SaveableStateProvider(entry.id) { content(entry.route) }
                        }
                    }
                }
            }
        }
    }
}

private enum class PagePosition { Top, Below, Hidden }

/** Positions a page for the current transition progress. Reads progress only in layout / draw. */
private fun Modifier.page(position: PagePosition, navigator: Navigator, isRoot: Boolean): Modifier = when (position) {
    PagePosition.Hidden -> layout { _, constraints -> layout(constraints.maxWidth, constraints.maxHeight) {} }
    PagePosition.Top -> graphicsLayer {
        val progress = navigator.progress
        translationX = if (isRoot) 0f else size.width * (1f - progress)
        shadowElevation = if (!isRoot && progress < 1f) 12.dp.toPx() else 0f
        shape = RectangleShape
        clip = false
    }
    PagePosition.Below -> graphicsLayer {
        val progress = navigator.progress
        translationX = -size.width * MuMotion.pageParallax * progress
        // Fully covered: skip drawing it at all.
        alpha = if (progress >= 1f) 0f else 1f
    }.drawWithContent {
        drawContent()
        drawRect(Color.Black, alpha = MuMotion.pageScrim * navigator.progress)
    }
}

/** Swipe right to go back. Children that consume the drag first (sliders, swipe rows) keep it. */
private fun Modifier.swipeBack(navigator: Navigator): Modifier = pointerInput(navigator) {
    awaitEachGesture {
        val down = awaitFirstDown(requireUnconsumed = false)
        val tracker = VelocityTracker()
        tracker.addPointerInputChange(down)
        var started = false
        val drag = awaitHorizontalTouchSlopOrCancellation(down.id) { change, over ->
            if (over > 0f && navigator.canGoBack && navigator.mutex.tryLock()) {
                started = true
                change.consume()
                navigator.progress = (1f - over / navigator.width).coerceIn(0f, 1f)
            }
        }
        if (drag == null || !started) {
            if (started) navigator.mutex.unlock()
            return@awaitEachGesture
        }
        tracker.addPointerInputChange(drag)
        horizontalDrag(drag.id) { change ->
            tracker.addPointerInputChange(change)
            navigator.progress = (navigator.progress - change.positionChange().x / navigator.width).coerceIn(0f, 1f)
            change.consume()
        }
        val velocity = tracker.calculateVelocity().x
        val goBack = velocity > 800f || (velocity > -300f && navigator.progress < 0.6f)
        val scope = navigator.scope
        if (scope == null) {
            navigator.mutex.unlock()
            return@awaitEachGesture
        }
        scope.launch {
            try {
                // Velocity in progress units: the page moves right as progress falls.
                val progressVelocity = -velocity / navigator.width
                if (goBack) {
                    navigator.animateProgress(0f, progressVelocity, MuMotion.pageSettle())
                    navigator.removeTop()
                } else {
                    navigator.animateProgress(1f, progressVelocity, MuMotion.pageSettle())
                }
            } finally {
                navigator.mutex.unlock()
            }
        }
    }
}
