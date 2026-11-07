package io.github.backspace135.musicunlock.ui.theme

import androidx.compose.animation.core.CubicBezierEasing
import androidx.compose.animation.core.FiniteAnimationSpec
import androidx.compose.animation.core.SpringSpec
import androidx.compose.animation.core.spring
import androidx.compose.animation.core.tween
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableFloatStateOf
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.setValue

/**
 * Motion tokens shared by the kit components. Values are state so the debug playground can tune them
 * live; release builds never change them. The final values and their macOS / Windows counterparts are in
 * apps/android/DESIGN.md ("动效参数").
 *
 * Springs are described by damping ratio (1 = no overshoot, lower = more bounce) and stiffness (higher =
 * faster). The system "remove animations" setting scales every Compose animation to zero, so nothing here
 * needs its own reduced-motion switch.
 */
object MuMotion {
    /** Windows `smooth` easing (cubic-bezier 0.2, 0, 0, 1), used for timed transitions. */
    val Smooth = CubicBezierEasing(0.2f, 0f, 0f, 1f)

    // Press feedback: buttons and cards shrink while held, spring back with a small overshoot.
    var pressScale by mutableFloatStateOf(0.96f)
    var pressDamping by mutableFloatStateOf(0.45f)
    var pressStiffness by mutableFloatStateOf(600f)
    /** A quick tap still shows the press for at least this long. */
    var pressMinMillis by mutableIntStateOf(90)

    // Switches, checkboxes, segmented-control thumb, selection checkboxes sliding in.
    var toggleDamping by mutableFloatStateOf(0.62f)
    var toggleStiffness by mutableFloatStateOf(700f)

    // Page stack.
    var pageDurationMillis by mutableIntStateOf(320)
    /** How far the page below moves left while the top page covers it, as a fraction of the width. */
    var pageParallax by mutableFloatStateOf(0.3f)
    /** Dimming of the page below at full cover. */
    var pageScrim by mutableFloatStateOf(0.12f)
    /** Spring that settles the page after a swipe, carrying the finger's velocity. */
    var pageSettleStiffness by mutableFloatStateOf(500f)

    // Bottom sheet.
    var sheetDamping by mutableFloatStateOf(0.86f)
    var sheetStiffness by mutableFloatStateOf(450f)
    /** Seconds of fling velocity added to the release position before choosing open or closed. */
    var sheetProjection by mutableFloatStateOf(0.15f)

    // Rows moving when items are added, removed or reordered.
    var listDamping by mutableFloatStateOf(0.85f)
    var listStiffness by mutableFloatStateOf(500f)

    /** Circular reveal when the theme changes. */
    var themeRevealMillis by mutableIntStateOf(480)

    fun <T> press(): SpringSpec<T> = spring(pressDamping, pressStiffness)
    fun <T> pressDown(): SpringSpec<T> = spring(1f, pressStiffness * 2.5f)
    fun <T> toggle(): SpringSpec<T> = spring(toggleDamping, toggleStiffness)
    fun <T> sheet(): SpringSpec<T> = spring(sheetDamping, sheetStiffness)
    fun <T> list(): SpringSpec<T> = spring(listDamping, listStiffness)
    fun <T> pageSettle(): SpringSpec<T> = spring(1f, pageSettleStiffness)
    fun <T> page(): FiniteAnimationSpec<T> = tween(pageDurationMillis, easing = Smooth)

    /** Fades and other timed changes: in 300 ms, out 200 ms, as on Windows. */
    fun <T> fadeIn(): FiniteAnimationSpec<T> = tween(300, easing = Smooth)
    fun <T> fadeOut(): FiniteAnimationSpec<T> = tween(200, easing = Smooth)
}
