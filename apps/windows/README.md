# 音匣 Windows

Windows 原生版本的开发目录，与 macOS 版本在同一个 Git 仓库维护。

**当前状态：解封装核心、命令行版、歌词核心、WinUI 3 音乐解锁与歌词工作区、账号登录与单 exe 启动器已实现。**

## 技术方向

- C++20 + C++/WinRT + WinUI 3 / XAML，使用原生界面；仅 x64，支持 Windows 10 1809+ 与 Windows 11。
- 沿用 macOS 版的品牌色、布局与双工作区；使用 Windows 系统材质（Mica / Acrylic）、控件和交互。
- 原生音频解封装引擎，使用 `../../tests/fixtures/qmc/` 中的相同测试向量验证；公共 KGM 数据位于 `../../resources/kgm.mask`。
- 歌词搜索、编辑与导出，优先使用原生扫码授权和 Windows 系统凭据保护。
- 自包含分发：最终产物是单个 exe。外层小型启动器内嵌压缩后的 WinUI 3 程序目录（系统 Compression API），
  首次运行解压到 `%LOCALAPPDATA%\MusicUnlock\runtime\<版本哈希>\` 并校验，用户无需安装 .NET 或 Windows App Runtime。
- 网易云用原生二维码登录；QQ 音乐没有原生扫码接口，用 WebView2 打开官方登录页，读取 Cookie 后交给 core 判断会话。
  WebView2 拿到的 QQ 会话能否直接用于歌词请求，需要用真实账号验证，不能假定浏览器授权自动提供 App 会话。

## 目录

C++ 核心与它的测试、第三方库放在仓库根目录的 `shared/core/`，与 Android 版共用：

```text
shared/core/
├── include/       公共头文件 musicunlock/*.h
├── src/           纯 C++20 静态库：解密、标签、命名、歌词解析与导出、登录会话、网易云 / QQ 音乐接口
│   └── platform/  平台实现：win/（CNG、DPAPI、WinHTTP、系统 ICU）、apple/（Mac 开发机：CommonCrypto、CoreFoundation）、
│                  portable/（纯 C++ AES-128-ECB 和文字转换占位，Android 与 Linux CI 使用；CMake 选项 MU_PORTABLE_AES 让其他平台也换用这个 AES）
├── tests/         核心、账号、导入、队列、歌词与歌词工作区测试，读取 ../../tests/fixtures/qmc
├── third_party/   nlohmann/json 3.12.0（单头文件，MIT）、nayuki QR-Code-generator 1.8.0（MIT）
└── CMakeLists.txt musicunlock_core 与 musicunlock_core_tests，按平台选择 src/platform/ 下的实现
```

`CMakeLists.txt` 用 `add_subdirectory` 引入核心；界面工程只编译 `src/*.cpp` 和 `src/platform/win/`。

```text
apps/windows/
├── cli/           musicunlock.exe，用法与 macOS CLI 相同；静态 CRT，kgm.mask 以资源内嵌
├── app/           WinUI 3 界面（C++/WinRT，未打包、Windows App SDK 自包含、静态 CRT）
├── launcher/      单 exe 启动器（纯 Win32、静态 CRT）与打包工具 launcher_pack
├── tools/         desktop_shot.cpp：在独立桌面上运行界面预览渲染
├── tests/         启动器测试与 smoke_launcher.py 用的替身程序 launcher_probe
└── scripts/       build.py：核心、命令行与启动器；smoke_cli.py / smoke_launcher.py：冒烟测试；
                   restore.py / build_app.py：界面；stage_app.py：整理分发目录；pack.py：生成单 exe；
                   screenshot.py：界面预览与端到端转换检查；remote.py：在 Windows 机器的独立工作树里构建
```

## 构建核心与命令行版

Windows 需要 Visual Studio 2022 Build Tools（“使用 C++ 的桌面开发”，含 MSVC x64、Windows SDK 与 CMake）和 Python 3：

```sh
python scripts/build.py              # Release，编译后运行核心与启动器测试、命令行与启动器冒烟测试
python scripts/build.py --config Debug --no-test
```

脚本通过 vswhere 定位 MSVC，使用 VS 自带的 CMake 与 Ninja。输出、缓存和临时文件都在 `build/` 下，
产物为 `build/windows-release/musicunlock.exe`，单文件即可运行：

```bat
musicunlock.exe D:\输出目录 D:\音乐\歌曲.ncm D:\另一个音乐目录
```

开发 Mac 上同一命令可编译核心与测试，用于快速验证算法（需要 CMake 与 Ninja）。

GitHub Actions（`.github/workflows/windows.yml`）在 windows-2022 上执行同一脚本、编译界面，上传 `musicunlock.exe`、
单文件的 `MusicUnlock.exe`、精简后的界面目录和单独的符号文件；
手动触发时额外运行联网的平台接口检查。

## 构建界面

同样只需要 Build Tools 的 C++ 工作负载，不需要 Visual Studio IDE 或 UWP 组件：

```sh
python scripts/build_app.py          # 恢复 NuGet 包并用 MSBuild 编译，产物在 build/app-release/
python scripts/screenshot.py         # 两个工作区、弹出层、大字体、高对比度与账号登录对话框的预览，输出到 build/previews/
python scripts/screenshot.py --convert   # 另外导入并实际转换示例文件，逐字节核对输出
python scripts/screenshot.py --motion all            # 记录全部动效场景，输出到 build/previews/motion/
python scripts/screenshot.py --motion workspace,drop  # 只记录列出的场景
python scripts/stage_app.py [--zip]  # 整理分发目录 build/dist/MusicUnlock/，--zip 另外打包并报告体积
python scripts/screenshot.py --dist [--convert]   # 用分发目录里的 App 渲染预览
```

- `restore.py` 按 `app/packages.config` 从 nuget.org 下载固定版本并校验 SHA-512，解压到 `build/packages/`。
  只引用界面用到的 Windows App SDK 组件，不用包含 AI、ML、Widgets、Search 的元包。
- 工程是普通桌面 C++ 项目：VC 只在 “Windows Store” 应用类型里调用 XAML 编译器，所以 `MusicUnlock.vcxproj`
  自己在 MIDL 前后运行两遍 WinUI 标记编译。
- 预览由 App 自己用 `RenderTargetBitmap` 渲染：`screenshot.py` 通过一次性计划任务（用 XML 定义注册，
  因为 `schtasks /TR` 限制命令行在 261 个字符以内）在登录会话里启动没有控制台窗口的 `desktop_shot.exe`，
  它新建独立桌面运行 App，结束后关闭桌面并删除计划任务，不显示也不截取用户桌面。
  渲染时 `MUSICUNLOCK_DATA_DIR` 指向输出目录，设置、会话和 WebView2 数据都不会写入用户的 AppData。
- 音乐解锁工作区的预览除了空队列和示例队列（`empty-*`、`queue-*`），还有拖入覆盖层（`drop-light`）、
  键盘焦点与悬停（`keyboard-light`）、行右键菜单、关于和关闭确认对话框（`menu-light`、`about-light`、`close-light`）、
  200 % 文字（`text200-*`）和高对比度（`contrast-*`），以及 UI 自动化树 `a11y.txt`（讲述人读到的控件类型、名称和状态）
  和 Tab 顺序 `tab-order.txt`。文字大小不改动机器的系统设置，由 App 模拟：每个 TextBlock 的字号乘 2，列宽按同一倍数加宽。
- 账号登录对话框的各状态（二维码等待、已扫码、已过期、已确认、被平台拒绝，QQ 登录页，会话已保存）有浅色、深色和高对比度三套预览。
  高对比度是系统设置，App 不能自己打开，所以预览换用 `Theme.xaml` 的 HighContrast 画刷模拟（WinUI 自带控件仍是浅色样式）；
  对话框和菜单在窗口之外的弹出层里，预览把它们叠加到窗口画面上。渲染不加载任何登录页，只启动一次 WebView2，检查运行时能从
  App 目录加载、数据目录在 `build/previews/appdata/WebView2/` 下。
- 之后模拟一次 QQ 音乐登录（`login-qq-*`）：从“账号登录”打开对话框、切到 QQ 音乐页签，官方登录页换成空白页并写入一组固定的
  假 Cookie，走和真实登录相同的读取 Cookie、加密保存、关闭登录页的流程；再关闭、重新打开对话框并退出登录。每一步写进
  `app.log`，`screenshot.py` 检查全部成功且 App 正常退出（#21 的闪退就是在这里复现的）。
- App 崩溃时（访问冲突等 XAML 收不到的异常）把异常代码、出错模块和调用栈（`模块+偏移`，可对照 PDB）追加到
  `MUSICUNLOCK_LOG`；没设这个变量时写到数据目录的 `crash.log`（`%LOCALAPPDATA%\MusicUnlock\crash.log`）。
- 预览是静态的，和 macOS 的 PreviewRenderer 一样不播放动效；`--convert` 的端到端转换保留动效，顺带检查动效代码。
- `--motion <场景>` 记录动效而不是截图。独立桌面上 `Windows.Graphics.Capture` 拒绝窗口
  （`CreateForWindow` 返回 `E_INVALIDARG`，窗口不在输入桌面上），`RenderTargetBitmap.RenderAsync` 也永远不返回，
  而切到输入桌面或抓屏幕都不允许，所以每个场景写一份日志 `build/previews/motion/<场景>-<浅色|深色>.log`：
  场景做了什么、每个动画的目标元素、变化的属性、起止值、曲线名和时长。场景名用逗号分隔，`all` 跑全部。
  自带 `workspace`（Ctrl+2 再 Ctrl+1 切换工作区）、`notice`（提示条出现和关闭）、`drop`（拖入覆盖层出现和消失）。
  新场景在 `motion_capture::add` 里注册，动作用 `Driver` 的 `click`、`press`、`release`、`drag`、`key`、`wait`、`capture`；
  动画开始前用 `motion::describe` 给元素起名，改 `Visibility` 后调用 `motion::changed`，日志才会记到隐式动画。

## 分发目录

`stage_app.py` 把 `build/app-release/` 复制到 `build/dist/MusicUnlock/`，单 exe 启动器打包的就是这个目录。
默认全部复制，只去掉下面几类运行时不会加载的文件（规则在脚本开头，新加的运行时文件如 `WebView2Loader.dll`
不用改脚本就会带上；某个被去掉的文件确实需要时，把文件名加进 `KEEP`）：

| 去掉的文件 | 为什么可以去掉 |
| --- | --- |
| 除 `zh-CN`、`zh-TW`、`en-US` 外的语言文件夹 | 只有 `Microsoft.ui.xaml.dll` 等的 `.mui` 字符串；界面是中文，`en-US` 是系统找不到语言时的回退 |
| `.winmd` | WinRT 元数据，只在编译时给 C++/WinRT 和 XAML 编译器用；运行时激活靠清单和 `XamlTypeInfo` |
| `.lib`、`.exp`（以及 `.ilk`、`.iobj`、`.ipdb`、`.map`） | 链接器的中间产物 |
| `.pdb` | 移到 `build/dist/symbols/`，分析崩溃时用 |

脚本最后检查 `MusicUnlock.exe`、`MusicUnlock.pri`、WinUI 与 Windows App SDK 的 DLL、`kgm.mask`、`LICENSE`、
`THIRD_PARTY_NOTICES.txt` 都在。`app/packages.config` 不包含 `Microsoft.WindowsAppSDK.Runtime`：那个包是给依赖框架的应用
安装 Windows App Runtime 的 MSIX（172 MB），自包含部署的文件来自各组件包。
引入新的运行时依赖（例如 WebView2）后，用 `screenshot.py --dist --convert` 确认分发目录仍能独立运行。

| | 目录 | zip（deflate 9） |
| --- | --- | --- |
| `build/app-release/` | 143.1 MB | 39.3 MB |
| `build/dist/MusicUnlock/` | 60.4 MB | 21.7 MB |
| `build/dist/symbols/` | 76.7 MB | |

## 单 exe

最终交付的是单个 `build/dist/MusicUnlock.exe`：外层是 `launcher/` 里的小启动器（纯 Win32 C++、静态 CRT，
不依赖 Windows App SDK），分发目录压缩后嵌在它的资源里。图标和版本信息直接复用 `app/MusicUnlock.rc`。

```sh
python scripts/build.py              # 同时编译启动器 launcher.exe 和打包工具 launcher_pack.exe
python scripts/build_app.py
python scripts/pack.py               # 先运行 stage_app.py，再生成 build/dist/MusicUnlock.exe
python scripts/pack.py --compare     # 比较压缩算法和分块大小
python scripts/screenshot.py --single [--convert]  # 通过单 exe 启动 App 渲染预览；--convert 另外在命令行上传一个文件，检查拖放导入
python scripts/screenshot.py --single --shortcut plain|app-id  # 从不带 / 带 AppUserModelID 的快捷方式启动，检查任务栏身份
```

- 打包：`launcher_pack` 把分发目录按扩展名排成一个数据块，按 4 MiB 分块用 LZMS 压缩（`cabinet.dll` 的
  Compression API），连同不压缩的清单（路径、大小、SHA-256）写进 exe 的 `PAYLOAD`（RCDATA）资源。
  清单的 SHA-256 就是版本标识，前 16 位十六进制是运行目录名。
- 首次运行：检查剩余空间，多线程解压，写进 `runtime\.tmp-<版本>-<进程号>\`，逐个文件从磁盘读回校验 SHA-256，
  记下每个文件的大小和修改时间（`.musicunlock-stamp`），再整体重命名为 `runtime\<版本>\`，半途中断不会留下看似完整的目录。
- 再次运行：只比较大小、修改时间和有没有多余的文件，不解压也不算哈希。对不上时逐个文件算哈希，
  重写不一致的、删掉多余的（例如被放进来的 DLL）；按文件修复，所以另一个窗口还开着时也能修复。
- 命令行原样传给 `MusicUnlock.exe`，拖到 exe 上的文件和“打开方式”都能导入；快捷方式的窗口状态和任务栏身份也一并传过去。
- 每个运行根目录一个具名互斥体：多个实例同时启动时只有一个在解压，其余等它完成后直接复用。App 启动后才放锁，
  然后删除其他版本的目录；有文件还开着（那个版本正在运行）的目录改名会失败，留到下次再删。只删除这种 16 位十六进制
  名称的目录和中断留下的临时目录。
- `cabinet.dll`、`bcrypt.dll` 只从 System32 加载（`/DEPENDENTLOADFLAG:0x800`），下载目录里放一个同名 DLL 不会被加载。
- 失败时用中文对话框说明原因和做法：磁盘空间不足（需要多少、还剩多少）、目录不可写、文件正在使用、exe 已损坏、
  解压出的文件校验失败或被安全软件拦截。
- `MUSICUNLOCK_RUNTIME_DIR` 换掉运行根目录（默认 `%LOCALAPPDATA%\MusicUnlock\runtime`）；`MUSICUNLOCK_LAUNCHER_LOG`
  把耗时和错误写进文件、不弹对话框，并等 App 退出后返回它的退出码。测试和 `screenshot.py --single` 都用这两个变量，
  运行目录在 `build/` 里，不写用户的 `%LOCALAPPDATA%`。
- 测试：`launcher_tests` 覆盖格式解析与路径校验（Mac 上也运行）、压缩、解压、篡改修复、旧版本清理和加锁；
  `smoke_launcher.py`（`build.py` 在 Windows 上自动运行）用一个记录启动方式的替身 App 打包出真正的 exe，只把它复制到空目录里运行，
  检查首次和再次启动、参数原样传递、篡改修复、6 个实例同时启动、旧版本清理与正在运行的版本保留，以及错误提示。

压缩方式（`pack.py --compare`，分发目录 53 个文件 61.4 MB，g16win 16 线程；64 MiB 一块即单线程）：

| 算法 | 分块 | 体积 | 解压 |
| --- | --- | --- | --- |
| XPRESS Huffman | 4 MiB | 25.7 MB | 16 ms（单线程 141 ms） |
| LZMS（采用） | 4 MiB | 19.0 MB | 70–230 ms（单线程约 0.7 s） |

LZMS 比 XPRESS Huffman 小约 26%，多出的解压时间只在首次运行时付一次。

### 任务栏与固定

App 实际运行的是 `runtime\<版本>\MusicUnlock.exe`。不做处理的话，固定到任务栏的是这个副本：升级后旧目录被删除，
固定的图标失效；从固定图标启动也会绕过启动器的校验和修复。所以（`app/Taskbar.cpp`）：

- 启动器启动 App 时设置环境变量 `MUSICUNLOCK_LAUNCHER_PATH` 为单 exe 的完整路径（覆盖继承来的值）；App 读取后立即清掉，
  不传给它打开的程序。
- 通过启动器启动时，App 在显示任何窗口之前用 `SetCurrentProcessExplicitAppUserModelID` 设置固定的 AppUserModelID
  `MusicUnlock.App`（文件对话框的最近项目也归到这个 ID），并在主窗口显示前用 `SHGetPropertyStoreForWindow` 设置
  `RelaunchCommand`（带引号的单 exe 路径，不带参数）、`RelaunchDisplayNameResource`（“音匣”）、
  `RelaunchIconResource`（单 exe 的图标 `<路径>,-1`），最后设置 `PKEY_AppUserModel_ID`。窗口销毁前
  （`AppWindow.Destroying`）按同样的顺序把它们清空，ID 最后清。固定时 Windows 用这些属性生成快捷方式，指向单 exe。
- 单 exe 被覆盖成新版本后，固定图标照样启动它，经过启动器的校验和旧版本清理；单 exe 被移动或改名后固定图标失效，这是预期行为。
- **直接运行**程序目录里的 `MusicUnlock.exe`（`build/app-release/`、`build/dist/MusicUnlock/`、开发调试）时什么都不设置，
  保持 Windows 按路径推导的身份：开发版不会和用户固定的单 exe 合成一个按钮，也不会把自己固定成正式版。
- **从快捷方式启动**时启动器把 `STARTF_TITLEISLINKNAME`（以及 `STARTF_TITLEISAPPID`）原样传给 App，App 读出快捷方式的
  AppUserModelID：是 `MusicUnlock.App`（例如从上面固定的图标启动）就照常设置；快捷方式没有 ID（用户自己建的快捷方式、
  在资源管理器里对 exe 文件“固定到任务栏”）时什么都不设置，让任务栏跟着快捷方式走——它本来就指向单 exe，显式 ID 反而会把
  窗口和这个固定图标分成两个按钮。
- 启动器本身不设置 ID：它没有窗口，只有出错时的对话框，那时还没有 App 窗口可以合并。
- **安装版（#10）**：开始菜单、桌面快捷方式都要在创建时写入 `System.AppUserModel.ID = MusicUnlock.App`（文件关联的
  ProgID 也写 `AppUserModelID`），否则快捷方式和运行中的窗口会分成两个任务栏按钮。安装版如果不经过启动器直接运行 App，
  App 也需要在那种情况下设置同一个 ID（目前只在 `MUSICUNLOCK_LAUNCHER_PATH` 存在时设置），并让 `RelaunchCommand`
  指向安装后的 exe，或者交给快捷方式提供。ID 一旦发布就不能再改。
- 测试：`MUSICUNLOCK_TASKBAR_LOG` 让 App 把进程 ID 和从窗口属性存储读回的值（设置后、清除后）写进文件；
  `screenshot.py` 每次都检查：`--single` 时 `RelaunchCommand` 指向 `build/dist/MusicUnlock.exe`、关闭前已清空，
  直接运行时一项都没有；`--shortcut plain|app-id` 由 `desktop_shot` 在输出目录里建一个快捷方式并按资源管理器的方式启动，
  分别检查不设置和照常设置。**真正固定到任务栏会改动用户的任务栏，只能人工验证。**

| 测量（g16win，Defender 实时保护关闭） | |
| --- | --- |
| `MusicUnlock.exe` | 19.4 MB（启动器本身 0.39 MB） |
| 首次启动（运行目录为空）到 App 进程启动 | 170–185 ms（解压 70–90 ms，写入并校验 35 ms） |
| 刚生成的 exe 第一次运行 | 约 1.0 s（另含系统第一次加载新 exe 的开销） |
| 再次启动到 App 进程启动 | 31–35 ms |

## 在 Windows 机器上并行开发

开发 Mac 上用 `remote.py` 把当前分支交给 Windows 机器（git 远程 `g16win`）构建。每个分支有自己的工作树
`D:\project\musicunlock\.worktrees\<分支名>`，多个会话同时开发不同分支时互不干扰：

```sh
python3 apps/windows/scripts/remote.py build                  # 核心、测试与命令行冒烟测试
python3 apps/windows/scripts/remote.py app                    # 编译界面
python3 apps/windows/scripts/remote.py screenshot [--convert] [--motion 场景|all] # 编译界面并渲染预览，取回到 build/previews/
python3 apps/windows/scripts/remote.py screenshot --dist      # 同上，但先整理分发目录，用它渲染
python3 apps/windows/scripts/remote.py screenshot --single    # 同上，但先生成单 exe，通过它启动 App
python3 apps/windows/scripts/remote.py screenshot --single --shortcut plain   # 同上，从快捷方式启动
python3 apps/windows/scripts/remote.py run -- <命令>           # 在工作树的 apps/windows 下运行任意命令
python3 apps/windows/scripts/remote.py list | remove          # 查看工作树 / 删除当前分支的工作树
```

- 同步时把工作区（已跟踪和未跟踪的文件，遵守 `.gitignore`）通过临时索引做成一个快照提交，推到远端的
  `refs/heads/wt/<分支名>` 再检出，不改动本地的索引和分支；未提交的改动也会带过去。
- 各工作树的 `build/` 互相独立；NuGet 包共用 `.worktrees\.packages`（`MUSICUNLOCK_PACKAGES_DIR`），
  恢复时加锁，只下载一次。`nuget-imports.props` 仍写在各自的 `build/` 里，分支的包清单不同也不会冲突。
- `screenshot.py` 的计划任务名和桌面名带有按工作树路径算出的后缀，两个工作树可以同时截图。
- 工作树目录已加入 `.gitignore`，远端主检出也在 `.git/info/exclude` 里忽略它。

## 玻璃界面基础

液态玻璃界面（总控 #57）的三套公共接口在 `app/` 下，签名在 #59 里定死。A–F 只调用这些接口；真正的玻璃材质由 #60 填进 `GlassSurface`，连续曲率由 #61 填进 `corners`，两边都不改签名。

- `SpringCurve.h`：SwiftUI `spring(response:dampingFraction:)` 的单位质量阻尼谐振子，纯 C++20，Mac 上的 `python scripts/build.py` 也会跑 `tests/ui_math_tests.cpp`。`value` 给出从 0 走到 1 的位移，`settle_time` 是之后与终值的差恒小于 ε 的最早时刻，`keyframes` 按帧采样成归一化关键帧。
- `GlassMotion.h`：唯一的参数表，与 macOS `Theme.swift` 的 `GlassMotion` 一一对应。数值是社区对 iOS 26 的估算，不是实测，改动时两端一起改：

  | 名称 | 用途 | response | damping |
  | --- | --- | --- | --- |
  | `open` | 菜单从按钮长出 | 0.32 | 0.78 |
  | `close` | 菜单缩回按钮 | 0.26 | 1 |
  | `lens` | 切换开关透镜移动 | 0.3 | 0.72 |
  | `press` | 透镜按下放大 | 0.22 | 0.6 |
  | `highlight` | 菜单行高亮滑动 | 0.2 | 0.85 |
  | `workspace` | 工作区切换 | 0.5 | 0.85 |
  | `pressable` | 普通按钮按下回弹 | 0.3 | 0.5 |
  | `footer` | 底栏按钮出现、消失、变形 | 0.45 | 0.8 |
  | `selection` | 歌词结果选中高亮滑动 | 0.35 | 0.8 |

  `handoff` 不是弹簧：菜单面板与按钮交叉淡化，easeInOut 0.08 秒，相对 `close` 开始延迟 0.12 秒。`scalar` / `vector2` / `vector3` 用 `SpringCurve` 的关键帧生成组合动画，起点取属性当前值，所以中途被打断不会跳；`motion::enabled()` 为 false 时返回空，调用方直接设终值。不用 `SpringNaturalMotionAnimation`，它的 `Period` 与 `response` 没有文档化的换算。
- `GlassSurface`（`GlassSurface.idl`）：所有玻璃表面的控件，派生自 `ContentControl`（`Border` 不是可组合的运行时类）。`Kind` 区分面板、控件和弹出菜单，`Radius` 为负时是高度一半的胶囊，`TintOpacity` 叠加 `MuTextColor`，`OnGlass` 表示已经放在另一块玻璃上。内容放在 `ContentPresenter` 里，背景由控件模板绘制，不参与内容的布局。现在是占位：面板用 `MuPanelBrush`，控件、菜单和 `OnGlass` 用不透明的 `MuSurfaceBrush`，都加 1px `MuSeparatorBrush` 描边，菜单另加阴影。画刷用 `ThemeResource`，跟随元素请求的主题。目前只替换了音乐解锁工作区的底栏，观感与原来的 `MuPanelStyle` 一致。
- `Corners.h`：保留 `geometry`、`clip`、`stroke` 三个入口，通过 Direct2D 路径与 `IGeometrySource2DInterop` 向合成器提供连续圆角，不依赖 Win2D。每角三段贝塞尔，横纵短边独立压缩；半径恰好等于短边一半时保留压缩连续角，超过时夹成正圆端胶囊。纯 C++ 的 `ContinuousPath` 与 SwiftUI 核对点共用 `ui_math_tests`，另检查四角方向、边间连接、宽高互换、零半径及胶囊径向误差。
- XAML 使用 `local:Corners.Radius="14"`，可挂在 `Border`、`Control`、`Grid` 或 `Shape`。自动替换原生圆角和单色等宽描边，描边中心线内缩半个线宽并按外轮廓裁剪；原生描边占用的空间补入 Padding，保持内容布局。支持 Shape 虚线；当前接入不涉及渐变或不等宽描边。状态存于元素私有附加属性，不以全局强引用表持有元素；同尺寸和半径复用裁剪，布局回调只比较缓存，画刷变色时刷新描边。`GlassSurface` 模板的 `Plate` 共用此属性，负半径明确使用圆端胶囊。

## 界面设计对应

布局、文案和交互沿用 [macOS 版 DESIGN.md](../macos/DESIGN.md)：左侧转换队列，右侧 268 px 导出设置，底栏显示状态与操作。
歌词工作区上方是来源、类型与关键词或分享链接，左侧 300 px 可勾选结果，右侧是原文 / 译文 / 罗马音 / 导出预览编辑区，
底栏是格式、导出设置与导出操作；导出细项放在对话框里。

- Liquid Glass 换成 Windows 系统材质：窗口用 Mica；工作区底下是 6 个灰阶径向渐变缓慢漂移的流动背景（`GlassSurface.cpp`
  的 `Backdrop`，轨迹在 `GlassField.h`），面板（`GlassSurface` 的 `Panel`）用应用内 `AcrylicBrush` 加一圈上亮下暗的渐变边，
  控件和菜单是不透明表面。窗口失去激活、最小化、“动画效果”或“透明效果”关闭、高对比度和静态预览时背景停住，
  透明效果关闭或高对比度时面板改用纯色。不用自绘模糊：WinUI 3 的组合器不接受外部传入的模糊效果（见 #60）。
  Windows 10 没有 Mica，退回桌面亚克力，两者都不可用时用纯色工作区背景；
  `MUSICUNLOCK_BACKDROP=acrylic` 或 `solid` 可以在 Windows 11 上强制使用退回方案来检查。
- 只用黑白灰：系统强调色被覆盖为墨灰（浅色 #1C1C1C、深色 #F2F2F2），主按钮、选中指示与焦点随之变灰；
  状态靠 Segoe Fluent 图标和文字区分，不靠颜色。支持高对比度主题。
- 连续圆角遵循内外同心规则：歌词结果外层 20、行左右内缩 6，行容器和选中高亮均为 14；空队列外框 22、虚线内缩 8，虚线圆角 14。格式标签为 5、封面为 7、输出按钮为 8；二维码白底 12、内缩 1 的遮罩 11，网页登录容器 8，24×24 徽标仍为正圆。仅替换轮廓，不增加可访问节点或 Tab 停靠点。
- 工具栏换成自定义标题栏：中间是工作区切换，右侧是添加文件夹、添加文件（歌词工作区是打开歌词、账号登录）；
  每组动作收进一个玻璃胶囊，只留图标，文字在提示和讲述人名称里。
  工作区切换、队列筛选、歌词编辑区页签和账号对话框的平台切换都用 `GlassSwitch`（`app/GlassSwitch.*`，对应 macOS
  `GlassSwitch`）：胶囊轨道里一颗透镜滑到选中段的后面，不盖住文字；图标段 46×28，文字段高 24、字号 12，选中项加粗。
  编辑区页签和平台切换设了 `OnGlass`，用浅色轨道加实心透镜，避免玻璃叠玻璃。按下立即选中，按住拖动连续切换；
  整个开关是一个 Tab 停靠点，左右方向键移动，Home / End 到首末段。自动化树提供 Selection / SelectionItem 模式、段名称、选中状态和序号。
  透镜移动用 `glass_motion::lens`，按下放大和阴影用 `press`，悬停文字用 100ms easeOut 颜色过渡；
  系统“动画效果”关闭时停止已有动画并直接到位，旧系统在下一次布局检查时同步状态。
  静态预览与透明效果关闭时使用主题画刷降级；位置和宽度用 XAML 采样弹簧，避免离屏渲染保留旧的 Composition 位移。
  标题栏里的开关保留为可交互 `Control`；真实鼠标点击、拖动不移动窗口的人工验收尚待用户操作确认。
  已连接的自动化树会收到 SelectionItem 选中状态与选中事件通知，不在初始化阶段主动创建 UIA 树。
  `switch-tests` / `switch-layout-tests` 验证集合、自动化选择、单 Tab 停靠点、Fill 等宽、文字重布局、悬停与减少动画；
  平台切换的自动化选择回归随账号静态预览执行。与原生 SelectorBar 相比，父项为 List、子项为 ListItem，
  所有分段共享一个 Tab 停靠点；方向键与 Home / End 的真实焦点路由、讲述人播报仍待人工确认。
  当前 #66 场景只提供日志，尚不能作为逐帧动效验收依据，亦不能证明真实拖动交互已通过。
  新增 `text200-lyrics-*`、`contrast-lyrics`、`contrast-text200-*` 和 `accounts-text200-*` 静态预览。
  200% 预览中，歌词页的歌曲统计和平台下拉框存在截断，编辑正文不随 TextBlock 模拟放大；
  这些开关以外的布局与预览限制不作为本任务已修复项。模拟高对比度不等于真实系统高对比度验收。
- 主按钮是墨色实心胶囊（浅色 #1C1C1C 白字、深色 #F2F2F2 黑字），次按钮是 `GlassSurface` 胶囊，卡片上的次按钮用不透明胶囊加细描边。
  按下缩放到 0.96（普通按钮和可点的行是 0.97）并变淡，松开用 `glass_motion::pressable` 回弹；“动画效果”关闭时只改不透明度。
  底栏里共用一个位置的两个按钮（开始/停止、搜索/取消、导出当前/停止导出）在同一个胶囊里过渡，
  “重试”“打开输出目录”“导出所选”出现时从主按钮的位置弹出来，消失时弹回去（`MorphGroup`，弹簧 `footer`）。
- 快捷键：Ctrl+O 添加文件（歌词工作区里是打开歌词）、Ctrl+Shift+O 添加文件夹、Ctrl+Enter 开始、Ctrl+. 停止转换或导出、
  Ctrl+1 / Ctrl+2 切换工作区、Ctrl+F 搜索歌词、Ctrl+S 导出当前歌词。
  队列行可以获得焦点，方向键在行间移动，Delete 移出队列；右键、Shift+F10 或菜单键打开行菜单
  （在资源管理器中显示源文件 / 输出文件、查找这首歌的歌词、移出队列）。
- 动效对应 DESIGN.md 的“动效”一节（`app/Motion.*`，曲线一律用 `glass_motion` 的弹簧采样）：
  两个工作区切换时各向自己一侧滑出 40 px 并淡出、另一侧滑入，同时在 98% 与 100% 之间缩放（`workspace` 弹簧）；
  标题栏两组操作保持交叉淡化，但用同一条弹簧。macOS 这里还有 6 px 模糊，WinUI 3 的组合器不接受外部传入的模糊效果，
  所以没有做。空状态、列表与筛选为空之间交叉淡入淡出；队列行和提示条从上方轻微落下、淡入，离开时原地缩小、淡出；
  状态图标切换时从 60% 放大淡入，完成时先过冲再回落；队列行滚出上下边缘时淡到 40%、缩到 96%，锚点取它离开的那一边
  （WinUI 3 的滚动属性集不更新，所以按滚动事件逐帧计算，而不是表达式动画）；
  歌词结果只有一块选中高亮，在行间用 `selection` 弹簧滑动，滚动时跟手；
  添加文件、换输出目录、开始与结束转换、搜索结果数量变化、打开导出设置时对应图标弹跳（先缩到 82% 再弹回），
  导出开始与结束时导出图标下沉 3 px 再回；登录状态的两个图标交叉替换；转换进行时“保留原始音质”换成四根依次变亮的竖条；
  网易云二维码从 92% 淡入放大，过期时淡到 35%，状态文字交叉淡化。
  拖入时覆盖层淡入并轻微缩放，图标上下跳动，空状态的唱片缓慢浮动，拖入时抬起并倾斜；行悬停是列表自带的浅色高亮。
  系统“动画效果”关闭（`UISettings.AnimationsEnabled`）或设置了 `MUSICUNLOCK_REDUCE_MOTION` 时一律不动，
  窗口重新激活时重新读取这个设置。预览渲染保持静态。
- 讲述人：队列行的名称是整行内容（标题、来源与大小、格式、状态、失败原因，规则在核心的 `AudioJob::spoken_summary`），
  图标按钮都有名称和快捷键，底栏状态是礼貌的实时区域，转换失败和提示条的内容会主动播报。
- 系统文字放大：文字由系统放大，列表的格式、状态两列按 `UISettings.TextScaleFactor` 加宽，
  放不下时筛选移到标题下一行，底栏按钮组放不下时换到状态文字的下一行，空状态可以滚动。
- 导出位置与命名方式保存在 `%LOCALAPPDATA%\MusicUnlock\settings.json`，歌词导出设置在同目录的 `lyrics.json`；
  转换或导出歌词时关闭窗口会先确认。
- 歌词请求在后台线程直连平台，可随时取消；批量导出逐首进行，可以停止，已完成的文件保留，同名不覆盖，失败原因逐首列出。
- 歌词工作区标题栏右侧是“账号登录”，对话框分网易云音乐和 QQ 音乐两页（对应 macOS 的账号菜单和登录 Sheet）：
  - 网易云：nayuki QR Code generator 生成矢量二维码，每 2 秒轮询一次（800 已过期、801 等待扫码、802 已扫码、803 已确认），
    关闭对话框即停止；也可以改用官方登录页。扫码登录照官方 Windows 客户端的方式请求：eapi 加密（AES-128-ECB + MD5 摘要）发到
    `interfacepc.music.163.com`，Cookie 和加密体里都带 `os=pc`、`appver` 和每个二维码随机的 `deviceId`，二维码内容带同一设备的
    `chainId`。网页式的明文请求同样能拿到 801，但手机扫码后会被风控拒绝（8821“请切换其他登录方式或升级新版本再试”）。QQ 音乐：WebView2 打开官方登录页，每秒读取一次 Cookie，
    出现音乐会话后再读一次确认，交给 `mu::accept_session` 判断。登录页只允许 https，不允许下载、权限请求、开发者工具。
  - 会话用 DPAPI（当前用户）加密 `mu::cookies_to_json` 的结果，存为 `%LOCALAPPDATA%\MusicUnlock\sessions\<平台>.bin`；
    不提供手动输入 Cookie 的地方。显示“会话已保存”和 `MusicApi::account_name` 取得的账号名，有效期由平台决定。
  - WebView2 数据每个平台一个目录：`%LOCALAPPDATA%\MusicUnlock\WebView2\<平台>`。退出登录删除会话文件和该平台的
    整个 WebView2 目录（Cookie 与浏览数据）；登录页的浏览器进程还没退出时，稍后重试，并在下次打开登录页或启动时补删。
  - 以上目录都跟随 `MUSICUNLOCK_DATA_DIR`。歌词请求通过 `accounts::cookie_header(平台)` 取 Cookie，没登录时为空。

## 验证

- 核心测试覆盖五组 QQ 音乐上游样本逐字节一致、密钥派生、KGM 常量、NCM 容器与元数据、ID3 / FLAC 标签、
  KWM / XM、同名文件不覆盖、超过 260 字符的输出路径、Windows 文件名规则（保留字符、设备名、结尾的点与空格）以及坏文件不崩溃。
- 歌词测试对照 macOS 的 `LyricsTests` 移植：LRC 解析（重复时间戳、offset、逐字 LRC、YRC）、LRC / SRT / VTT / TXT 导出的
  三种排列、时间偏移与精度、拼音（系统 ICU，与 macOS 结果逐字一致）、UTF-8 / UTF-8 BOM / UTF-16 LE 与 GBK 读取、
  导出不覆盖、链接识别、Set-Cookie 与会话规则；接口请求和解析用脚本化传输层测试，无需联网。
- 账号测试：二维码矩阵与路径数据、扫码登录的 eapi 请求（解密后核对路径、摘要、客户端标识和 chainId）、
  扫码状态对应与轮询（确认、过期、超时、无效会话、网络错误、取消）、
  会话文件的保存、读取、过期、损坏与删除；Windows 上另外用真实 DPAPI 加解密并检查篡改后无法解开。
- 歌词工作区的规则对照 macOS 的 `LyricsModel` 放在核心（`lyric_workspace.h`）里测试：搜索与翻页、链接和歌单解析、
  过期结果丢弃、勾选与预览分开、编辑缓存、本地歌词、导出预览与设置错误、批量导出的跳过 / 失败 / 停止和提示文案。
- 设置 `MUSICUNLOCK_LIVE_TESTS=1` 后在 Windows 上运行测试，会通过 WinHTTP 实际请求网易云和 QQ 音乐的搜索、
  链接解析、歌词和扫码接口（生成一个二维码并查询一次，应为 801），并用歌词工作区免登录搜索、导出，核对文件与核心渲染结果逐字节一致。
  测试程序可以带测试名片段只跑一部分，例如 `musicunlock_core_tests live_netease`。
- 与 macOS Swift CLI 对同一批 31 个样本（覆盖全部格式、封面与标签、错误文件和同名冲突）交叉比对，输出逐字节一致。

## 目录边界

Windows 工程、源码、测试与打包脚本均放在此目录。构建输出、IDE 缓存和签名私钥不提交。
开发验证需要 Windows 环境，重点检查真实账号授权、音频兼容性、资源占用和干净系统部署。

macOS 版本见 [../macos/](../macos/)。

项目代码采用根目录的 [MIT 许可证](../../LICENSE)，算法衍生部分和数据保留 [第三方声明](../../THIRD_PARTY_NOTICES.txt)。
