package io.github.backspace135.musicunlock.ui.playground

import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.WindowInsets
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.navigationBars
import androidx.compose.foundation.layout.navigationBarsPadding
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.windowInsetsBottomHeight
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.LazyListScope
import androidx.compose.foundation.lazy.itemsIndexed
import androidx.compose.material3.Slider
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableFloatStateOf
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.input.nestedscroll.nestedScroll
import androidx.compose.ui.unit.dp
import io.github.backspace135.musicunlock.ui.kit.ActionFooter
import io.github.backspace135.musicunlock.ui.kit.CheckRow
import io.github.backspace135.musicunlock.ui.kit.ConfirmDialog
import io.github.backspace135.musicunlock.ui.kit.EmptyState
import io.github.backspace135.musicunlock.ui.kit.JobGlyph
import io.github.backspace135.musicunlock.ui.kit.LazyGroupItem
import io.github.backspace135.musicunlock.ui.kit.ListGroup
import io.github.backspace135.musicunlock.ui.kit.ListRow
import io.github.backspace135.musicunlock.ui.kit.MenuItem
import io.github.backspace135.musicunlock.ui.kit.MuBottomSheet
import io.github.backspace135.musicunlock.ui.kit.MuCheckbox
import io.github.backspace135.musicunlock.ui.kit.MuIcons
import io.github.backspace135.musicunlock.ui.kit.MuProgressBar
import io.github.backspace135.musicunlock.ui.kit.MuSpinner
import io.github.backspace135.musicunlock.ui.kit.MuSwitch
import io.github.backspace135.musicunlock.ui.kit.MuTextField
import io.github.backspace135.musicunlock.ui.kit.MuTopBar
import io.github.backspace135.musicunlock.ui.kit.NoticeBanner
import io.github.backspace135.musicunlock.ui.kit.OptionRow
import io.github.backspace135.musicunlock.ui.kit.OverflowMenu
import io.github.backspace135.musicunlock.ui.kit.PlainButton
import io.github.backspace135.musicunlock.ui.kit.PrimaryButton
import io.github.backspace135.musicunlock.ui.kit.RowDivider
import io.github.backspace135.musicunlock.ui.kit.SearchField
import io.github.backspace135.musicunlock.ui.kit.SecondaryButton
import io.github.backspace135.musicunlock.ui.kit.SectionHeader
import io.github.backspace135.musicunlock.ui.kit.SegmentedControl
import io.github.backspace135.musicunlock.ui.kit.StatusGlyph
import io.github.backspace135.musicunlock.ui.kit.SwipeToDeleteRow
import io.github.backspace135.musicunlock.ui.kit.SwitchRow
import io.github.backspace135.musicunlock.ui.kit.TopBarSearch
import io.github.backspace135.musicunlock.ui.kit.pressable
import io.github.backspace135.musicunlock.ui.kit.rememberTopBarState
import io.github.backspace135.musicunlock.ui.nav.LocalNavigator
import io.github.backspace135.musicunlock.ui.nav.Route
import io.github.backspace135.musicunlock.ui.theme.LocalThemeController
import io.github.backspace135.musicunlock.ui.theme.MuMotion
import io.github.backspace135.musicunlock.ui.theme.MuShapes
import io.github.backspace135.musicunlock.ui.theme.MuSpacing
import io.github.backspace135.musicunlock.ui.theme.MuTheme
import io.github.backspace135.musicunlock.ui.theme.ThemeMode
import java.util.Locale

/**
 * Debug-only component playground (设置 → 开发 → 组件演示与动画调参). Every kit component next to sliders
 * for the [MuMotion] values it uses. Changes apply live and are lost on restart; copy the final values
 * from the "当前参数" block into MuMotion and apps/android/DESIGN.md. Developer tool: texts are inline,
 * not in strings.xml.
 */
object DebugTools {
    const val available = true

    @Composable
    fun Playground() {
        PlaygroundScreen()
    }
}

private class Tunable(val label: String, val range: ClosedFloatingPointRange<Float>, val get: () -> Float, val set: (Float) -> Unit, val integer: Boolean = false)

private val Defaults: Map<String, Float> by lazy { allTunables().associate { it.label to it.get() } }

private fun allTunables() = pressTunables + toggleTunables + pageTunables + sheetTunables + listTunables + themeTunables

private val pressTunables = listOf(
    Tunable("pressScale", 0.8f..1f, { MuMotion.pressScale }, { MuMotion.pressScale = it }),
    Tunable("pressDamping", 0.2f..1f, { MuMotion.pressDamping }, { MuMotion.pressDamping = it }),
    Tunable("pressStiffness", 100f..1500f, { MuMotion.pressStiffness }, { MuMotion.pressStiffness = it }),
    Tunable("pressMinMillis", 0f..250f, { MuMotion.pressMinMillis.toFloat() }, { MuMotion.pressMinMillis = it.toInt() }, integer = true),
)
private val toggleTunables = listOf(
    Tunable("toggleDamping", 0.2f..1f, { MuMotion.toggleDamping }, { MuMotion.toggleDamping = it }),
    Tunable("toggleStiffness", 100f..1500f, { MuMotion.toggleStiffness }, { MuMotion.toggleStiffness = it }),
)
private val pageTunables = listOf(
    Tunable("pageDurationMillis", 120f..700f, { MuMotion.pageDurationMillis.toFloat() }, { MuMotion.pageDurationMillis = it.toInt() }, integer = true),
    Tunable("pageParallax", 0f..0.6f, { MuMotion.pageParallax }, { MuMotion.pageParallax = it }),
    Tunable("pageScrim", 0f..0.4f, { MuMotion.pageScrim }, { MuMotion.pageScrim = it }),
    Tunable("pageSettleStiffness", 100f..1500f, { MuMotion.pageSettleStiffness }, { MuMotion.pageSettleStiffness = it }),
)
private val sheetTunables = listOf(
    Tunable("sheetDamping", 0.3f..1f, { MuMotion.sheetDamping }, { MuMotion.sheetDamping = it }),
    Tunable("sheetStiffness", 100f..1500f, { MuMotion.sheetStiffness }, { MuMotion.sheetStiffness = it }),
    Tunable("sheetProjection", 0f..0.4f, { MuMotion.sheetProjection }, { MuMotion.sheetProjection = it }),
)
private val listTunables = listOf(
    Tunable("listDamping", 0.3f..1f, { MuMotion.listDamping }, { MuMotion.listDamping = it }),
    Tunable("listStiffness", 100f..1500f, { MuMotion.listStiffness }, { MuMotion.listStiffness = it }),
)
private val themeTunables = listOf(
    Tunable("themeRevealMillis", 150f..1200f, { MuMotion.themeRevealMillis.toFloat() }, { MuMotion.themeRevealMillis = it.toInt() }, integer = true),
)

@Composable
private fun PlaygroundScreen() {
    val navigator = LocalNavigator.current
    val theme = LocalThemeController.current
    val topBar = rememberTopBarState()
    remember { Defaults } // Capture the defaults before the first change.
    var query by rememberSaveable { mutableStateOf("") }
    var searching by rememberSaveable { mutableStateOf(false) }
    var sheet by rememberSaveable { mutableStateOf(false) }
    var dialog by rememberSaveable { mutableStateOf(false) }
    var banner by rememberSaveable { mutableStateOf(true) }
    var footer by rememberSaveable { mutableStateOf(false) }
    var running by rememberSaveable { mutableStateOf(false) }
    var progress by rememberSaveable { mutableFloatStateOf(0.35f) }
    var switchOn by rememberSaveable { mutableStateOf(true) }
    var checked by rememberSaveable { mutableStateOf(true) }
    var segment by rememberSaveable { mutableIntStateOf(0) }
    var option by rememberSaveable { mutableIntStateOf(0) }
    var selecting by rememberSaveable { mutableStateOf(false) }
    var glyph by rememberSaveable { mutableIntStateOf(0) }
    var items by remember { mutableStateOf((1..5).toList()) }
    var nextItem by remember { mutableIntStateOf(6) }
    var text by rememberSaveable { mutableStateOf("") }
    var version by remember { mutableIntStateOf(0) } // Bumped by reset so the sliders re-read.

    Box(Modifier.fillMaxSize()) {
        Column(Modifier.fillMaxSize()) {
            MuTopBar(
                "组件演示",
                state = topBar,
                onBack = navigator::pop,
                search = TopBarSearch(query, { query = it }, "原地展开的搜索框", searching, { searching = it }),
                actions = {
                    OverflowMenu(
                        listOf(
                            MenuItem("恢复默认参数") {
                                Defaults.forEach { (label, value) -> allTunables().first { it.label == label }.set(value) }
                                version++
                            },
                            MenuItem("再推入一页") { navigator.push(Route.Playground) },
                        ),
                    )
                },
            )
            LazyColumn(Modifier.fillMaxSize().nestedScroll(topBar.nestedScrollConnection), contentPadding = PaddingValues(bottom = 120.dp)) {
                item { NoticeBanner(if (banner) "提示横幅：可关闭，淡出。" else null, onDismiss = { banner = false }) }

                section("按压反馈", pressTunables, version) {
                    Row(horizontalArrangement = Arrangement.spacedBy(MuSpacing.s)) {
                        PrimaryButton("主按钮", {})
                        SecondaryButton("次按钮", {})
                        PlainButton("文字按钮", {})
                    }
                    Spacer(Modifier.height(MuSpacing.m))
                    Box(
                        Modifier
                            .fillMaxWidth()
                            .pressable({})
                            .clip(MuShapes.medium)
                            .background(MuTheme.colors.surface)
                            .padding(MuSpacing.l),
                    ) { Text("可按压的卡片", style = MuTheme.type.body) }
                }

                section("开关、勾选、分段、多选", toggleTunables, version) {
                    Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(MuSpacing.l)) {
                        MuSwitch(switchOn, { switchOn = it })
                        MuCheckbox(checked, { checked = it })
                        StatusGlyph(JobGlyph.entries[glyph], null, Modifier.pressable({ glyph = (glyph + 1) % JobGlyph.entries.size }))
                        MuSpinner()
                        Text("点状态图标切换", style = MuTheme.type.caption, color = MuTheme.colors.secondary)
                    }
                    Spacer(Modifier.height(MuSpacing.m))
                    SegmentedControl(listOf("全部", "待转换", "已完成", "未完成"), segment, { segment = it })
                    Spacer(Modifier.height(MuSpacing.m))
                    Column(Modifier.clip(MuShapes.medium).background(MuTheme.colors.surface)) {
                        SwitchRow("开关行", switchOn, { switchOn = it }, subtitle = "整行可点，拇指按下时拉长")
                        RowDivider()
                        CheckRow("勾选行", checked, { checked = it })
                        RowDivider()
                        listOf("保留原文件名", "歌手 - 歌曲名").forEachIndexed { index, label ->
                            OptionRow(label, option == index, { option = index })
                        }
                        RowDivider()
                        listOf("长按进入多选", "再点一下退出").forEachIndexed { index, label ->
                            ListRow(label, selectionMode = selecting, selected = selecting && index == 0, onClick = { selecting = !selecting }, onLongClick = { selecting = true })
                        }
                    }
                }

                section("页面栈", pageTunables, version) {
                    Text("右滑本页任意位置返回；Android 14+ 的系统返回手势会预览返回。", style = MuTheme.type.caption, color = MuTheme.colors.secondary)
                    Spacer(Modifier.height(MuSpacing.s))
                    SecondaryButton("推入一页", { navigator.push(Route.Playground) })
                }

                section("底部弹窗、对话框", sheetTunables, version) {
                    Row(horizontalArrangement = Arrangement.spacedBy(MuSpacing.s)) {
                        SecondaryButton("打开底部弹窗", { sheet = true })
                        SecondaryButton("确认对话框", { dialog = true })
                    }
                }

                section("列表增删和重排", listTunables, version) {
                    Row(horizontalArrangement = Arrangement.spacedBy(MuSpacing.s)) {
                        SecondaryButton("添加", { items = listOf(nextItem++) + items }, small = true)
                        SecondaryButton("打乱", { items = items.shuffled() }, small = true)
                        SecondaryButton("排序", { items = items.sorted() }, small = true)
                    }
                    Text("左滑一行移出。", style = MuTheme.type.caption, color = MuTheme.colors.secondary, modifier = Modifier.padding(top = MuSpacing.s))
                }
                itemsIndexed(items, key = { _, it -> it }) { index, value ->
                    LazyGroupItem(index, items.size, Modifier.animateItem(fadeInSpec = MuMotion.fadeIn(), placementSpec = MuMotion.list(), fadeOutSpec = MuMotion.fadeOut())) {
                        SwipeToDeleteRow(onDelete = { items = items - value }, label = "移出") {
                            ListRow("第 $value 行", subtitle = "可左滑移出", leading = { StatusGlyph(JobGlyph.Pending, null) })
                        }
                    }
                }

                section("主题切换", themeTunables, version) {
                    SegmentedControl(listOf("跟随系统", "浅色", "深色"), theme.mode.ordinal, { theme.setMode(ThemeMode.entries[it]) })
                }

                section("底栏、进度、输入框", emptyList(), version) {
                    Row(horizontalArrangement = Arrangement.spacedBy(MuSpacing.s)) {
                        SecondaryButton(if (footer) "隐藏底栏" else "显示底栏", { footer = !footer }, small = true)
                        SecondaryButton(if (running) "切回文字" else "切到进度", { running = !running }, small = true)
                    }
                    Spacer(Modifier.height(MuSpacing.m))
                    MuProgressBar(progress)
                    Slider(progress, { progress = it })
                    SearchField(query, { query = it }, "搜索框")
                    Spacer(Modifier.height(MuSpacing.m))
                    MuTextField(text, { text = it }, label = "文本框", placeholder = "\${name} - \${singer}", caption = "说明文字")
                }

                item {
                    SectionHeader("空态模板")
                    EmptyState("把音乐放进来", body = "选择文件或整个文件夹\n恢复为普通播放器也能打开的音频", icon = MuIcons.MusicNote, drift = true) {
                        PrimaryButton("选择音乐文件", {})
                    }
                }

                item(key = "values-$version") {
                    SectionHeader("当前参数（复制到 MuMotion 和 DESIGN.md）")
                    Text(
                        allTunables().joinToString("\n") { "${it.label} = ${format(it, it.get())}" },
                        style = MuTheme.type.mono,
                        modifier = Modifier.padding(horizontal = MuSpacing.page),
                    )
                    Spacer(Modifier.windowInsetsBottomHeight(WindowInsets.navigationBars))
                }
            }
        }
        ActionFooter(
            title = if (running) "正在转换 2 / 5" else "5 个文件待转换",
            detail = "同名文件自动编号，源文件保持不变。",
            progress = if (running) progress else null,
            actionLabel = if (running) "停止转换" else "开始转换",
            onAction = { running = !running },
            secondaryLabel = if (running) null else "重试未完成",
            visible = footer,
            modifier = Modifier.align(Alignment.BottomCenter).navigationBarsPadding(),
        )
    }

    MuBottomSheet(sheet, { sheet = false }, title = "底部弹窗") {
        Text(
            "拖动把手或标题；列表滚到顶后继续下拉会带动弹窗。松手时按速度决定收起或弹回。",
            style = MuTheme.type.subtitle,
            color = MuTheme.colors.secondary,
            modifier = Modifier.padding(horizontal = MuSpacing.page + MuSpacing.xs),
        )
        LazyColumn(Modifier.height(360.dp)) {
            items(30) { index -> ListRow("第 ${index + 1} 项", onClick = {}) }
        }
    }
    ConfirmDialog(
        visible = dialog,
        title = "清空队列？",
        message = "未开始的文件会一起移出，源文件保持不变。",
        confirmLabel = "清空队列",
        onConfirm = { dialog = false },
        onDismissRequest = { dialog = false },
        icon = MuIcons.Warning,
    )
}

private fun LazyListScope.section(title: String, tunables: List<Tunable>, version: Int, demo: @Composable () -> Unit) {
    item(key = "section-$title") {
        SectionHeader(title)
        Column(Modifier.padding(horizontal = MuSpacing.page)) { demo() }
    }
    if (tunables.isNotEmpty()) {
        item(key = "tunables-$title-$version") {
            ListGroup(Modifier.padding(top = MuSpacing.m)) {
                tunables.forEach { TunableSlider(it) }
            }
        }
    }
}

@Composable
private fun TunableSlider(tunable: Tunable) {
    var value by remember { mutableFloatStateOf(tunable.get()) }
    Column(Modifier.padding(horizontal = MuSpacing.l, vertical = MuSpacing.xs)) {
        Row {
            Text(tunable.label, style = MuTheme.type.caption, modifier = Modifier.weight(1f))
            Text(format(tunable, value), style = MuTheme.type.caption, color = MuTheme.colors.secondary)
        }
        Slider(
            value = value,
            onValueChange = {
                value = if (tunable.integer) it.toInt().toFloat() else it
                tunable.set(value)
            },
            valueRange = tunable.range,
            modifier = Modifier.fillMaxWidth().height(32.dp),
        )
    }
}

private fun format(tunable: Tunable, value: Float): String =
    if (tunable.integer) value.toInt().toString() else String.format(Locale.ROOT, "%.2f", value)
