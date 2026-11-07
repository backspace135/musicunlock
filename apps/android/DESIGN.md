# 音匣 Android 设计与组件用法

这份文档是 Android 界面的唯一依据。#31（解封装）、#32（歌词）、#33（账号）的页面都用这里的组件拼出来，
**照着示例代码写，不要自己另做按钮、开关、弹窗**。交互、文案和规则仍以 `apps/macos/DESIGN.md` 和
`apps/windows/app/` 为准；本文只说明 Android 上怎么实现。

- 只用黑、白、灰。**状态靠图标、文字、形状或位置区分，不能只靠颜色**。
- 交互手感参照 Telegram（跟手右滑返回、弹簧动画、可拖拽弹窗），全部自己实现，不复制 Telegram 代码（GPL-2.0）。
- 不用 Material 的动态取色，也不直接用 Material 3 的 `Button`、`Switch`、`Checkbox`、`ModalBottomSheet`、
  `AlertDialog`、`DropdownMenu`、`TopAppBar`、`NavigationBar`，这些都有对应的组件（见“组件清单”）。
  `Text`、`Icon`、`Slider` 可以直接用。

目录：

1. [文件放在哪里](#1-文件放在哪里)
2. [新增一个页面](#2-新增一个页面)
3. [文案](#3-文案)
4. [主题 token](#4-主题-token)
5. [动效参数](#5-动效参数)
6. [组件清单：什么时候用哪个](#6-组件清单什么时候用哪个)
7. [组件用法（可直接复制）](#7-组件用法可直接复制)
8. [整页模板](#8-整页模板)
9. [常见错误](#9-常见错误)
10. [调参页与验证](#10-调参页与验证)

---

## 1. 文件放在哪里

代码都在 `app/src/main/kotlin/io/github/backspace135/musicunlock/` 下：

```text
MainActivity.kt          主题 → 弹层 → 页面栈，不需要改
ui/theme/                颜色、字体、间距、圆角、动效参数、主题切换（#30）
ui/kit/                  所有组件（#30）。页面只用这里的组件
ui/nav/Navigator.kt      页面栈（#30）
ui/nav/PageStackHost.kt  页面栈的动画和手势（#30）
ui/nav/Routes.kt         ★ 所有页面在这里登记（各 issue 只追加自己的条目）
ui/shell/AppShell.kt     底部导航：解封装 / 歌词两个工作区（#30）
ui/unlock/               #31：UnlockScreen.kt、UnlockViewModel.kt、OutputSettingsSection.kt
ui/lyrics/               #32：LyricsScreen.kt、LyricsEditorScreen.kt、LyricsViewModel.kt、LyricsExportSettingsSection.kt
ui/accounts/             #33：AccountsScreen.kt、AccountSettingsSection.kt
ui/settings/             设置页骨架、关于、开源许可（#30）
app/src/debug/.../ui/playground/DebugTools.kt   组件演示与调参页，只在 debug 版里
app/src/release/.../ui/playground/DebugTools.kt release 版的空实现
```

**谁改哪些文件：**

| 文件 | 负责 | 说明 |
| --- | --- | --- |
| `ui/unlock/*` | #31 | 文件开头写着 `PLACEHOLDER (#30)` / `FAKE DATA (#30)`，整个替换掉 |
| `ui/lyrics/*` | #32 | 同上 |
| `ui/accounts/*` | #33 | 同上 |
| `ui/nav/Routes.kt` | 共用 | 只在自己的注释块下面**追加**路由，不要重排或删别人的 |
| `res/values/strings.xml` | 共用 | 只在自己的区块末尾追加 |
| `AndroidManifest.xml`、`gradle/libs.versions.toml` | 共用 | 只追加 |
| `ui/kit/*`、`ui/theme/*`、`ui/nav/Navigator.kt`、`PageStackHost.kt`、`ui/shell/*`、`ui/settings/*` | #30 | 不要改。缺组件或发现 bug 时在 PR 里说明 |

**设置页的区块**：`ui/settings/SettingsScreen.kt` 依次调用下面几个函数，每个函数在负责它的 issue 的包里，
所以并行开发时不会改到同一个文件：

| 区块 | 函数 | 文件 | 负责 |
| --- | --- | --- | --- |
| 输出（输出文件夹、文件命名） | `OutputSettingsSection()` | `ui/unlock/OutputSettingsSection.kt` | #31 |
| 歌词导出 | `LyricsExportSettingsSection()` | `ui/lyrics/LyricsExportSettingsSection.kt` | #32 |
| 账号 | `AccountSettingsSection()` | `ui/accounts/AccountSettingsSection.kt` | #33 |
| 外观（主题）、关于（含开源许可）、开发 | 写在 `SettingsScreen.kt` 里 | — | #30，已完成 |

`OutputSettingsSection()` 同时用在解封装队列的“导出设置”底部弹窗里；`LyricsExportSettingsContent()`
同时用在歌词工作区“⋯”菜单的“导出设置”弹窗里。改一处，两个入口一起生效。

## 2. 新增一个页面

页面就是一个 `@Composable` 函数。推入后可以右滑返回、系统返回，Android 14+ 有预测性返回，都不用自己写。

**第 1 步**：在 `ui/nav/Routes.kt` 里，在自己 issue 的注释块下面加一个路由：

```kotlin
// #31 解封装: the workspace itself lives in the shell; add pushed pages here.
/** Details of one queue job. */
data class UnlockJobDetail(val jobId: Long) : Route
```

路由的参数只能是 `String`、`Int`、`Long`、`Boolean`、枚举这类可以序列化的值（App 被系统杀掉后要能恢复）。
不要把对象、列表、回调放进路由。

**第 2 步**：在同一个文件的 `RouteContent` 里加一行（`when` 少写分支会编译失败，正好提醒你）：

```kotlin
is Route.UnlockJobDetail -> UnlockJobDetailScreen(route.jobId)
```

**第 3 步**：在自己的包里写页面，从别的页面推入：

```kotlin
val navigator = LocalNavigator.current
PrimaryButton("查看详情", onClick = { navigator.push(Route.UnlockJobDetail(job.id)) })
```

返回用 `navigator.pop()`（顶栏的返回按钮写 `onBack = navigator::pop`）。`navigator.popToRoot()` 回到底部导航。

**切换工作区**（例如解封装里的“查找这首歌的歌词”）：

```kotlin
val shell = LocalShell.current
shell.open(Workspace.Lyrics)
```

**ViewModel**：在页面里直接 `viewModel()`。每个页面有自己的 ViewModelStore，页面出栈时 ViewModel 被清除；
两个工作区的 ViewModel 属于根页面，切换工作区、推入设置页都不会丢。

```kotlin
@Composable
fun UnlockJobDetailScreen(jobId: Long, vm: UnlockJobDetailViewModel = viewModel()) { … }
```

ViewModel 里的状态用 `mutableStateOf`（像 `UnlockViewModel.kt` 那样）或 `StateFlow` +
`collectAsStateWithLifecycle()` 都可以。

**页面里的返回键**：被盖住的页面仍然在组合里（这样它的状态和正在运行的任务不会丢），所以页面里的
`BackHandler` 必须加上 `LocalPageActive.current`，否则会抢走上层页面的返回键：

```kotlin
BackHandler(enabled = LocalPageActive.current && vm.selectionMode) { vm.endSelection() }
```

底部弹窗、对话框、菜单、顶栏搜索框自带返回处理，不用再写。

## 3. 文案

- 所有界面文字放在 `app/src/main/res/values/strings.xml`，只有中文。用词和 Windows / macOS 版一致。
- 命名：`<区域>_<内容>[_<类型>]`，全小写下划线。
  - 区域：`common`（通用词）、`nav`、`unlock`、`lyrics`、`accounts`、`settings`、`about`、`licenses`
  - 类型：`title`（标题）、`body`（正文）、`action`（按钮）、`hint`（提示）、`label`（标签）、
    `description`（TalkBack 读的说明）、`format`（带参数）
  - 例子：`unlock_empty_title`、`unlock_start_action`、`unlock_count_format`、`unlock_add_files_description`
- 带参数用 `%1$d`、`%1$s`，取用：`stringResource(R.string.unlock_count_format, count)`。
- 换行写 `\n`。中文标点用全角。
- 新文案加在自己区域的最后，不要改别人的。
- 核心（`shared/core`）返回的文案直接显示，不要再放进 strings.xml。
- 例外：debug 版的调参页是开发工具，文字直接写在代码里。

ViewModel 拿不到 `Context` 时，用资源 id 加参数传给界面，例如 `UnlockViewModel.kt` 里的 `Notice`：

```kotlin
data class Notice(@StringRes val text: Int, val args: List<Any> = emptyList())
// 界面上
NoticeBanner(notice?.let { stringResource(it.text, *it.args.toTypedArray()) }, onDismiss = { vm.notice = null })
```

## 4. 主题 token

在代码里通过 `MuTheme.colors.x`、`MuTheme.type.x`、`MuSpacing.x`、`MuShapes.x` 取用，**不要写颜色值和 dp 字面量**
（组件内部的固定尺寸除外）。

### 颜色

macOS 的五个 token 原样沿用（R = G = B）；后几项对应 Windows `Theme.xaml` 里的半透明画刷。

| token | 浅色 | 深色 | 用途 | macOS / Windows 对应 |
| --- | --- | --- | --- | --- |
| `background` | #F2F2F2 | #1E1E1E | 页面底色、底部弹窗 | 工作区 / `MuWorkspaceBrush` |
| `surface` | #FAFAFA | #282828 | 分组卡片、底栏、底部导航 | 主表面 / `MuSurfaceBrush` |
| `elevated` | #FFFFFF | #303030 | 对话框、菜单 | （无，玻璃层） |
| `text` | #1C1C1C | #F2F2F2 | 主文字、主按钮底色、状态图标 | 主文字 / `MuTextBrush` |
| `secondary` | #636363 | #A0A0A0 | 次文字、说明、未选中图标 | 次文字 / `MuSecondaryBrush` |
| `separator` | #E0E0E0 | #3A3A3A | 分隔线、开关关闭时的轨道 | 分隔线 / `MuSeparatorBrush` |
| `fill` | text 5.5 % | text 7.8 % | 分段控件轨道、次按钮、搜索框、封面占位 | `MuChipBrush` |
| `highlight` | text 5.9 % | text 6.3 % | 行按下、转换中的行、当前行 | `MuRunningRowBrush` |
| `thumb` | #FFFFFF | #5A5A5A | 分段控件选中块、开关关闭时的圆钮 | 系统控件 |
| `onText` | #FAFAFA | #1C1C1C | 主按钮上的文字和图标 | — |
| `scrim` | 黑 40 % | 黑 60 % | 弹窗背后的遮罩 | — |

深色模式只用灰阶，不跟随系统动态取色。主题在“设置 → 外观”里选：跟随系统 / 浅色 / 深色，保存在
`SharedPreferences("settings")` 的 `themeMode`（`system` / `light` / `dark`）。

### 字体

系统字体（Roboto + Noto Sans CJK）。字号按手机阅读距离从桌面版放大，层级一一对应：

| token | 大小 / 行高 | 字重 | 用途 | macOS / Windows |
| --- | --- | --- | --- | --- |
| `largeTitle` | 30 / 36 sp | SemiBold | 顶栏大标题 | 标题 23 pt / `MuPageTitleStyle` 20 |
| `title` | 19 / 24 sp | SemiBold | 顶栏小标题、弹窗标题 | — |
| `headline` | 17 / 22 sp | SemiBold | 页内小节标题、空态标题 | `MuSectionTitleStyle` 15 |
| `body` | 16 / 22 sp | Regular | 行标题、正文 | 行标题 13 pt |
| `label` | 15 / 20 sp | Medium | 按钮文字 | `MuLabelStyle` 12 |
| `subtitle` | 14 / 19 sp | Regular | 行副标题 | 辅助信息 12 pt |
| `caption` | 13 / 18 sp | Regular | 说明、提示、分组脚注 | `MuCaptionStyle` 12 |
| `sectionHeader` | 13 / 18 sp | Medium | 分组上方的小标题 | — |
| `mono` | 14 / 21 sp | 等宽 | 歌词编辑、导出预览 | 等宽 |

数字一律用等宽数字（`tnum`），计数和进度跳动时不抖。次要文字写 `color = MuTheme.colors.secondary`。

### 间距与圆角

4 / 8 dp 网格：`MuSpacing.xxs` 2、`xs` 4、`s` 8、`m` 12、`l` 16、`xl` 24、`xxl` 32；
`page` 16（页面左右边距，桌面版是 24）；`rowHeight` 56（单行）；`twoLineRowHeight` 64（带副标题）；
`touchTarget` 48（最小触控区域）。

圆角：`MuShapes.small` 10（分段控件、搜索框、封面）、`medium` 16（分组卡片、横幅、菜单）、
`large` 22（底栏、对话框，对应 macOS 的 20–22 pt 玻璃面板）、`sheet` 顶部 24、`capsule` 胶囊（按钮）。

## 5. 动效参数

参数都在 `ui/theme/Motion.kt` 的 `MuMotion` 里，用调参页（见第 10 节）在模拟器上逐项调过，下表就是最终值。
弹簧用阻尼比（1 = 不回弹，越小越弹）和刚度（越大越快）描述。系统“移除动画”打开时，Compose 会把所有动画时长缩为 0，
不需要单独处理；macOS / Windows 的“减少动态效果”在 Android 上就对应这个开关。

| 交互 | 参数 | 值 | macOS 对应 | Windows 对应 |
| --- | --- | --- | --- | --- |
| 按压反馈 | `pressScale` / `pressDamping` / `pressStiffness` | 0.96 / 0.45 / 600 | 无（桌面用悬停高亮） | 无 |
| 快速轻点时最短按下时间 | `pressMinMillis` | 90 ms | — | — |
| 开关、勾选、分段、多选勾选框滑入、按钮文字切换 | `toggleDamping` / `toggleStiffness` | 0.62 / 700 | 系统控件、SF Symbols 替换动画 | `pop()`：0.34,1.4,0.64,1 回弹曲线 |
| 页面推入 / 退出 | `pageDurationMillis`，曲线 `Smooth` | 320 ms，cubic-bezier(0.2, 0, 0, 1) | 无页面栈 | `smooth` 曲线 |
| 下层页面视差 / 变暗 | `pageParallax` / `pageScrim` | 0.30 宽度 / 12 % 黑 | — | — |
| 右滑松手后归位（带手指速度） | `pageSettleStiffness`（阻尼 1） | 500 | — | — |
| 底部弹窗吸附 | `sheetDamping` / `sheetStiffness` | 0.86 / 450 | 系统 Sheet | `ContentDialog` |
| 松手时按速度预判位置 | `sheetProjection` | 0.15 s | — | — |
| 列表增删、重排 | `listDamping` / `listStiffness` | 0.85 / 500 | `.settle` | `enter()`：自上方 8 px 淡入 300 ms |
| 提示横幅 | `fadeIn()` / `fadeOut()` | 进 300 ms 自上方 8 dp 落下，出 200 ms 缩到 98 % | `.settle` | `settle()` |
| 工作区切换 | `fadeIn()` + 从 98 % 放大 | 300 ms | `blurReplace` | `crossfade()`：350 ms，98 % |
| 状态图标变化 | `StatusGlyph` | 从 60 % 放大；完成时回弹一次 | SF Symbols 弹跳 | `pop(bounce)` |
| 空态图标漂浮 | `EmptyState(drift = true)` | 上下 5 dp，单程 2.8 s | 唱片缓慢漂浮 | `Loop::drift`：5.6 s 往返 |
| 主题切换 | `themeRevealMillis` | 480 ms 圆形展开 | 跟随系统，无应用内切换 | 跟随系统 |

Telegram 式手感的具体表现：

- **页面栈**：在页面任意位置向右拖（子组件没有先拿走这个手势时），页面跟着手指走；下层页面同时从左侧
  三分之一处移回并逐渐变亮，上层页面左边缘有阴影。松手时带着手指速度弹簧归位：向右甩或拖过 40 % 就返回，否则回弹。
  系统返回键直接退出；Android 14+ 的系统返回手势（从屏幕边缘滑）会驱动同一个过渡，并且可以中途取消。
- **按压**：按钮和卡片按下时缩小到 96 %，松手时带一点过冲弹回；很快的轻点也至少显示 90 ms 的按下状态。
  列表行不缩放，而是铺一层浅灰。
- **底部弹窗**：拖把手、标题或弹窗里任何不滚动的地方都能拖；里面的列表滚到顶后继续下拉会带着弹窗走。
  松手时按速度预判位置，决定收起还是弹回；往上拖过头有橡皮筋阻力；遮罩深浅随弹窗位置实时变化。
- **顶栏**：列表往上滚时，大标题先折叠成顶栏里的小标题，再滚动列表；滚回顶部时大标题展开；停在一半会吸附。
  搜索图标点开后，搜索框从图标位置原地展开盖住标题，右边出现“取消”。
- **列表**：增加、删除、重排、筛选时，行平滑移动到新位置，新行淡入，删除的行淡出后空位合拢。
- **主题切换**：截下当前画面，新主题从点按的位置以圆形展开，盖过旧画面。

## 6. 组件清单：什么时候用哪个

| 需要 | 用 | 不要用 |
| --- | --- | --- |
| 页面顶部标题栏 | `MuTopBar`（列表页加 `state = rememberTopBarState()` 得到可折叠大标题） | Material `TopAppBar` |
| 顶栏右侧的图标按钮 | `IconAction` | `IconButton` |
| 顶栏的“⋯”菜单 | `OverflowMenu` + `MenuItem` | `DropdownMenu` |
| 一屏里最主要的一个操作 | `PrimaryButton`（一屏最多一个） | `Button` |
| 其他按钮 | `SecondaryButton` | `OutlinedButton` |
| 内容里的文字链接式操作（“查看全部”“全选”） | `PlainButton` | `TextButton` |
| 2–5 个互斥选项，横向放得下（筛选、标签页、格式） | `SegmentedControl` | `TabRow`、单选按钮 |
| 互斥选项多或文字长（命名方式、编码） | `OptionRow`（放在 `ListGroup` 里） | `RadioButton` |
| 开 / 关一个设置 | `SwitchRow`（单独的开关用 `MuSwitch`） | Material `Switch` |
| 多个独立的勾选项 | `CheckRow`（单独的勾选框用 `MuCheckbox`） | Material `Checkbox` |
| 进入下一页的设置行 | `NavigationRow` | — |
| 一组固定的行（设置、弹窗里的选项） | `ListGroup` + 行 + `RowDivider` | `Card` |
| 长的、会变化的列表（队列、搜索结果） | `LazyColumn` + `LazyGroupItem` + `ListRow` | — |
| 列表行 | `ListRow`（多选时 `selectionMode`，当前项 `current`，进行中 `highlighted`） | `ListItem` |
| 左滑移出 / 删除一行 | `SwipeToDeleteRow` 包住 `ListRow` | `SwipeToDismissBox` |
| 任务状态（待转换 / 转换中 / 已完成 / 未完成） | `StatusGlyph` + 状态文字 | 只用颜色 |
| 正在加载 / 扫描 | `MuSpinner` + 说明文字 | `CircularProgressIndicator` |
| 确定进度 | `MuProgressBar` | `LinearProgressIndicator` |
| 页面底部的状态 + 主操作 | `ActionFooter` | `BottomAppBar` |
| 不需要用户决定的提示（“已添加 2 个文件”、错误原因） | `NoticeBanner` | `Snackbar`、`Toast` |
| 空列表、筛选无结果 | `EmptyState` | — |
| 可撤销的选项、详情、设置 | `MuBottomSheet` | `ModalBottomSheet` |
| 不可撤销或会覆盖内容的操作前确认 | `ConfirmDialog` | `AlertDialog` |
| 搜索框（一直显示） | `SearchField` | `SearchBar` |
| 点图标才出现的搜索 | `MuTopBar(search = TopBarSearch(…))` | — |
| 设置里的文本输入、歌词编辑 | `MuTextField` | `TextField`、`OutlinedTextField` |
| 图标 | `MuIcons.X` | 直接 `Icons.Filled.X` |
| 可点的自定义区域（卡片、状态行） | `Modifier.pressable(onClick)` | `Modifier.clickable` |

`BottomNavigation` 只给 `AppShell` 用，页面里不要用。

## 7. 组件用法（可直接复制）

下面的代码默认已经有这些 import（按需删减）：

```kotlin
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.itemsIndexed
import androidx.compose.material3.Icon
import androidx.compose.material3.Text
import androidx.compose.runtime.*
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.input.nestedscroll.nestedScroll
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.unit.dp
import androidx.lifecycle.viewmodel.compose.viewModel
import io.github.backspace135.musicunlock.R
import io.github.backspace135.musicunlock.ui.kit.*
import io.github.backspace135.musicunlock.ui.nav.LocalNavigator
import io.github.backspace135.musicunlock.ui.nav.LocalPageActive
import io.github.backspace135.musicunlock.ui.nav.Route
import io.github.backspace135.musicunlock.ui.theme.MuMotion
import io.github.backspace135.musicunlock.ui.theme.MuShapes
import io.github.backspace135.musicunlock.ui.theme.MuSpacing
import io.github.backspace135.musicunlock.ui.theme.MuTheme
```

### 7.1 顶栏 `MuTopBar`

可折叠大标题（工作区首页、设置这类长列表页）：

```kotlin
val topBar = rememberTopBarState()
Column(Modifier.fillMaxSize()) {
    MuTopBar(
        title = stringResource(R.string.unlock_title),
        state = topBar,
        actions = {
            IconAction(MuIcons.AddFolder, stringResource(R.string.unlock_add_folder_description), onClick = vm::addFolder)
            IconAction(MuIcons.Add, stringResource(R.string.unlock_add_files_description), onClick = vm::addFiles)
            OverflowMenu(
                listOf(
                    MenuItem(stringResource(R.string.unlock_clear_finished_action), enabled = vm.finishedCount > 0, onClick = vm::clearFinished),
                    MenuItem(stringResource(R.string.unlock_clear_queue_action)) { confirmClear = true },
                ),
            )
        },
    )
    // nestedScroll 必须接在列表上，大标题才会随滚动折叠
    LazyColumn(Modifier.fillMaxSize().nestedScroll(topBar.nestedScrollConnection)) { … }
}
```

大标题下面加一行小字（例如歌词工作台的登录状态），用 `subtitle`：

```kotlin
MuTopBar(
    title = stringResource(R.string.lyrics_title),
    state = topBar,
    subtitle = {
        Row(Modifier.pressable({ navigator.push(Route.Accounts) }), verticalAlignment = Alignment.CenterVertically) {
            Icon(MuIcons.Globe, null, Modifier.size(14.dp), tint = MuTheme.colors.secondary)
            Text(stringResource(R.string.lyrics_login_public), style = MuTheme.type.caption, color = MuTheme.colors.secondary)
        }
    },
)
```

推入的子页面（有返回按钮，没有大标题）：

```kotlin
MuTopBar(
    title = song.title,
    onBack = navigator::pop,
    actions = { IconAction(MuIcons.Refresh, stringResource(R.string.lyrics_reload_description), { confirmReload = true }) },
)
```

点图标才展开的搜索：

```kotlin
var searching by rememberSaveable { mutableStateOf(false) }
MuTopBar(
    title = stringResource(R.string.lyrics_title),
    state = topBar,
    search = TopBarSearch(
        query = vm.query,
        onQueryChange = { vm.query = it },
        placeholder = stringResource(R.string.lyrics_search_placeholder),
        expanded = searching,
        onExpandedChange = { searching = it },
        onSearch = { vm.search() },   // 键盘上的“搜索”键
    ),
)
```

### 7.2 按钮

```kotlin
PrimaryButton(stringResource(R.string.unlock_start_action), onClick = vm::start, enabled = vm.pendingCount > 0)
SecondaryButton(stringResource(R.string.unlock_empty_pick_folder_action), onClick = pickFolder)
SecondaryButton(stringResource(R.string.unlock_find_lyrics_action), onClick = findLyrics, icon = MuIcons.Lyrics, modifier = Modifier.fillMaxWidth())
PlainButton(stringResource(R.string.unlock_filter_show_all_action), onClick = { vm.filter = QueueFilter.All })
IconAction(MuIcons.Settings, stringResource(R.string.nav_settings_description), onClick = { navigator.push(Route.Settings) })
```

- 底栏、行内这类空间小的地方加 `small = true`（高 40 dp）。
- **按钮文字变了会原地弹簧切换**，所以“开始转换 / 停止转换”“搜索 / 取消”只需要换 `text`，不要放两个按钮：

```kotlin
PrimaryButton(
    text = stringResource(if (vm.running) R.string.unlock_stop_action else R.string.unlock_start_action),
    onClick = { if (vm.running) vm.stop() else vm.start() },
)
```

- `IconAction` 的第二个参数是 TalkBack 读的说明，必须写，文案用 `_description` 结尾。

### 7.3 可点击的自定义区域 `Modifier.pressable`

```kotlin
Row(
    Modifier
        .padding(horizontal = MuSpacing.page)
        .pressable(onClick = { showExportSettings = true }, pressedScale = 0.98f)
        .padding(vertical = MuSpacing.s),
    verticalAlignment = Alignment.CenterVertically,
) {
    Text(stringResource(R.string.unlock_output_strip_format, folderName, namingName), style = MuTheme.type.caption, color = MuTheme.colors.secondary)
    Icon(MuIcons.Chevron, null, Modifier.size(16.dp), tint = MuTheme.colors.secondary)
}
```

`pressable` 要写在 `background` / `clip` 前面，整块才会一起缩放。大块区域用 `pressedScale = 0.98f`，小块用默认值。
需要长按时加 `onLongClick = { … }`。

### 7.4 分段控件 `SegmentedControl`

```kotlin
SegmentedControl(
    options = QueueFilter.entries.map { stringResource(it.label) },
    selectedIndex = vm.filter.ordinal,
    onSelect = { vm.filter = QueueFilter.entries[it] },
    modifier = Modifier.padding(horizontal = MuSpacing.page),
)
```

宽度占满父布局，各段等宽。超过 5 段或文字放不下时改用 `OptionRow`。

### 7.5 开关、勾选、单选

放在 `ListGroup` 里，行之间加 `RowDivider()`：

```kotlin
ListGroup(header = stringResource(R.string.settings_lyrics_header)) {
    CheckRow(stringResource(R.string.lyrics_include_original), checked = s.includeOriginal, onCheckedChange = vm::setIncludeOriginal)
    RowDivider(inset = 50.dp)   // 勾选框行：分隔线从文字开始
    CheckRow(stringResource(R.string.lyrics_include_translation), checked = s.includeTranslation, onCheckedChange = vm::setIncludeTranslation)
}
ListGroup(Modifier.padding(top = MuSpacing.l)) {
    SwitchRow(stringResource(R.string.lyrics_ignore_empty), checked = s.ignoreEmpty, onCheckedChange = vm::setIgnoreEmpty)
    RowDivider()
    SwitchRow(stringResource(R.string.lyrics_skip_instrumental), checked = s.skipInstrumental, onCheckedChange = vm::setSkipInstrumental, subtitle = "可选的说明")
}
ListGroup(header = "文件命名") {
    NamingPolicy.entries.forEachIndexed { index, policy ->
        if (index > 0) RowDivider()
        OptionRow(stringResource(policy.label), selected = vm.naming == policy, onClick = { vm.naming = policy })
    }
}
```

不在行里、单独使用时：`MuSwitch(checked, onCheckedChange = { … })`、`MuCheckbox(checked, onCheckedChange = { … })`。

### 7.6 分组和设置行

```kotlin
ListGroup(header = stringResource(R.string.settings_output_header), footer = stringResource(R.string.unlock_status_detail)) {
    NavigationRow(stringResource(R.string.settings_output_folder), onClick = { … }, value = folderName)
    RowDivider()
    NavigationRow(stringResource(R.string.settings_output_naming), onClick = { … }, value = namingName)
}
ListGroup(header = stringResource(R.string.settings_accounts_header)) {
    NavigationRow(stringResource(R.string.settings_accounts_entry), onClick = { navigator.push(Route.Accounts) }, icon = MuIcons.Account, value = status)
}
```

`ListGroup` 自带页面左右边距，直接放在页面的 `Column` 或 `LazyColumn` 的 `item {}` 里。只要一个小标题时用
`SectionHeader("标题")`。

### 7.7 长列表：`LazyColumn` + `LazyGroupItem` + `ListRow`

```kotlin
LazyColumn(
    Modifier.fillMaxSize().nestedScroll(topBar.nestedScrollConnection),
    contentPadding = PaddingValues(bottom = 112.dp),   // 有 ActionFooter 时留出底栏的高度
) {
    item(key = "filters") { /* 筛选、计数等 */ }
    itemsIndexed(jobs, key = { _, job -> job.id }) { index, job ->   // key 必须是稳定的唯一 id
        LazyGroupItem(
            index = index,
            count = jobs.size,
            modifier = Modifier.animateItem(
                fadeInSpec = MuMotion.fadeIn(),
                placementSpec = MuMotion.list(),
                fadeOutSpec = MuMotion.fadeOut(),
            ),
            dividerInset = 64.dp,   // 行左边有 36 dp 封面时，分隔线从文字开始
        ) {
            ListRow(
                title = job.title,
                subtitle = job.subtitle,
                highlighted = job.state == JobState.Running,   // 转换中：整行浅灰
                onClick = { openJobId = job.id },
                leading = { Cover(job) },
                extra = job.detail?.let { { Text(it, style = MuTheme.type.caption) } },   // 第三行，例如失败原因
                trailing = {
                    Column(horizontalAlignment = Alignment.End) {
                        Text(job.format, style = MuTheme.type.caption, color = MuTheme.colors.secondary)
                        Row(verticalAlignment = Alignment.CenterVertically) {
                            StatusGlyph(job.glyph, contentDescription = null, size = 16.dp)
                            Text(job.stateLabel, style = MuTheme.type.caption, modifier = Modifier.padding(start = MuSpacing.xs))
                        }
                    }
                },
            )
        }
    }
}
```

- 列表平滑增删、重排全靠 `key` + `Modifier.animateItem(…)`，**两者缺一个就没有动画**。
- 多选：`ListRow(…, selectionMode = vm.selectionMode, selected = id in vm.checked, onClick = { vm.toggle(id) }, onLongClick = { vm.beginSelection(id) })`，
  勾选框会弹簧滑入。需要一直显示勾选框时，`selectionMode = true`。
- 当前正在编辑的项（左边深色竖线 + 浅灰底）：`current = id == vm.currentId`。
- 行左边的小图标用 `RowIcon(MuIcons.X)`；36×40 封面这类自定义内容直接放进 `leading`。
- 标题要显示两行：`titleMaxLines = 2`。

### 7.8 左滑移出 `SwipeToDeleteRow`

```kotlin
LazyGroupItem(index, jobs.size, Modifier.animateItem(fadeInSpec = MuMotion.fadeIn(), placementSpec = MuMotion.list(), fadeOutSpec = MuMotion.fadeOut())) {
    SwipeToDeleteRow(
        onDelete = { vm.remove(job.id) },                 // 必须把这一项从列表里删掉
        label = stringResource(R.string.unlock_remove_action),   // “移出”；默认是“删除”
        enabled = job.state != JobState.Running,          // 转换中的行不能滑
    ) {
        ListRow(title = job.title, subtitle = job.subtitle, onClick = { … })
    }
}
```

向左拖过操作区再松手（或快速左甩）就移出；没拖够会弹回。向右滑不受影响（页面仍可右滑返回）。
TalkBack 用户可以在行的操作菜单里找到同一个操作。行放在别的底色上时传 `background = MuTheme.colors.background`。

### 7.9 状态图标、转圈、进度条

```kotlin
StatusGlyph(JobGlyph.Pending, contentDescription = null)   // 虚线圆
StatusGlyph(JobGlyph.Running, contentDescription = null)   // 转圈
StatusGlyph(JobGlyph.Done, contentDescription = null)      // 实心圆加对勾；变成 Done 时弹跳一次
StatusGlyph(JobGlyph.Failed, contentDescription = null)    // 圆圈加感叹号

Row(verticalAlignment = Alignment.CenterVertically) {
    MuSpinner(size = 16.dp)
    Text(stringResource(R.string.unlock_scanning), style = MuTheme.type.caption, modifier = Modifier.padding(start = MuSpacing.s))
}

MuProgressBar(progress = finished / total.coerceAtLeast(1).toFloat())
```

**状态图标旁边必须同时显示状态文字**（例如核心给的 `state_label`），图标本身不读给 TalkBack（`contentDescription = null`）。

### 7.10 底栏 `ActionFooter`

页面根部用 `Box`，列表在下面，底栏对齐底部：

```kotlin
Box(Modifier.fillMaxSize()) {
    Column(Modifier.fillMaxSize()) {
        MuTopBar(…)
        LazyColumn(…, contentPadding = PaddingValues(bottom = 112.dp)) { … }
    }
    ActionFooter(
        title = vm.statusTitle,                        // 左上：例如“正在转换 2 / 5”
        detail = vm.statusDetail,                      // 左下：说明文字
        progress = if (vm.running) vm.progress else null,   // 不为 null 时，说明文字换成进度条
        actionLabel = stringResource(if (vm.running) R.string.unlock_stop_action else R.string.unlock_start_action),
        onAction = { if (vm.running) vm.stop() else vm.start() },
        actionEnabled = vm.canStart || vm.running,
        secondaryLabel = if (vm.failedCount > 0 && !vm.running) stringResource(R.string.unlock_retry_action) else null,
        onSecondary = vm::retryFailed,
        visible = vm.jobs.isNotEmpty(),                // false 时从底部滑出
        modifier = Modifier.align(Alignment.BottomCenter),
    )
}
```

- 左边要放别的东西（例如歌词的格式选择）时用 `leading = { SegmentedControl(listOf("LRC", "SRT", "TXT", "VTT"), vm.format, { vm.format = it }) }`，
  这时 `title`、`detail` 不显示。
- 次按钮出现时从主按钮旁边分离出来，消失时收回。
- 工作区页面的底栏在底部导航上面，不用处理系统导航栏；推入的子页面（没有底部导航）要加
  `Modifier.align(Alignment.BottomCenter).navigationBarsPadding()`。

### 7.11 提示横幅 `NoticeBanner`

```kotlin
Column(Modifier.fillMaxSize()) {
    MuTopBar(…)
    NoticeBanner(text = vm.notice, onDismiss = vm::dismissNotice)   // text 为 null 或空字符串时隐藏
    LazyColumn(…) { … }
}
```

**横幅放在顶栏和列表之间，不要放进 `LazyColumn` 的第一项**：列表顶部插入的内容会出现在视野上方，用户看不到。
`onDismiss = null` 时没有关闭按钮。换图标：`icon = MuIcons.Warning`。

### 7.12 空态 `EmptyState`

```kotlin
EmptyState(
    title = stringResource(R.string.unlock_empty_title),
    body = stringResource(R.string.unlock_empty_body),
    icon = MuIcons.MusicNote,
    drift = true,                      // 图标缓慢上下漂浮
    modifier = Modifier.padding(top = MuSpacing.xl),
) {
    PrimaryButton(stringResource(R.string.unlock_empty_pick_files_action), onClick = pickFiles)
    Spacer(Modifier.height(MuSpacing.s))
    SecondaryButton(stringResource(R.string.unlock_empty_pick_folder_action), onClick = pickFolder)
}
```

筛选无结果这类小空态不要 `drift`，按钮用 `PlainButton`。要显示 App 图标等自定义图片时传 `artwork = { Image(…) }` 代替 `icon`。

### 7.13 底部弹窗 `MuBottomSheet`

```kotlin
var openJobId by rememberSaveable { mutableStateOf<Long?>(null) }
val openJob = vm.jobs.firstOrNull { it.id == openJobId }

MuBottomSheet(
    visible = openJob != null,
    onDismissRequest = { openJobId = null },     // 必须把 visible 变成 false
    title = openJob?.title,
) {
    if (openJob != null) {
        Text(openJob.detail, style = MuTheme.type.subtitle, modifier = Modifier.padding(horizontal = MuSpacing.page + MuSpacing.xs))
        Column(Modifier.fillMaxWidth().padding(MuSpacing.page), verticalArrangement = Arrangement.spacedBy(MuSpacing.s)) {
            SecondaryButton("分享", onClick = { … }, modifier = Modifier.fillMaxWidth())
            SecondaryButton(stringResource(R.string.unlock_remove_from_queue_action), onClick = { openJobId = null; vm.remove(openJob.id) }, modifier = Modifier.fillMaxWidth())
        }
    }
}
```

- 弹窗可以写在页面函数里的任何位置，它会显示在整个 App 最上层（盖住底部导航）。
- 内容是 `ColumnScope`，从上到下排。内容长时用 `Column(Modifier.verticalScroll(rememberScrollState()))` 或 `LazyColumn`，
  滚到顶后继续下拉会带着弹窗走。
- 弹窗底色和页面一样，所以里面可以直接放 `ListGroup`。
- 点遮罩、下拉、系统返回都会调用 `onDismissRequest`。

### 7.14 确认对话框 `ConfirmDialog`

```kotlin
var confirmClear by rememberSaveable { mutableStateOf(false) }

ConfirmDialog(
    visible = confirmClear,
    title = stringResource(R.string.unlock_clear_queue_title),
    message = stringResource(R.string.unlock_clear_queue_body),
    confirmLabel = stringResource(R.string.unlock_clear_queue_action),
    onConfirm = {
        confirmClear = false          // 两个回调都要关掉对话框
        vm.clearQueue()
    },
    onDismissRequest = { confirmClear = false },
    icon = MuIcons.Warning,           // 危险操作加警告图标，不要用颜色表示危险
)
```

只在“不能撤销”或“会覆盖用户内容”时用。其他提示用 `NoticeBanner`。

### 7.15 “⋯”菜单 `OverflowMenu`

```kotlin
OverflowMenu(
    listOf(
        MenuItem(stringResource(R.string.lyrics_open_local_action)) { openLocal() },
        MenuItem(stringResource(R.string.lyrics_export_settings_action)) { showExportSettings = true },
        MenuItem("复制歌词", enabled = vm.preview.isNotEmpty(), icon = MuIcons.Copy) { copy() },
    ),
)
```

放在 `MuTopBar` 的 `actions` 里。选中一项后菜单自动关闭再执行。

### 7.16 输入框

```kotlin
SearchField(
    query = vm.query,
    onQueryChange = { vm.query = it },
    placeholder = stringResource(R.string.lyrics_search_placeholder),
    onSearch = vm::search,
    modifier = Modifier.fillMaxWidth().padding(horizontal = MuSpacing.page),
)

MuTextField(
    value = vm.filenameTemplate,
    onValueChange = vm::setFilenameTemplate,
    label = "文件名",
    caption = "支持 \${name}、\${singer}、\${album}、\${id}、\${index}。同名文件自动编号。",
)

// 歌词编辑：多行、等宽
MuTextField(value = text, onValueChange = { text = it }, singleLine = false, monospace = true, readOnly = vm.exporting)

// 数字
MuTextField(
    value = offsetText,
    onValueChange = { offsetText = it },
    label = "时间偏移（毫秒）",
    keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Number),
)
```

`MuTextField` 默认换行；歌词编辑区需要“不自动换行、横向滚动、关掉拼写检查和联想”时（#32），在
`MuTextField` 外层加 `Modifier.horizontalScroll(rememberScrollState())`，并传
`keyboardOptions = KeyboardOptions(autoCorrectEnabled = false, keyboardType = KeyboardType.Ascii)`。

### 7.17 图标 `MuIcons`

已有：`Back`、`Chevron`、`Add`、`AddFolder`、`Check`、`Close`、`More`、`Refresh`、`Search`、`Share`、`Account`、
`Delete`、`Info`、`Lock`、`Settings`、`Warning`、`MusicNote`、`Folder`、`Lyrics`、`Shield`、`Globe`、`Album`、
`Playlist`、`Copy`。

缺图标时在 `ui/kit/MuIcons.kt` 里加（这是 #30 的文件里唯一允许追加的地方）：先找 material-icons-core 里有没有
（`Icons.Outlined.X` / `Icons.Filled.X`），没有就从 <https://fonts.google.com/icons> 复制 24×24 的 path 数据，照文件里
`material("名字", "path")` 的写法加一项。

### 7.18 主题

页面里不需要处理深浅色，组件和 token 会自动跟随。要读当前设置或改主题：

```kotlin
val theme = LocalThemeController.current
theme.mode                                          // ThemeMode.System / Light / Dark
theme.setMode(ThemeMode.Dark, origin = Offset(x, y))   // origin：圆形展开的中心（根坐标），可省略
```

## 8. 整页模板

一个带可折叠标题、横幅、筛选、可左滑列表、空态、底栏、详情弹窗和确认框的工作区页面。新页面从这里复制，再删掉用不到的部分。
完整的可运行版本见 `ui/unlock/UnlockScreen.kt`。

```kotlin
@Composable
fun ExampleScreen(vm: ExampleViewModel = viewModel()) {
    val navigator = LocalNavigator.current
    val topBar = rememberTopBarState()
    var openId by rememberSaveable { mutableStateOf<Long?>(null) }
    var confirmClear by rememberSaveable { mutableStateOf(false) }
    val items = vm.visibleItems

    Box(Modifier.fillMaxSize()) {
        Column(Modifier.fillMaxSize()) {
            MuTopBar(
                title = stringResource(R.string.unlock_title),
                state = topBar,
                actions = {
                    IconAction(MuIcons.Add, stringResource(R.string.unlock_add_files_description), onClick = vm::add)
                    OverflowMenu(listOf(MenuItem(stringResource(R.string.unlock_clear_queue_action)) { confirmClear = true }))
                },
            )
            NoticeBanner(vm.notice, onDismiss = vm::dismissNotice)
            LazyColumn(
                Modifier.fillMaxSize().nestedScroll(topBar.nestedScrollConnection),
                contentPadding = PaddingValues(bottom = 112.dp),
            ) {
                item(key = "filters") {
                    SegmentedControl(
                        options = vm.filterLabels,
                        selectedIndex = vm.filterIndex,
                        onSelect = vm::selectFilter,
                        modifier = Modifier.padding(horizontal = MuSpacing.page, vertical = MuSpacing.s),
                    )
                }
                if (items.isEmpty()) {
                    item(key = "empty") {
                        EmptyState(title = stringResource(R.string.unlock_empty_title), icon = MuIcons.MusicNote, drift = true) {
                            PrimaryButton(stringResource(R.string.unlock_empty_pick_files_action), onClick = vm::add)
                        }
                    }
                } else {
                    itemsIndexed(items, key = { _, item -> item.id }) { index, item ->
                        LazyGroupItem(
                            index,
                            items.size,
                            Modifier.animateItem(fadeInSpec = MuMotion.fadeIn(), placementSpec = MuMotion.list(), fadeOutSpec = MuMotion.fadeOut()),
                        ) {
                            SwipeToDeleteRow(onDelete = { vm.remove(item.id) }) {
                                ListRow(title = item.title, subtitle = item.subtitle, onClick = { openId = item.id })
                            }
                        }
                    }
                }
            }
        }
        ActionFooter(
            title = vm.statusTitle,
            detail = vm.statusDetail,
            progress = vm.progress,
            actionLabel = vm.actionLabel,
            onAction = vm::primaryAction,
            visible = items.isNotEmpty(),
            modifier = Modifier.align(Alignment.BottomCenter),
        )
    }

    val open = vm.items.firstOrNull { it.id == openId }
    MuBottomSheet(visible = open != null, onDismissRequest = { openId = null }, title = open?.title) {
        // 详情内容
    }
    ConfirmDialog(
        visible = confirmClear,
        title = stringResource(R.string.unlock_clear_queue_title),
        confirmLabel = stringResource(R.string.unlock_clear_queue_action),
        onConfirm = { confirmClear = false; vm.clear() },
        onDismissRequest = { confirmClear = false },
        icon = MuIcons.Warning,
    )
}
```

推入的子页面模板（设置类页面）：

```kotlin
@Composable
fun ExampleDetailScreen(id: Long) {
    val navigator = LocalNavigator.current
    Column(Modifier.fillMaxSize()) {
        MuTopBar(title = "标题", onBack = navigator::pop)
        Column(Modifier.verticalScroll(rememberScrollState())) {
            ListGroup(header = "分组") {
                NavigationRow("一项", onClick = { })
                RowDivider()
                SwitchRow("开关", checked = true, onCheckedChange = { })
            }
            Spacer(Modifier.windowInsetsBottomHeight(WindowInsets.navigationBars))   // 留出系统导航栏
        }
    }
}
```

## 9. 常见错误

1. **状态只用颜色区分**：不允许。状态要有图标和文字（`StatusGlyph` + 文字）、选中要有形状或字重变化
   （组件已经做好，自己写的东西也要遵守）。
2. **`LazyColumn` 的 `items` 没写 `key` 或没加 `animateItem`**：增删时不会平滑移动。
3. **横幅放进了 `LazyColumn`**：会被滚到视野外。放在顶栏和列表之间。
4. **`SwipeToDeleteRow` 的 `onDelete` 没有真正删除这一项**：行会滑出后停在那里。
5. **弹窗、对话框的回调里没有把 `visible` 设成 `false`**：弹窗会弹回来。
6. **页面里的 `BackHandler` 没加 `LocalPageActive.current`**：被盖住的页面会抢走返回键。
7. **把对象或回调放进 `Route`**：App 被杀后恢复时会崩溃。路由参数只放 id、字符串、数字、枚举。
8. **直接写 `Color(0xFF…)`、`16.dp` 这类字面量**：用 `MuTheme.colors`、`MuSpacing`。
9. **直接 import `androidx.compose.material.icons.Icons`**：用 `MuIcons`，缺的按 7.17 补。
10. **用 Material 3 的按钮、开关、弹窗**：样式和手感都不对，用第 6 节表格里的组件。
11. **文字写死在代码里**：放进 `strings.xml`（第 3 节）。
12. **在 `remember` 里放页面状态**：横竖屏切换、App 被杀后会丢。页面上的开关状态用 `rememberSaveable`，
    数据放 ViewModel。
13. **列表盖在底栏下面**：列表加 `contentPadding = PaddingValues(bottom = 112.dp)`。

## 10. 调参页与验证

**调参页**：debug 版的“设置 → 开发 → 组件演示与动画调参”。每个组件旁边有 `MuMotion` 对应参数的滑块，拖动后立即生效，
重启后恢复默认；“⋯ → 恢复默认参数”可以一键还原。页面底部的“当前参数”可以直接抄回 `Motion.kt` 和本文第 5 节。
release 版没有这个入口（`app/src/release/` 里是空实现）。

**构建和运行**（在仓库根目录）：

```sh
python3 apps/android/scripts/build.py
adb -s emulator-5554 install -r apps/android/build/app/outputs/apk/debug/app-debug.apk
adb -s emulator-5554 shell am start -n io.github.backspace135.musicunlock/.MainActivity
```

`adb` 一律用 `-s` 指定设备。单元测试：在 `apps/android` 下设置好 `JAVA_HOME` 和 `ANDROID_HOME` 后运行
`./gradlew :app:testDebugUnitTest`。

**#30 的验收记录**（模拟器 `mu-ui`，Pixel 7 外形，Android 37 arm64，宿主 GPU）：

- 每种交互都录了浅色和深色的屏幕录像，路径写在 PR 里，只保存在本地。
- 掉帧检查：同一组操作（列表滚动、推入并右滑返回页面、切换工作区、切换筛选）下，release 版
  `dumpsys gfxinfo` 的 janky 帧为 5.0 %（99 分位 22 ms），作为对照，系统“设置”App 在同一模拟器上滚动为 15.9 %。
  debug 版因为没有 R8 优化，janky 帧为 8.9 %，属正常。
- 跟手程度、高刷新率屏幕下的流畅度、系统返回手势的真实手感，模拟器只能近似，留到 #34 的真机验收统一检查。
