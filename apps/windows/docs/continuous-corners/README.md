# #61 连续圆角验证记录

验证日期：2026-11-07。基线为 `main@23622a6`，本分支已 rebase，保留 #69 按钮结构、按压行为和 #70 单块歌词高亮。

## 验证结果

- `python scripts/build.py`：launcher、ui_math、core 共 3/3，通过 CLI 和启动器冒烟测试。
- `python scripts/build_app.py`：通过。
- `python scripts/screenshot.py`：退出码 0，应用退出码 `0x00000000`；模拟 QQ 登录、重开、退出和数据目录隔离检查通过。
- `python scripts/pack.py`：通过，未增加 NuGet 包。相同工具链和打包参数下，基线单 exe 19,476,480 字节，本分支 19,474,944 字节，差值 −1,536 字节。压缩结果受代码布局影响，不代表原始代码体积减少。
- `a11y.txt` 和 `tab-order.txt` 与 `baseline/` 对应文件逐字节相同。这只覆盖脚本的队列场景，不代替完整键盘与讲述人操作测试。

## 预览

以下图片均由项目 `screenshot.py` 生成，不是桌面抓屏。

| 场景 | 本分支 |
| --- | --- |
| 浅色 | [queue-light.png](queue-light.png) |
| 深色 | [queue-dark.png](queue-dark.png) |
| 高对比度 | [contrast-queue.png](contrast-queue.png) |
| 200% 文字 | [text200-queue-light.png](text200-queue-light.png) |
| 歌词高亮 | [lyrics-editor-light.png](lyrics-editor-light.png) |
| 连续虚线 | [empty-light.png](empty-light.png) |
| 二维码遮罩 | [accounts-qr-expired-light.png](accounts-qr-expired-light.png) |

圆角描边不再横穿内容；底栏与输出按钮轮廓完整，虚线沿连续路径绘制，二维码白底与内缩 1 的遮罩保持 12/11 的同心关系。

### 基线已有的问题

- 深色次按钮仍为浅底浅字：[基线](baseline/queue-dark.png)。#57 已有 #63 关于 GlassSurface 主题解析的协调留言，本分支不改变材质或画刷解析。
- 高对比度场景工作区上移，200% 文字场景标题栏与工作区文字重叠：[高对比度基线](baseline/contrast-queue.png)、[200% 基线](baseline/text200-queue-light.png)。基线也存在，不能宣称整体现有布局已通过验收。
- 本次没有新增动画，也未验证 #70 动效；静态预览不能证明动画的正确性。

## 路径开销与原生集成检查

在 `apps/windows` 下运行：

```powershell
python docs/continuous-corners/measure_corners.py
```

脚本从生产 `Corners.cpp` 提取 `factory/build` 函数，并链接实际的 `ContinuousPath.cpp`，使用 MSVC x64 `/O2` 编译；所有生成物放在 `build/tmp/`。
原生集成检查覆盖矩形、小半径、连续角、胶囊的边界、四边中点与中心填充，以及半径小于内缩量的粗描边中心线。

预热 1,000 次后对变化宽高的 10,000 次 Direct2D 路径构建计时，半径 20：

```text
D2D integration: bounds, edge connections, center fill, small-radius inset passed
D2D path rebuild: n=10000 mean=0.608 us p50=0.600 us p95=0.700 us p99=0.900 us
```

这是本机单次运行的 CPU 微基准，仅含控制点计算和 Direct2D 几何创建，不含 Composition 包装、提交、XAML 布局或 GPU。
`clip` 对相同尺寸和半径复用；附加属性布局回调仍有属性读取开销。**实际拖窗帧耗时与主观流畅度未测，不能据此声称拖窗无卡顿。**

## 剩余边界

自动描边仅针对本次接入的单色、等宽描边，暂不支持渐变和不等宽描边；线宽与 Padding 在首次应用时替换，后续动态修改这些属性不在当前契约内。
尚未做 Windows 10 干净系统运行、完整键盘操作或独立上下文代码评审；CI 结果以 PR 检查为准。PR 保持草稿，实际拖窗验证待补，不自行合并。
