package io.github.backspace135.musicunlock.ui.kit

import androidx.compose.animation.core.animateFloatAsState
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.navigationBarsPadding
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.selection.selectableGroup
import androidx.compose.material3.Icon
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.Immutable
import androidx.compose.runtime.getValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.drawBehind
import androidx.compose.ui.geometry.CornerRadius
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.selected
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import io.github.backspace135.musicunlock.ui.theme.MuMotion
import io.github.backspace135.musicunlock.ui.theme.MuSpacing
import io.github.backspace135.musicunlock.ui.theme.MuTheme

@Immutable
data class NavItem(val label: String, val icon: ImageVector)

/**
 * App-level bottom navigation between workspaces. The selected item gets a grey pill behind its icon
 * that springs open and a bold label, so the selection does not depend on colour.
 */
@Composable
fun BottomNavigation(items: List<NavItem>, selectedIndex: Int, onSelect: (Int) -> Unit, modifier: Modifier = Modifier) {
    val colors = MuTheme.colors
    Row(
        modifier
            .fillMaxWidth()
            .background(colors.surface)
            .drawBehind { drawRect(colors.separator, size = Size(size.width, 0.5.dp.toPx())) }
            .navigationBarsPadding()
            .height(64.dp)
            .selectableGroup(),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        items.forEachIndexed { index, item ->
            val selected = index == selectedIndex
            val pill by animateFloatAsState(if (selected) 1f else 0f, MuMotion.toggle(), label = "navPill")
            Column(
                Modifier
                    .weight(1f)
                    .semantics { this.selected = selected }
                    .pressable({ onSelect(index) }, role = Role.Tab, pressedScale = 0.9f)
                    .padding(vertical = MuSpacing.xs),
                horizontalAlignment = Alignment.CenterHorizontally,
            ) {
                Box(
                    Modifier
                        .size(width = 64.dp, height = 32.dp)
                        .drawBehind {
                            if (pill > 0f) {
                                val width = size.width * pill.coerceAtMost(1.1f)
                                drawRoundRect(
                                    colors.fill,
                                    topLeft = Offset((size.width - width) / 2, 0f),
                                    size = Size(width, size.height),
                                    cornerRadius = CornerRadius(size.height / 2),
                                )
                            }
                        },
                    contentAlignment = Alignment.Center,
                ) {
                    Icon(item.icon, null, Modifier.size(24.dp), tint = if (selected) colors.text else colors.secondary)
                }
                Text(
                    item.label,
                    style = MuTheme.type.caption,
                    fontWeight = if (selected) FontWeight.SemiBold else FontWeight.Normal,
                    color = if (selected) colors.text else colors.secondary,
                    modifier = Modifier.padding(top = 2.dp),
                )
            }
        }
    }
}
