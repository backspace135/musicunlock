package io.github.backspace135.musicunlock.ui.kit

import androidx.compose.animation.AnimatedVisibility
import androidx.compose.animation.core.animateFloatAsState
import androidx.compose.animation.core.tween
import androidx.compose.animation.expandHorizontally
import androidx.compose.animation.fadeIn
import androidx.compose.animation.fadeOut
import androidx.compose.animation.scaleIn
import androidx.compose.animation.scaleOut
import androidx.compose.animation.shrinkHorizontally
import androidx.compose.foundation.background
import androidx.compose.foundation.combinedClickable
import androidx.compose.foundation.interaction.MutableInteractionSource
import androidx.compose.foundation.interaction.collectIsPressedAsState
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ColumnScope
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.widthIn
import androidx.compose.foundation.selection.toggleable
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Icon
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.remember
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.alpha
import androidx.compose.ui.draw.clip
import androidx.compose.ui.draw.drawBehind
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Shape
import androidx.compose.ui.graphics.graphicsLayer
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.selected
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import io.github.backspace135.musicunlock.ui.theme.MuMotion
import io.github.backspace135.musicunlock.ui.theme.MuShapes
import io.github.backspace135.musicunlock.ui.theme.MuSpacing
import io.github.backspace135.musicunlock.ui.theme.MuTheme

/**
 * Rounded card holding a fixed set of rows (settings, sheet content). Put [RowDivider] between rows.
 * [header] is a small title above the card, [footer] a caption below it.
 */
@Composable
fun ListGroup(
    modifier: Modifier = Modifier,
    header: String? = null,
    footer: String? = null,
    content: @Composable ColumnScope.() -> Unit,
) {
    Column(modifier.fillMaxWidth().padding(horizontal = MuSpacing.page)) {
        if (header != null) SectionHeader(header)
        Column(Modifier.fillMaxWidth().clip(MuShapes.medium).background(MuTheme.colors.surface), content = content)
        if (footer != null) {
            Text(
                footer,
                style = MuTheme.type.caption,
                color = MuTheme.colors.secondary,
                modifier = Modifier.padding(start = MuSpacing.l, end = MuSpacing.l, top = MuSpacing.s),
            )
        }
    }
}

/** Small title above a [ListGroup] or a block of content. */
@Composable
fun SectionHeader(text: String, modifier: Modifier = Modifier) {
    Text(
        text,
        style = MuTheme.type.sectionHeader,
        color = MuTheme.colors.secondary,
        modifier = modifier.padding(start = MuSpacing.l, end = MuSpacing.l, top = MuSpacing.xl, bottom = MuSpacing.s),
    )
}

/** Hairline between rows. Use `inset = 60.dp` when the rows have a leading icon, so the line starts at the text. */
@Composable
fun RowDivider(modifier: Modifier = Modifier, inset: Dp = MuSpacing.l) {
    Box(
        modifier
            .fillMaxWidth()
            .padding(start = inset)
            .height(0.5.dp)
            .background(MuTheme.colors.separator),
    )
}

/**
 * One item of a `LazyColumn` drawn as part of a group card: the first item gets the top corners, the
 * last the bottom ones, and every item but the first a divider. Use it for long, changing lists (queue,
 * search results) where [ListGroup] cannot be used. Pass `Modifier.animateItem(...)` as [modifier].
 */
@Composable
fun LazyGroupItem(index: Int, count: Int, modifier: Modifier = Modifier, dividerInset: Dp = MuSpacing.l, content: @Composable () -> Unit) {
    val radius = 16.dp
    val shape: Shape = RoundedCornerShape(
        topStart = if (index == 0) radius else 0.dp,
        topEnd = if (index == 0) radius else 0.dp,
        bottomStart = if (index == count - 1) radius else 0.dp,
        bottomEnd = if (index == count - 1) radius else 0.dp,
    )
    Box(modifier.fillMaxWidth().padding(horizontal = MuSpacing.page)) {
        Box(Modifier.fillMaxWidth().clip(shape).background(MuTheme.colors.surface)) {
            content()
            if (index > 0) RowDivider(Modifier.align(Alignment.TopStart), inset = dividerInset)
        }
    }
}

/**
 * The standard list row: optional leading slot (icon, cover, glyph), title, subtitle, optional third
 * line ([extra]), optional trailing slot. It highlights while pressed instead of scaling.
 *
 * - [selectionMode]: a round checkbox slides in at the start; [selected] is its state. Toggle it in
 *   [onClick].
 * - [highlighted]: grey tint for the running job or the selected item.
 * - [current]: tint plus a 3 dp ink bar at the start, for "the item shown in the editor".
 */
@Composable
fun ListRow(
    title: String,
    modifier: Modifier = Modifier,
    subtitle: String? = null,
    leading: (@Composable () -> Unit)? = null,
    trailing: (@Composable () -> Unit)? = null,
    extra: (@Composable () -> Unit)? = null,
    onClick: (() -> Unit)? = null,
    onLongClick: (() -> Unit)? = null,
    selectionMode: Boolean = false,
    selected: Boolean = false,
    highlighted: Boolean = false,
    current: Boolean = false,
    enabled: Boolean = true,
    titleMaxLines: Int = 1,
) {
    val colors = MuTheme.colors
    val source = remember { MutableInteractionSource() }
    val pressed by source.collectIsPressedAsState()
    val tint by animateFloatAsState(
        when {
            pressed -> 1f
            highlighted || current -> 0.8f
            else -> 0f
        },
        tween(if (pressed) 60 else 250),
        label = "rowTint",
    )
    val click = if (onClick != null || onLongClick != null) {
        Modifier.combinedClickable(
            interactionSource = source,
            indication = null,
            enabled = enabled,
            onLongClick = onLongClick,
            onClick = onClick ?: {},
        )
    } else {
        Modifier
    }
    Row(
        modifier
            .fillMaxWidth()
            .semantics { if (selectionMode) this.selected = selected }
            .then(click)
            .drawBehind {
                if (tint > 0f) drawRect(colors.highlight, alpha = tint.coerceAtMost(1f))
                if (current) drawRect(colors.text, size = Size(3.dp.toPx(), size.height))
            }
            .alpha(if (enabled) 1f else DisabledAlpha)
            .heightIn(min = MuSpacing.rowHeight)
            .padding(horizontal = MuSpacing.l, vertical = 10.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        AnimatedVisibility(
            selectionMode,
            enter = expandHorizontally(MuMotion.toggle()) + fadeIn() + scaleIn(MuMotion.toggle(), initialScale = 0.5f),
            exit = shrinkHorizontally(MuMotion.toggle()) + fadeOut() + scaleOut(targetScale = 0.5f),
        ) {
            MuCheckbox(selected, null, Modifier.padding(end = MuSpacing.m))
        }
        if (leading != null) {
            Box(Modifier.padding(end = MuSpacing.m), contentAlignment = Alignment.Center) { leading() }
        }
        Column(Modifier.weight(1f)) {
            Text(title, style = MuTheme.type.body, maxLines = titleMaxLines, overflow = TextOverflow.Ellipsis)
            if (subtitle != null) {
                Text(subtitle, style = MuTheme.type.subtitle, color = colors.secondary, maxLines = 1, overflow = TextOverflow.Ellipsis)
            }
            if (extra != null) extra()
        }
        if (trailing != null) {
            Box(Modifier.padding(start = MuSpacing.m), contentAlignment = Alignment.CenterEnd) { trailing() }
        }
    }
}

/** 24 dp icon for [ListRow]'s leading slot, in secondary grey. */
@Composable
fun RowIcon(icon: ImageVector, modifier: Modifier = Modifier) {
    Icon(icon, null, modifier.size(24.dp), tint = MuTheme.colors.secondary)
}

/** Row that opens another page: title, optional current value, chevron. */
@Composable
fun NavigationRow(
    title: String,
    onClick: () -> Unit,
    modifier: Modifier = Modifier,
    value: String? = null,
    subtitle: String? = null,
    icon: ImageVector? = null,
    enabled: Boolean = true,
) {
    ListRow(
        title = title,
        modifier = modifier,
        subtitle = subtitle,
        leading = icon?.let { { RowIcon(it) } },
        trailing = {
            Row(verticalAlignment = Alignment.CenterVertically) {
                if (value != null) {
                    Text(
                        value,
                        style = MuTheme.type.subtitle,
                        color = MuTheme.colors.secondary,
                        maxLines = 1,
                        overflow = TextOverflow.Ellipsis,
                        modifier = Modifier.widthIn(max = 160.dp).padding(end = MuSpacing.xs),
                    )
                }
                Icon(MuIcons.Chevron, null, Modifier.size(20.dp), tint = MuTheme.colors.secondary)
            }
        },
        onClick = onClick,
        enabled = enabled,
    )
}

/** On / off setting. The whole row toggles; the switch thumb stretches while the row is pressed. */
@Composable
fun SwitchRow(
    title: String,
    checked: Boolean,
    onCheckedChange: (Boolean) -> Unit,
    modifier: Modifier = Modifier,
    subtitle: String? = null,
    enabled: Boolean = true,
) {
    val source = remember { MutableInteractionSource() }
    Row(
        modifier
            .fillMaxWidth()
            .toggleable(checked, source, indication = null, enabled = enabled, role = Role.Switch, onValueChange = onCheckedChange)
            .heightIn(min = MuSpacing.rowHeight)
            .padding(horizontal = MuSpacing.l, vertical = 10.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        RowTexts(title, subtitle, enabled, Modifier.weight(1f))
        MuSwitch(checked, null, Modifier.padding(start = MuSpacing.m), enabled = enabled, interactionSource = source)
    }
}

/** One of several independent options (multi-select in settings): round checkbox, then the text. */
@Composable
fun CheckRow(
    title: String,
    checked: Boolean,
    onCheckedChange: (Boolean) -> Unit,
    modifier: Modifier = Modifier,
    subtitle: String? = null,
    enabled: Boolean = true,
) {
    Row(
        modifier
            .fillMaxWidth()
            .toggleable(checked, remember { MutableInteractionSource() }, indication = null, enabled = enabled, role = Role.Checkbox, onValueChange = onCheckedChange)
            .heightIn(min = MuSpacing.rowHeight)
            .padding(horizontal = MuSpacing.l, vertical = 10.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        MuCheckbox(checked, null, Modifier.padding(end = MuSpacing.m), enabled = enabled)
        RowTexts(title, subtitle, enabled, Modifier.weight(1f))
    }
}

/**
 * One choice of a single-choice list (naming policy, encoding…) when a [SegmentedControl] would not fit.
 * The selected row shows a check mark that pops in.
 */
@Composable
fun OptionRow(
    title: String,
    selected: Boolean,
    onClick: () -> Unit,
    modifier: Modifier = Modifier,
    subtitle: String? = null,
    enabled: Boolean = true,
) {
    ListRow(
        title = title,
        modifier = modifier.semantics { this.selected = selected },
        subtitle = subtitle,
        trailing = {
            AnimatedVisibility(selected, enter = fadeIn() + scaleIn(MuMotion.toggle(), initialScale = 0.4f), exit = fadeOut() + scaleOut(targetScale = 0.6f)) {
                Icon(MuIcons.Check, null, Modifier.size(22.dp), tint = MuTheme.colors.text)
            }
        },
        onClick = onClick,
        enabled = enabled,
    )
}

@Composable
private fun RowTexts(title: String, subtitle: String?, enabled: Boolean, modifier: Modifier) {
    Column(modifier.graphicsLayer { alpha = if (enabled) 1f else DisabledAlpha }) {
        Text(title, style = MuTheme.type.body)
        if (subtitle != null) Text(subtitle, style = MuTheme.type.subtitle, color = MuTheme.colors.secondary)
    }
}
