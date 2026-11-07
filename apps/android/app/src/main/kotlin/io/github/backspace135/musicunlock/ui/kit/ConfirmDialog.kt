package io.github.backspace135.musicunlock.ui.kit

import androidx.activity.compose.BackHandler
import androidx.compose.animation.AnimatedVisibility
import androidx.compose.animation.core.spring
import androidx.compose.animation.core.updateTransition
import androidx.compose.animation.fadeIn
import androidx.compose.animation.fadeOut
import androidx.compose.animation.scaleIn
import androidx.compose.animation.scaleOut
import androidx.compose.foundation.background
import androidx.compose.foundation.gestures.detectTapGestures
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.layout.widthIn
import androidx.compose.material3.Icon
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.paneTitle
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.unit.dp
import io.github.backspace135.musicunlock.R
import io.github.backspace135.musicunlock.ui.theme.MuMotion
import io.github.backspace135.musicunlock.ui.theme.MuShapes
import io.github.backspace135.musicunlock.ui.theme.MuSpacing
import io.github.backspace135.musicunlock.ui.theme.MuTheme

/**
 * Asks before an action that cannot be undone or replaces work ("清空队列", "重新获取歌词…"). The card
 * springs in from 90 % over a fading scrim. [onConfirm] and [onDismissRequest] must both set [visible]
 * to false; the scrim, system back and the dismiss button call [onDismissRequest]. Pass [icon] (for
 * example [MuIcons.Warning]) for destructive actions so the danger is not shown by colour.
 */
@Composable
fun ConfirmDialog(
    visible: Boolean,
    title: String,
    confirmLabel: String,
    onConfirm: () -> Unit,
    onDismissRequest: () -> Unit,
    modifier: Modifier = Modifier,
    message: String? = null,
    dismissLabel: String = stringResource(R.string.common_cancel),
    icon: ImageVector? = null,
) {
    val transition = updateTransition(visible, label = "confirmDialog")
    if (!transition.currentState && !transition.targetState) return
    Overlay {
        BackHandler(enabled = visible, onBack = onDismissRequest)
        val colors = MuTheme.colors
        Box(Modifier.fillMaxSize(), contentAlignment = Alignment.Center) {
            transition.AnimatedVisibility({ it }, enter = fadeIn(MuMotion.fadeIn()), exit = fadeOut(MuMotion.fadeOut())) {
                Box(
                    Modifier
                        .fillMaxSize()
                        .background(colors.scrim)
                        .pointerInput(Unit) { detectTapGestures { onDismissRequest() } },
                )
            }
            transition.AnimatedVisibility(
                { it },
                enter = fadeIn(MuMotion.fadeIn()) + scaleIn(spring(0.7f, 600f), initialScale = 0.9f),
                exit = fadeOut(MuMotion.fadeOut()) + scaleOut(MuMotion.fadeOut(), targetScale = 0.95f),
            ) {
                Column(
                    modifier
                        .padding(horizontal = MuSpacing.xl)
                        .widthIn(max = 340.dp)
                        .fillMaxWidth()
                        .clip(MuShapes.large)
                        .background(colors.elevated)
                        .semantics { paneTitle = title }
                        // Taps on the card must not reach the scrim.
                        .pointerInput(Unit) { detectTapGestures { } }
                        .padding(MuSpacing.xl),
                ) {
                    if (icon != null) {
                        Icon(icon, null, Modifier.size(28.dp), tint = colors.text)
                        Spacer(Modifier.height(MuSpacing.m))
                    }
                    Text(title, style = MuTheme.type.title)
                    if (message != null) {
                        Text(message, style = MuTheme.type.subtitle, color = colors.secondary, modifier = Modifier.padding(top = MuSpacing.s))
                    }
                    Spacer(Modifier.height(MuSpacing.xl))
                    Row(Modifier.fillMaxWidth()) {
                        SecondaryButton(dismissLabel, onDismissRequest, Modifier.weight(1f))
                        Spacer(Modifier.width(MuSpacing.s))
                        PrimaryButton(confirmLabel, onConfirm, Modifier.weight(1f))
                    }
                }
            }
        }
    }
}
