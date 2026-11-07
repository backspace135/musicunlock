package io.github.backspace135.musicunlock.ui.kit

import androidx.compose.animation.AnimatedVisibility
import androidx.compose.animation.fadeIn
import androidx.compose.animation.fadeOut
import androidx.compose.animation.scaleIn
import androidx.compose.animation.scaleOut
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.text.BasicTextField
import androidx.compose.foundation.text.KeyboardActions
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.material3.Icon
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.alpha
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.SolidColor
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.input.ImeAction
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import io.github.backspace135.musicunlock.R
import io.github.backspace135.musicunlock.ui.theme.MuShapes
import io.github.backspace135.musicunlock.ui.theme.MuSpacing
import io.github.backspace135.musicunlock.ui.theme.MuTheme

/**
 * Search box: magnifier, single line, clear button while not empty. The keyboard's search key calls
 * [onSearch]. For a search that expands from a top-bar icon use `MuTopBar(search = …)` instead.
 */
@Composable
fun SearchField(
    query: String,
    onQueryChange: (String) -> Unit,
    placeholder: String,
    modifier: Modifier = Modifier,
    onSearch: () -> Unit = {},
    enabled: Boolean = true,
) {
    val colors = MuTheme.colors
    Row(
        modifier
            .height(40.dp)
            .clip(MuShapes.small)
            .background(colors.fill)
            .padding(start = 10.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Icon(MuIcons.Search, null, Modifier.size(20.dp), tint = colors.secondary)
        Box(Modifier.weight(1f).padding(horizontal = MuSpacing.s), contentAlignment = Alignment.CenterStart) {
            if (query.isEmpty()) {
                Text(placeholder, style = MuTheme.type.body, color = colors.secondary, maxLines = 1, overflow = TextOverflow.Ellipsis)
            }
            BasicTextField(
                value = query,
                onValueChange = onQueryChange,
                enabled = enabled,
                singleLine = true,
                textStyle = MuTheme.type.body.copy(color = colors.text),
                cursorBrush = SolidColor(colors.text),
                keyboardOptions = KeyboardOptions(imeAction = ImeAction.Search),
                keyboardActions = KeyboardActions(onSearch = { onSearch() }),
                modifier = Modifier.fillMaxWidth(),
            )
        }
        AnimatedVisibility(query.isNotEmpty(), enter = fadeIn() + scaleIn(initialScale = 0.6f), exit = fadeOut() + scaleOut(targetScale = 0.6f)) {
            IconAction(MuIcons.Close, stringResource(R.string.common_clear), { onQueryChange("") }, Modifier.size(40.dp))
        }
        if (query.isEmpty()) Box(Modifier.width(MuSpacing.xs))
    }
}

/**
 * Labelled text input for settings (file-name template, time offset…). [label] sits above the field and
 * [caption] below it. For lyrics editing pass `singleLine = false, monospace = true`.
 */
@Composable
fun MuTextField(
    value: String,
    onValueChange: (String) -> Unit,
    modifier: Modifier = Modifier,
    label: String? = null,
    placeholder: String? = null,
    caption: String? = null,
    singleLine: Boolean = true,
    monospace: Boolean = false,
    enabled: Boolean = true,
    readOnly: Boolean = false,
    keyboardOptions: KeyboardOptions = KeyboardOptions.Default,
    keyboardActions: KeyboardActions = KeyboardActions.Default,
) {
    val colors = MuTheme.colors
    val style = (if (monospace) MuTheme.type.mono else MuTheme.type.body).copy(color = colors.text)
    Column(modifier.alpha(if (enabled) 1f else DisabledAlpha)) {
        if (label != null) {
            Text(label, style = MuTheme.type.caption, color = colors.secondary, modifier = Modifier.padding(bottom = MuSpacing.xs))
        }
        BasicTextField(
            value = value,
            onValueChange = onValueChange,
            enabled = enabled,
            readOnly = readOnly,
            singleLine = singleLine,
            textStyle = style,
            cursorBrush = SolidColor(colors.text),
            keyboardOptions = keyboardOptions,
            keyboardActions = keyboardActions,
            modifier = Modifier.fillMaxWidth(),
            decorationBox = { inner ->
                Box(
                    Modifier
                        .fillMaxWidth()
                        .heightIn(min = 44.dp)
                        .clip(MuShapes.small)
                        .background(colors.fill)
                        .padding(horizontal = MuSpacing.m, vertical = 11.dp),
                    contentAlignment = Alignment.CenterStart,
                ) {
                    if (value.isEmpty() && placeholder != null) Text(placeholder, style = style, color = colors.secondary)
                    inner()
                }
            },
        )
        if (caption != null) {
            Text(caption, style = MuTheme.type.caption, color = colors.secondary, modifier = Modifier.padding(top = MuSpacing.xs))
        }
    }
}
