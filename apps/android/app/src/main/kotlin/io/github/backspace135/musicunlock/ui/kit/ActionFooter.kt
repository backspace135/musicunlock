package io.github.backspace135.musicunlock.ui.kit

import androidx.compose.animation.AnimatedContent
import androidx.compose.animation.AnimatedVisibility
import androidx.compose.animation.SizeTransform
import androidx.compose.animation.expandHorizontally
import androidx.compose.animation.fadeIn
import androidx.compose.animation.fadeOut
import androidx.compose.animation.scaleIn
import androidx.compose.animation.scaleOut
import androidx.compose.animation.shrinkHorizontally
import androidx.compose.animation.slideInVertically
import androidx.compose.animation.slideOutVertically
import androidx.compose.animation.togetherWith
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.draw.shadow
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import io.github.backspace135.musicunlock.ui.theme.MuMotion
import io.github.backspace135.musicunlock.ui.theme.MuShapes
import io.github.backspace135.musicunlock.ui.theme.MuSpacing
import io.github.backspace135.musicunlock.ui.theme.MuTheme

/**
 * Floating footer at the bottom of a workspace: status on the left, buttons on the right.
 *
 * - Left: [title] and below it either [detail] or, when [progress] is not null, a progress bar (0..1).
 *   Changes roll in from below. Pass [leading] to put something else there (for example a format
 *   [SegmentedControl]); it replaces the texts.
 * - Right: the primary button. Changing [actionLabel] ("开始转换" → "停止转换") morphs it in place.
 *   [secondaryLabel] adds a secondary button that splits off from the primary one when it appears.
 * - [visible] slides the whole footer in from the bottom / out again.
 *
 * Place it at the bottom of the page's Box and give the list bottom content padding of about 96 dp so the
 * last row can scroll above it.
 */
@Composable
fun ActionFooter(
    title: String,
    actionLabel: String,
    onAction: () -> Unit,
    modifier: Modifier = Modifier,
    detail: String? = null,
    progress: Float? = null,
    actionEnabled: Boolean = true,
    secondaryLabel: String? = null,
    onSecondary: () -> Unit = {},
    visible: Boolean = true,
    leading: (@Composable () -> Unit)? = null,
) {
    AnimatedVisibility(
        visible,
        modifier = modifier,
        enter = slideInVertically(MuMotion.sheet()) { it } + fadeIn(MuMotion.fadeIn()),
        exit = slideOutVertically(MuMotion.sheet()) { it } + fadeOut(MuMotion.fadeOut()),
    ) {
        val colors = MuTheme.colors
        // Keep the last secondary label while that button folds back into the primary one.
        var lastSecondary by remember { mutableStateOf(secondaryLabel.orEmpty()) }
        if (secondaryLabel != null) lastSecondary = secondaryLabel
        Row(
            Modifier
                .fillMaxWidth()
                .padding(horizontal = MuSpacing.m, vertical = MuSpacing.s)
                .shadow(12.dp, MuShapes.large, ambientColor = colors.scrim, spotColor = colors.scrim)
                .clip(MuShapes.large)
                .background(colors.surface)
                .border(0.5.dp, colors.separator, MuShapes.large)
                .heightIn(min = 64.dp)
                .padding(start = MuSpacing.l, end = MuSpacing.s, top = MuSpacing.s, bottom = MuSpacing.s),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            Box(Modifier.weight(1f).padding(end = MuSpacing.s)) {
                if (leading != null) {
                    leading()
                } else {
                    FooterStatus(title, detail, progress)
                }
            }
            AnimatedVisibility(
                secondaryLabel != null,
                enter = expandHorizontally(MuMotion.toggle(), expandFrom = Alignment.End) + fadeIn() + scaleIn(MuMotion.toggle(), initialScale = 0.8f),
                exit = shrinkHorizontally(MuMotion.toggle(), shrinkTowards = Alignment.End) + fadeOut() + scaleOut(targetScale = 0.8f),
            ) {
                SecondaryButton(lastSecondary, onSecondary, Modifier.padding(end = MuSpacing.s), small = true)
            }
            PrimaryButton(actionLabel, onAction, enabled = actionEnabled, small = true)
        }
    }
}

@Composable
private fun FooterStatus(title: String, detail: String?, progress: Float?) {
    Column {
        AnimatedContent(
            title,
            transitionSpec = {
                (slideInVertically(MuMotion.toggle()) { it / 2 } + fadeIn()) togetherWith
                    (slideOutVertically(MuMotion.toggle()) { -it / 2 } + fadeOut()) using SizeTransform(clip = false)
            },
            label = "footerTitle",
        ) { Text(it, style = MuTheme.type.label, maxLines = 1, overflow = TextOverflow.Ellipsis) }
        AnimatedContent(
            progress != null,
            transitionSpec = { fadeIn(MuMotion.fadeIn()) togetherWith fadeOut(MuMotion.fadeOut()) using SizeTransform(clip = false) },
            label = "footerDetail",
        ) { showProgress ->
            if (showProgress) {
                MuProgressBar(progress ?: 0f, Modifier.padding(top = MuSpacing.s, bottom = MuSpacing.xs))
            } else if (detail != null) {
                Text(detail, style = MuTheme.type.caption, color = MuTheme.colors.secondary, maxLines = 1, overflow = TextOverflow.Ellipsis)
            }
        }
    }
}
