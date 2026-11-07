package io.github.backspace135.musicunlock.ui.theme

import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.ui.unit.dp

/** 4 / 8 dp spacing grid, as on macOS and Windows. Use these instead of literal dp values. */
object MuSpacing {
    val xxs = 2.dp
    val xs = 4.dp
    val s = 8.dp
    val m = 12.dp
    val l = 16.dp
    val xl = 24.dp
    val xxl = 32.dp

    /** Left and right page margin (24 on the desktop apps; phones are narrower). */
    val page = 16.dp
    /** Minimum height of a tappable row. */
    val rowHeight = 56.dp
    /** Minimum touch target for icon buttons. */
    val touchTarget = 48.dp
}

/** Corner radii. Panels follow the 20–22 pt glass surfaces on macOS. */
object MuShapes {
    /** Segmented-control thumb, search field, chips, cover art. */
    val small = RoundedCornerShape(10.dp)
    /** Row groups and banners. */
    val medium = RoundedCornerShape(16.dp)
    /** Floating footer, dialogs. */
    val large = RoundedCornerShape(22.dp)
    /** Top corners of bottom sheets. */
    val sheet = RoundedCornerShape(topStart = 24.dp, topEnd = 24.dp)
    /** Buttons and pills. */
    val capsule = RoundedCornerShape(percent = 50)
}
