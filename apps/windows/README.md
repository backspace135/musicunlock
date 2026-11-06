# 音匣 Windows

Windows 原生版本的开发目录，与 macOS 版本在同一个 Git 仓库维护。

**当前状态：解封装核心、命令行版、歌词核心、WinUI 3 音乐解锁与歌词工作区、账号登录已实现；单 exe 打包开发中。**

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

```text
apps/windows/
├── core/          纯 C++20 静态库：解密、标签、命名、歌词解析与导出、登录会话、网易云 / QQ 音乐接口
│                  （Windows 用 CNG、WinHTTP、系统 ICU；Mac 开发机用 CommonCrypto、CoreFoundation）
├── cli/           musicunlock.exe，用法与 macOS CLI 相同；静态 CRT，kgm.mask 以资源内嵌
├── app/           WinUI 3 界面（C++/WinRT，未打包、Windows App SDK 自包含、静态 CRT）
├── tools/         desktop_shot.cpp：在独立桌面上运行界面预览渲染
├── tests/         core 与歌词测试，读取 ../../tests/fixtures/qmc
├── third_party/   nlohmann/json 3.12.0（单头文件，MIT）、nayuki QR-Code-generator 1.8.0（MIT）
└── scripts/       build.py：核心与命令行；smoke_cli.py：命令行冒烟测试；restore.py / build_app.py：界面；
                   stage_app.py：整理分发目录；screenshot.py：界面预览与端到端转换检查；
                   remote.py：在 Windows 机器的独立工作树里构建
```

## 构建核心与命令行版

Windows 需要 Visual Studio 2022 Build Tools（“使用 C++ 的桌面开发”，含 MSVC x64、Windows SDK 与 CMake）和 Python 3：

```sh
python scripts/build.py              # Release，编译后运行核心测试和命令行冒烟测试
python scripts/build.py --config Debug --no-test
```

脚本通过 vswhere 定位 MSVC，使用 VS 自带的 CMake 与 Ninja。输出、缓存和临时文件都在 `build/` 下，
产物为 `build/windows-release/musicunlock.exe`，单文件即可运行：

```bat
musicunlock.exe D:\输出目录 D:\音乐\歌曲.ncm D:\另一个音乐目录
```

开发 Mac 上同一命令可编译核心与测试，用于快速验证算法（需要 CMake 与 Ninja）。

GitHub Actions（`.github/workflows/windows.yml`）在 windows-2022 上执行同一脚本、编译界面，上传 `musicunlock.exe`、
精简后的界面目录和单独的符号文件；
手动触发时额外运行联网的平台接口检查。

## 构建界面

同样只需要 Build Tools 的 C++ 工作负载，不需要 Visual Studio IDE 或 UWP 组件：

```sh
python scripts/build_app.py          # 恢复 NuGet 包并用 MSBuild 编译，产物在 build/app-release/
python scripts/screenshot.py         # 空队列、示例队列、歌词工作区（空状态、编辑区）与账号登录对话框的预览，输出到 build/previews/
python scripts/screenshot.py --convert   # 另外导入并实际转换示例文件，逐字节核对输出
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
- 账号登录对话框的各状态（二维码等待、已扫码、已过期、已确认，QQ 登录页，会话已保存）有浅色、深色和高对比度三套预览。
  高对比度是系统设置，App 不能自己打开，所以预览换用 `Theme.xaml` 的 HighContrast 画刷模拟（WinUI 自带控件仍是浅色样式）；
  对话框在窗口之外的弹出层里，预览把它叠加到窗口画面上。渲染不加载任何登录页，只启动一次 WebView2，检查运行时能从
  App 目录加载、数据目录在 `build/previews/appdata/WebView2/` 下。

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

## 在 Windows 机器上并行开发

开发 Mac 上用 `remote.py` 把当前分支交给 Windows 机器（git 远程 `g16win`）构建。每个分支有自己的工作树
`D:\project\musicunlock\.worktrees\<分支名>`，多个会话同时开发不同分支时互不干扰：

```sh
python3 apps/windows/scripts/remote.py build                  # 核心、测试与命令行冒烟测试
python3 apps/windows/scripts/remote.py app                    # 编译界面
python3 apps/windows/scripts/remote.py screenshot [--convert] # 编译界面并渲染预览，取回到 build/previews/
python3 apps/windows/scripts/remote.py screenshot --dist      # 同上，但先整理分发目录，用它渲染
python3 apps/windows/scripts/remote.py run -- <命令>           # 在工作树的 apps/windows 下运行任意命令
python3 apps/windows/scripts/remote.py list | remove          # 查看工作树 / 删除当前分支的工作树
```

- 同步时把工作区（已跟踪和未跟踪的文件，遵守 `.gitignore`）通过临时索引做成一个快照提交，推到远端的
  `refs/heads/wt/<分支名>` 再检出，不改动本地的索引和分支；未提交的改动也会带过去。
- 各工作树的 `build/` 互相独立；NuGet 包共用 `.worktrees\.packages`（`MUSICUNLOCK_PACKAGES_DIR`），
  恢复时加锁，只下载一次。`nuget-imports.props` 仍写在各自的 `build/` 里，分支的包清单不同也不会冲突。
- `screenshot.py` 的计划任务名和桌面名带有按工作树路径算出的后缀，两个工作树可以同时截图。
- 工作树目录已加入 `.gitignore`，远端主检出也在 `.git/info/exclude` 里忽略它。

## 界面设计对应

布局、文案和交互沿用 [macOS 版 DESIGN.md](../macos/DESIGN.md)：左侧转换队列，右侧 268 px 导出设置，底栏显示状态与操作。
歌词工作区上方是来源、类型与关键词或分享链接，左侧 300 px 可勾选结果，右侧是原文 / 译文 / 罗马音 / 导出预览编辑区，
底栏是格式、导出设置与导出操作；导出细项放在对话框里。

- Liquid Glass 换成 Windows 系统材质：窗口用 Mica，面板是半透明表面加细描边，不用自绘模糊。
- 只用黑白灰：系统强调色被覆盖为墨灰（浅色 #1C1C1C、深色 #F2F2F2），主按钮、选中指示与焦点随之变灰；
  状态靠 Segoe Fluent 图标和文字区分，不靠颜色。支持高对比度主题。
- 工具栏换成自定义标题栏：中间是工作区切换，右侧是添加文件夹、添加文件（歌词工作区是打开歌词、账号登录）；
  队列筛选和编辑区页签用 SelectorBar。
- 快捷键：Ctrl+O 添加文件（歌词工作区里是打开歌词）、Ctrl+Shift+O 添加文件夹、Ctrl+Enter 开始、Ctrl+. 停止转换或导出、
  Ctrl+1 / Ctrl+2 切换工作区、Ctrl+F 搜索歌词、Ctrl+S 导出当前歌词。队列行的右键菜单可以查找这首歌的歌词。
- 导出位置与命名方式保存在 `%LOCALAPPDATA%\MusicUnlock\settings.json`，歌词导出设置在同目录的 `lyrics.json`；
  转换或导出歌词时关闭窗口会先确认。
- 歌词请求在后台线程直连平台，可随时取消；批量导出逐首进行，可以停止，已完成的文件保留，同名不覆盖，失败原因逐首列出。
- 歌词工作区标题栏右侧是“账号登录”，对话框分网易云音乐和 QQ 音乐两页（对应 macOS 的账号菜单和登录 Sheet）：
  - 网易云：nayuki QR Code generator 生成矢量二维码，每 2 秒轮询一次（800 已过期、801 等待扫码、802 已扫码、803 已确认），
    关闭对话框即停止；也可以改用官方登录页。QQ 音乐：WebView2 打开官方登录页，每秒读取一次 Cookie，
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
- 账号测试：二维码矩阵与路径数据、扫码状态对应与轮询（确认、过期、超时、无效会话、网络错误、取消）、
  会话文件的保存、读取、过期、损坏与删除；Windows 上另外用真实 DPAPI 加解密并检查篡改后无法解开。
- 歌词工作区的规则对照 macOS 的 `LyricsModel` 放在核心（`lyric_workspace.h`）里测试：搜索与翻页、链接和歌单解析、
  过期结果丢弃、勾选与预览分开、编辑缓存、本地歌词、导出预览与设置错误、批量导出的跳过 / 失败 / 停止和提示文案。
- 设置 `MUSICUNLOCK_LIVE_TESTS=1` 后在 Windows 上运行测试，会通过 WinHTTP 实际请求网易云和 QQ 音乐的搜索、
  链接解析、歌词和扫码接口，并用歌词工作区免登录搜索、导出，核对文件与核心渲染结果逐字节一致。
- 与 macOS Swift CLI 对同一批 31 个样本（覆盖全部格式、封面与标签、错误文件和同名冲突）交叉比对，输出逐字节一致。

## 目录边界

Windows 工程、源码、测试与打包脚本均放在此目录。构建输出、IDE 缓存和签名私钥不提交。
开发验证需要 Windows 环境，重点检查真实账号授权、音频兼容性、资源占用和干净系统部署。

macOS 版本见 [../macos/](../macos/)。

项目代码采用根目录的 [MIT 许可证](../../LICENSE)，算法衍生部分和数据保留 [第三方声明](../../THIRD_PARTY_NOTICES.txt)。
