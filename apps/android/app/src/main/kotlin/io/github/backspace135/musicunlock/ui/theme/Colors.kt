package io.github.backspace135.musicunlock.ui.theme

import androidx.compose.runtime.Immutable
import androidx.compose.ui.graphics.Color

/**
 * Colour tokens from apps/macos/DESIGN.md: black, white and grey only (R = G = B), no accent colour.
 * States are told apart by icons, text, shape or position, never by colour alone.
 */
@Immutable
data class MuColors(
    /** Workspace background behind every page. */
    val background: Color,
    /** Cards, row groups, the bottom navigation and footers. */
    val surface: Color,
    /** Bottom sheets, dialogs and menus, which float above surfaces. */
    val elevated: Color,
    /** Main text, primary actions and state glyphs. */
    val text: Color,
    /** Secondary text, captions and inactive icons. */
    val secondary: Color,
    val separator: Color,
    /** Quiet fills: segmented-control track, switch track when off, chips, the search field. */
    val fill: Color,
    /** Row highlight while pressed, and the running / selected row tint. */
    val highlight: Color,
    /** Raised thumbs on [fill] tracks: the selected segment, a switch thumb when off. */
    val thumb: Color,
    /** Text and icons on top of [text]-coloured fills, such as the primary button. */
    val onText: Color,
    /** Dimming behind sheets and dialogs, at full strength. */
    val scrim: Color,
    val isDark: Boolean,
)

val LightColors = MuColors(
    background = Color(0xFFF2F2F2),
    surface = Color(0xFFFAFAFA),
    elevated = Color(0xFFFFFFFF),
    text = Color(0xFF1C1C1C),
    secondary = Color(0xFF636363),
    separator = Color(0xFFE0E0E0),
    fill = Color(0x0E1C1C1C),
    highlight = Color(0x0F1C1C1C),
    thumb = Color(0xFFFFFFFF),
    onText = Color(0xFFFAFAFA),
    scrim = Color(0x66000000),
    isDark = false,
)

val DarkColors = MuColors(
    background = Color(0xFF1E1E1E),
    surface = Color(0xFF282828),
    elevated = Color(0xFF303030),
    text = Color(0xFFF2F2F2),
    secondary = Color(0xFFA0A0A0),
    separator = Color(0xFF3A3A3A),
    fill = Color(0x14F2F2F2),
    highlight = Color(0x10F2F2F2),
    thumb = Color(0xFF5A5A5A),
    onText = Color(0xFF1C1C1C),
    scrim = Color(0x99000000),
    isDark = true,
)
