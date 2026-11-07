package io.github.backspace135.musicunlock.ui.kit

import androidx.compose.animation.AnimatedContent
import androidx.compose.animation.AnimatedVisibility
import androidx.compose.animation.expandVertically
import androidx.compose.animation.fadeIn
import androidx.compose.animation.fadeOut
import androidx.compose.animation.scaleOut
import androidx.compose.animation.shrinkVertically
import androidx.compose.animation.slideInVertically
import androidx.compose.animation.togetherWith
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.material3.Icon
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.LiveRegionMode
import androidx.compose.ui.semantics.liveRegion
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.unit.dp
import io.github.backspace135.musicunlock.R
import io.github.backspace135.musicunlock.ui.theme.MuMotion
import io.github.backspace135.musicunlock.ui.theme.MuShapes
import io.github.backspace135.musicunlock.ui.theme.MuSpacing
import io.github.backspace135.musicunlock.ui.theme.MuTheme

/**
 * Notice above content ("已添加 2 个文件…", errors that need no decision). Hidden while [text] is null or
 * empty; it drops in from 8 dp above and fades in, and shrinks and fades out when [text] becomes empty or
 * the close button calls [onDismiss] (pass null for a banner that cannot be closed). A new text while it is
 * shown cross-fades.
 */
@Composable
fun NoticeBanner(
    text: String?,
    onDismiss: (() -> Unit)?,
    modifier: Modifier = Modifier,
    icon: ImageVector = MuIcons.Info,
) {
    // Keep the last text while the banner animates out.
    var shown by remember { mutableStateOf(text.orEmpty()) }
    if (!text.isNullOrEmpty()) shown = text
    val drop = with(LocalDensity.current) { 8.dp.roundToPx() }
    AnimatedVisibility(
        visible = !text.isNullOrEmpty(),
        modifier = modifier,
        enter = fadeIn(MuMotion.fadeIn()) + slideInVertically(MuMotion.fadeIn()) { -drop } + expandVertically(MuMotion.list()),
        exit = fadeOut(MuMotion.fadeOut()) + scaleOut(MuMotion.fadeOut(), targetScale = 0.98f) + shrinkVertically(MuMotion.list()),
    ) {
        val colors = MuTheme.colors
        Row(
            Modifier
                .fillMaxWidth()
                .padding(horizontal = MuSpacing.page, vertical = MuSpacing.xs)
                .clip(MuShapes.medium)
                .background(colors.surface)
                .border(0.5.dp, colors.separator, MuShapes.medium)
                .semantics { liveRegion = LiveRegionMode.Polite }
                .padding(start = MuSpacing.l, top = MuSpacing.xs, bottom = MuSpacing.xs),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            Icon(icon, null, Modifier.size(20.dp), tint = colors.text)
            AnimatedContent(
                shown,
                Modifier.weight(1f).padding(horizontal = MuSpacing.m, vertical = MuSpacing.s),
                transitionSpec = { fadeIn(MuMotion.fadeIn()) togetherWith fadeOut(MuMotion.fadeOut()) },
                label = "bannerText",
            ) { Text(it, style = MuTheme.type.subtitle) }
            if (onDismiss != null) {
                IconAction(MuIcons.Close, stringResource(R.string.common_close), onDismiss)
            } else {
                Row(Modifier.padding(end = MuSpacing.l)) {}
            }
        }
    }
}
