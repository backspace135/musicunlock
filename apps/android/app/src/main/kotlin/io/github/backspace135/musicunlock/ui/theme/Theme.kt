package io.github.backspace135.musicunlock.ui.theme

import androidx.compose.foundation.text.selection.LocalTextSelectionColors
import androidx.compose.foundation.text.selection.TextSelectionColors
import androidx.compose.material3.LocalContentColor
import androidx.compose.material3.LocalTextStyle
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.darkColorScheme
import androidx.compose.material3.lightColorScheme
import androidx.compose.runtime.Composable
import androidx.compose.runtime.CompositionLocalProvider
import androidx.compose.runtime.ReadOnlyComposable
import androidx.compose.runtime.remember
import androidx.compose.runtime.staticCompositionLocalOf

private val LocalMuColors = staticCompositionLocalOf { LightColors }
private val LocalMuType = staticCompositionLocalOf { MuType }

/**
 * App theme. Read tokens with `MuTheme.colors.text`, `MuTheme.type.body`; spacing and shapes are plain
 * objects ([MuSpacing], [MuShapes]). Material 3 is themed with the same greys so its text fields, menus and
 * selection handles never show the default purple.
 */
@Composable
fun MuTheme(dark: Boolean, content: @Composable () -> Unit) {
    val colors = if (dark) DarkColors else LightColors
    val scheme = remember(colors) {
        val base = if (dark) darkColorScheme() else lightColorScheme()
        base.copy(
            primary = colors.text,
            onPrimary = colors.onText,
            primaryContainer = colors.fill,
            onPrimaryContainer = colors.text,
            secondary = colors.secondary,
            onSecondary = colors.onText,
            secondaryContainer = colors.fill,
            onSecondaryContainer = colors.text,
            tertiary = colors.secondary,
            background = colors.background,
            onBackground = colors.text,
            surface = colors.surface,
            onSurface = colors.text,
            surfaceVariant = colors.fill,
            onSurfaceVariant = colors.secondary,
            surfaceContainer = colors.elevated,
            surfaceContainerHigh = colors.elevated,
            surfaceContainerLow = colors.surface,
            outline = colors.separator,
            outlineVariant = colors.separator,
            error = colors.text,
            onError = colors.onText,
        )
    }
    val selection = remember(colors) { TextSelectionColors(handleColor = colors.text, backgroundColor = colors.text.copy(alpha = 0.2f)) }
    MaterialTheme(colorScheme = scheme) {
        CompositionLocalProvider(
            LocalMuColors provides colors,
            LocalMuType provides MuType,
            LocalContentColor provides colors.text,
            LocalTextStyle provides MuType.body,
            LocalTextSelectionColors provides selection,
            content = content,
        )
    }
}

object MuTheme {
    val colors: MuColors
        @Composable @ReadOnlyComposable get() = LocalMuColors.current
    val type: MuTypography
        @Composable @ReadOnlyComposable get() = LocalMuType.current
}
