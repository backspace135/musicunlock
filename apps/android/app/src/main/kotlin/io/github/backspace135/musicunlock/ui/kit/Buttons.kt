package io.github.backspace135.musicunlock.ui.kit

import androidx.compose.animation.AnimatedContent
import androidx.compose.animation.SizeTransform
import androidx.compose.animation.fadeIn
import androidx.compose.animation.fadeOut
import androidx.compose.animation.slideInVertically
import androidx.compose.animation.slideOutVertically
import androidx.compose.animation.togetherWith
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.defaultMinSize
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.material3.Icon
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.alpha
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import io.github.backspace135.musicunlock.ui.theme.MuMotion
import io.github.backspace135.musicunlock.ui.theme.MuShapes
import io.github.backspace135.musicunlock.ui.theme.MuSpacing
import io.github.backspace135.musicunlock.ui.theme.MuTheme

/**
 * The one main action of a screen or footer: ink capsule, light label. When [text] changes the label
 * slides and the capsule resizes with a spring, so "开始转换" → "停止转换" morphs in place.
 */
@Composable
fun PrimaryButton(
    text: String,
    onClick: () -> Unit,
    modifier: Modifier = Modifier,
    enabled: Boolean = true,
    icon: ImageVector? = null,
    small: Boolean = false,
) {
    ButtonBase(text, onClick, modifier, enabled, icon, small, MuTheme.colors.text, MuTheme.colors.onText)
}

/** Any other button next to or instead of a primary one: grey capsule, ink label. */
@Composable
fun SecondaryButton(
    text: String,
    onClick: () -> Unit,
    modifier: Modifier = Modifier,
    enabled: Boolean = true,
    icon: ImageVector? = null,
    small: Boolean = false,
) {
    ButtonBase(text, onClick, modifier, enabled, icon, small, MuTheme.colors.fill, MuTheme.colors.text)
}

/** Text-only action inside content, such as "查看全部" or "全选". */
@Composable
fun PlainButton(text: String, onClick: () -> Unit, modifier: Modifier = Modifier, enabled: Boolean = true) {
    ButtonBase(text, onClick, modifier, enabled, null, small = true, Color.Transparent, MuTheme.colors.text)
}

/** 48 dp round icon button for top bars and rows. [contentDescription] is required for TalkBack. */
@Composable
fun IconAction(
    icon: ImageVector,
    contentDescription: String,
    onClick: () -> Unit,
    modifier: Modifier = Modifier,
    enabled: Boolean = true,
) {
    Box(
        modifier
            .size(MuSpacing.touchTarget)
            .alpha(if (enabled) 1f else DisabledAlpha)
            .pressable(onClick, enabled = enabled, onClickLabel = contentDescription, pressedScale = 0.88f),
        contentAlignment = Alignment.Center,
    ) {
        Icon(icon, contentDescription, Modifier.size(24.dp), tint = MuTheme.colors.text)
    }
}

internal const val DisabledAlpha = 0.38f

@Composable
private fun ButtonBase(
    text: String,
    onClick: () -> Unit,
    modifier: Modifier,
    enabled: Boolean,
    icon: ImageVector?,
    small: Boolean,
    container: Color,
    content: Color,
) {
    val height = if (small) 40.dp else 48.dp
    val padding = if (small) PaddingValues(horizontal = 14.dp) else PaddingValues(horizontal = 20.dp)
    Row(
        modifier
            .alpha(if (enabled) 1f else DisabledAlpha)
            .pressable(onClick, enabled = enabled)
            .defaultMinSize(minHeight = height)
            .clip(MuShapes.capsule)
            .background(container)
            .padding(padding),
        horizontalArrangement = Arrangement.Center,
        verticalAlignment = Alignment.CenterVertically,
    ) {
        if (icon != null) {
            Icon(icon, null, Modifier.size(20.dp), tint = content)
            Box(Modifier.size(MuSpacing.s))
        }
        AnimatedContent(
            targetState = text,
            transitionSpec = {
                (fadeIn(MuMotion.fadeIn()) + slideInVertically(MuMotion.toggle()) { it / 2 }) togetherWith
                    (fadeOut(MuMotion.fadeOut()) + slideOutVertically(MuMotion.toggle()) { -it / 2 }) using
                    SizeTransform(clip = false) { _, _ -> MuMotion.toggle() }
            },
            contentAlignment = Alignment.Center,
            label = "buttonLabel",
        ) { label ->
            Text(label, color = content, style = MuTheme.type.label, maxLines = 1, overflow = TextOverflow.Ellipsis)
        }
    }
}
