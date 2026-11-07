package io.github.backspace135.musicunlock.ui.theme

import androidx.compose.runtime.Immutable
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.em
import androidx.compose.ui.unit.sp

/**
 * Type scale. The system font (Roboto / Noto Sans CJK) like the system font on macOS and Windows; sizes are
 * the desktop ones scaled up for phone reading distance. Colours are not part of the styles: text takes
 * [MuColors.text] by default, pass `color = MuTheme.colors.secondary` for secondary text.
 */
@Immutable
data class MuTypography(
    /** Large collapsing page title in the top bar. */
    val largeTitle: TextStyle,
    /** Small top-bar title, dialog and sheet titles. */
    val title: TextStyle,
    /** Section headings inside a page, empty-state titles. */
    val headline: TextStyle,
    /** Row titles and body text. */
    val body: TextStyle,
    /** Button labels, selected segments. */
    val label: TextStyle,
    /** Row subtitles and secondary lines. */
    val subtitle: TextStyle,
    /** Captions, hints, footnotes under groups. */
    val caption: TextStyle,
    /** Group headers above row groups (small, spaced). */
    val sectionHeader: TextStyle,
    /** Lyrics and other text where columns must line up. */
    val mono: TextStyle,
)

private val Tabular = "tnum"

val MuType = MuTypography(
    largeTitle = TextStyle(fontSize = 30.sp, lineHeight = 36.sp, fontWeight = FontWeight.SemiBold, letterSpacing = (-0.01).em),
    title = TextStyle(fontSize = 19.sp, lineHeight = 24.sp, fontWeight = FontWeight.SemiBold),
    headline = TextStyle(fontSize = 17.sp, lineHeight = 22.sp, fontWeight = FontWeight.SemiBold),
    body = TextStyle(fontSize = 16.sp, lineHeight = 22.sp, fontFeatureSettings = Tabular),
    label = TextStyle(fontSize = 15.sp, lineHeight = 20.sp, fontWeight = FontWeight.Medium, fontFeatureSettings = Tabular),
    subtitle = TextStyle(fontSize = 14.sp, lineHeight = 19.sp, fontFeatureSettings = Tabular),
    caption = TextStyle(fontSize = 13.sp, lineHeight = 18.sp, fontFeatureSettings = Tabular),
    sectionHeader = TextStyle(fontSize = 13.sp, lineHeight = 18.sp, fontWeight = FontWeight.Medium, letterSpacing = 0.02.em),
    mono = TextStyle(fontSize = 14.sp, lineHeight = 21.sp, fontFamily = FontFamily.Monospace),
)
