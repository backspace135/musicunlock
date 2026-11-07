package io.github.backspace135.musicunlock.ui.kit

import androidx.activity.compose.BackHandler
import androidx.compose.animation.AnimatedVisibility
import androidx.compose.animation.core.animate
import androidx.compose.animation.core.animateFloatAsState
import androidx.compose.animation.core.spring
import androidx.compose.animation.expandHorizontally
import androidx.compose.animation.fadeIn
import androidx.compose.animation.fadeOut
import androidx.compose.animation.shrinkHorizontally
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.BoxWithConstraints
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.RowScope
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.statusBarsPadding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.text.BasicTextField
import androidx.compose.foundation.text.KeyboardActions
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.material3.Icon
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.Stable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableFloatStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.saveable.Saver
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.draw.clipToBounds
import androidx.compose.ui.draw.drawBehind
import androidx.compose.ui.focus.FocusRequester
import androidx.compose.ui.focus.focusRequester
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.SolidColor
import androidx.compose.ui.graphics.graphicsLayer
import androidx.compose.ui.input.nestedscroll.NestedScrollConnection
import androidx.compose.ui.input.nestedscroll.NestedScrollSource
import androidx.compose.ui.layout.layout
import androidx.compose.ui.platform.LocalFocusManager
import androidx.compose.ui.platform.LocalSoftwareKeyboardController
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.heading
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.input.ImeAction
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.Constraints
import androidx.compose.ui.unit.Velocity
import androidx.compose.ui.unit.dp
import androidx.compose.ui.util.lerp
import io.github.backspace135.musicunlock.R
import io.github.backspace135.musicunlock.ui.nav.LocalPageActive
import io.github.backspace135.musicunlock.ui.theme.MuShapes
import io.github.backspace135.musicunlock.ui.theme.MuSpacing
import io.github.backspace135.musicunlock.ui.theme.MuTheme
import kotlin.math.roundToInt

/**
 * Collapse state of a [MuTopBar] with a large title. Connect the page's scrolling list with
 * `Modifier.nestedScroll(state.nestedScrollConnection)`: scrolling the list up first folds the large title
 * into the small one; scrolling back to the top unfolds it. A release half-way snaps to the nearer end.
 */
@Stable
class TopBarState internal constructor(initialOffset: Float) {
    /** Height of the large-title area in px, measured by the bar. */
    internal var range by mutableFloatStateOf(0f)
    /** 0 = expanded, -[range] = collapsed. */
    internal var offset by mutableFloatStateOf(initialOffset)

    /** 0 when the large title is fully shown, 1 when it is folded away. */
    val collapsedFraction: Float get() = if (range <= 0f) 0f else (-offset / range).coerceIn(0f, 1f)

    val nestedScrollConnection: NestedScrollConnection = object : NestedScrollConnection {
        override fun onPreScroll(available: Offset, source: NestedScrollSource): Offset {
            if (available.y >= 0f) return Offset.Zero
            return Offset(0f, consume(available.y))
        }

        override fun onPostScroll(consumed: Offset, available: Offset, source: NestedScrollSource): Offset {
            if (available.y <= 0f) return Offset.Zero
            return Offset(0f, consume(available.y))
        }

        override suspend fun onPostFling(consumed: Velocity, available: Velocity): Velocity {
            val fraction = collapsedFraction
            if (fraction > 0f && fraction < 1f) {
                val target = if (fraction < 0.5f) 0f else -range
                animate(offset, target, animationSpec = spring(0.9f, 500f)) { value, _ -> offset = value }
            }
            return Velocity.Zero
        }
    }

    private fun consume(delta: Float): Float {
        val before = offset
        offset = (offset + delta).coerceIn(-range, 0f)
        return offset - before
    }

    internal companion object {
        val Saver = Saver<TopBarState, Float>(save = { it.offset }, restore = { TopBarState(it) })
    }
}

@Composable
fun rememberTopBarState(): TopBarState = rememberSaveable(saver = TopBarState.Saver) { TopBarState(0f) }

/**
 * Search that expands in place from a magnifier icon in the top bar. Keep [expanded] and [query] in the
 * page's state; collapsing (the "取消" button or back) calls [onExpandedChange] with false.
 */
class TopBarSearch(
    val query: String,
    val onQueryChange: (String) -> Unit,
    val placeholder: String,
    val expanded: Boolean,
    val onExpandedChange: (Boolean) -> Unit,
    val onSearch: () -> Unit = {},
)

/**
 * Page top bar. Under the status bar there is a 56 dp row with the optional back button, the small title,
 * the search icon and [actions] (use [IconAction] / [OverflowMenu]).
 *
 * - With [state], a large [title] sits below that row and folds into the small title as the content
 *   scrolls; [subtitle] (for example a login status line) folds with it. Without [state] only the small
 *   title is shown.
 * - With [search], tapping the magnifier grows a search field over the title with a spring, focuses it
 *   and shows "取消"; collapsing reverses it and hides the keyboard.
 */
@Composable
fun MuTopBar(
    title: String,
    modifier: Modifier = Modifier,
    state: TopBarState? = null,
    subtitle: (@Composable () -> Unit)? = null,
    onBack: (() -> Unit)? = null,
    search: TopBarSearch? = null,
    actions: @Composable RowScope.() -> Unit = {},
) {
    val colors = MuTheme.colors
    val searching = search?.expanded == true
    val searchProgress by animateFloatAsState(if (searching) 1f else 0f, spring(0.82f, 450f), label = "searchExpand")
    if (search != null) {
        BackHandler(enabled = searching && LocalPageActive.current) { search.onExpandedChange(false) }
    }
    Column(
        modifier
            .fillMaxWidth()
            .background(colors.background)
            .statusBarsPadding()
            .drawBehind {
                val shown = maxOf(state?.collapsedFraction ?: 0f, searchProgress)
                if (shown > 0f) {
                    drawRect(colors.separator, Offset(0f, size.height - 0.5.dp.toPx()), alpha = shown)
                }
            },
    ) {
        Row(
            Modifier
                .fillMaxWidth()
                .height(56.dp)
                .padding(start = if (onBack != null) MuSpacing.xs else MuSpacing.page, end = MuSpacing.xs),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            if (onBack != null) IconAction(MuIcons.Back, stringResource(R.string.common_back), onBack)
            BoxWithConstraints(Modifier.weight(1f).fillMaxHeight(), contentAlignment = Alignment.CenterStart) {
                Text(
                    title,
                    style = MuTheme.type.title,
                    maxLines = 1,
                    overflow = TextOverflow.Ellipsis,
                    modifier = Modifier
                        .padding(start = if (onBack != null) MuSpacing.xs else 0.dp, end = 44.dp)
                        .graphicsLayer {
                            val collapsed = state?.collapsedFraction ?: 1f
                            val shown = ((collapsed - 0.6f) / 0.4f).coerceIn(0f, 1f)
                            alpha = shown * (1f - searchProgress)
                            translationY = (1f - shown) * 6.dp.toPx()
                        },
                )
                if (search != null) {
                    ExpandingSearchField(search, searchProgress, maxWidth.value, Modifier.align(Alignment.CenterEnd))
                }
            }
            AnimatedVisibility(
                searching,
                enter = expandHorizontally(spring(0.82f, 450f)) + fadeIn(),
                exit = shrinkHorizontally(spring(0.82f, 450f)) + fadeOut(),
            ) {
                PlainButton(stringResource(R.string.common_cancel), { search?.onExpandedChange(false) })
            }
            AnimatedVisibility(
                !searching,
                enter = expandHorizontally(spring(0.82f, 450f)) + fadeIn(),
                exit = shrinkHorizontally(spring(0.82f, 450f)) + fadeOut(),
            ) {
                Row(verticalAlignment = Alignment.CenterVertically, content = actions)
            }
        }
        if (state != null) {
            Column(
                Modifier
                    .fillMaxWidth()
                    .clipToBounds()
                    .layout { measurable, constraints ->
                        val placeable = measurable.measure(constraints.copy(minHeight = 0, maxHeight = Constraints.Infinity))
                        val full = placeable.height.toFloat()
                        if (state.range != full) state.range = full
                        val height = ((full + state.offset) * (1f - searchProgress)).roundToInt().coerceAtLeast(0)
                        layout(placeable.width, height) { placeable.place(0, height - placeable.height) }
                    }
                    .graphicsLayer { alpha = (1f - state.collapsedFraction * 1.4f).coerceIn(0f, 1f) * (1f - searchProgress) }
                    .padding(start = MuSpacing.page, end = MuSpacing.page, bottom = MuSpacing.s),
            ) {
                Text(title, style = MuTheme.type.largeTitle, maxLines = 1, overflow = TextOverflow.Ellipsis, modifier = Modifier.semantics { heading() })
                if (subtitle != null) subtitle()
            }
        }
    }
}

/** Magnifier icon that grows leftwards into a search field; [progress] 0 = icon, 1 = full field. */
@Composable
private fun ExpandingSearchField(search: TopBarSearch, progress: Float, maxWidthDp: Float, modifier: Modifier) {
    val colors = MuTheme.colors
    val focus = remember { FocusRequester() }
    val focusManager = LocalFocusManager.current
    val keyboard = LocalSoftwareKeyboardController.current
    LaunchedEffect(search.expanded) {
        if (search.expanded) {
            focus.requestFocus()
            keyboard?.show()
        } else {
            focusManager.clearFocus()
        }
    }
    val width = lerp(MuSpacing.touchTarget.value, maxWidthDp, progress).dp
    Row(
        modifier
            .width(width)
            .height(40.dp)
            .clip(MuShapes.small)
            .drawBehind { drawRect(colors.fill, alpha = progress.coerceIn(0f, 1f)) },
        verticalAlignment = Alignment.CenterVertically,
    ) {
        if (search.expanded || progress > 0.01f) {
            Box(Modifier.size(40.dp), contentAlignment = Alignment.Center) {
                Icon(MuIcons.Search, null, Modifier.size(20.dp), tint = colors.secondary)
            }
            Box(Modifier.weight(1f).graphicsLayer { alpha = ((progress - 0.4f) / 0.6f).coerceIn(0f, 1f) }, contentAlignment = Alignment.CenterStart) {
                if (search.query.isEmpty()) {
                    Text(search.placeholder, style = MuTheme.type.body, color = colors.secondary, maxLines = 1, overflow = TextOverflow.Ellipsis)
                }
                BasicTextField(
                    value = search.query,
                    onValueChange = search.onQueryChange,
                    singleLine = true,
                    textStyle = MuTheme.type.body.copy(color = colors.text),
                    cursorBrush = SolidColor(colors.text),
                    keyboardOptions = KeyboardOptions(imeAction = ImeAction.Search),
                    keyboardActions = KeyboardActions(onSearch = { search.onSearch() }),
                    modifier = Modifier.fillMaxWidth().focusRequester(focus),
                )
            }
            if (search.query.isNotEmpty()) {
                IconAction(MuIcons.Close, stringResource(R.string.common_clear), { search.onQueryChange("") }, Modifier.size(40.dp))
            } else {
                Spacer(Modifier.width(MuSpacing.s))
            }
        } else {
            IconAction(MuIcons.Search, stringResource(R.string.common_search), { search.onExpandedChange(true) })
        }
    }
}
