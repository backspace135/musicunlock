# 音匣 Windows

Windows 原生版本的开发目录，与 macOS 版本在同一个 Git 仓库维护。

**当前状态：解封装核心、命令行版、歌词核心与 WinUI 3 音乐解锁工作区已实现；歌词工作区与单 exe 打包开发中。**

## 技术方向

- C++20 + C++/WinRT + WinUI 3 / XAML，使用原生界面；仅 x64，支持 Windows 10 1809+ 与 Windows 11。
- 沿用 macOS 版的品牌色、布局与双工作区；使用 Windows 系统材质（Mica / Acrylic）、控件和交互。
- 原生音频解封装引擎，使用 `../../tests/fixtures/qmc/` 中的相同测试向量验证；公共 KGM 数据位于 `../../resources/kgm.mask`。
- 歌词搜索、编辑与导出，优先使用原生扫码授权和 Windows 系统凭据保护。
- 自包含分发：最终产物是单个 exe。外层小型启动器内嵌压缩后的 WinUI 3 程序目录（系统 Compression API），
  首次运行解压到 `%LOCALAPPDATA%\MusicUnlock\runtime\<版本哈希>\` 并校验，用户无需安装 .NET 或 Windows App Runtime。
- 原生扫码能否覆盖 QQ 音乐登录需要在 Windows 上验证，不能假定浏览器授权自动提供 App 会话。

## 目录

```text
apps/windows/
├── core/          纯 C++20 静态库：解密、标签、命名、歌词解析与导出、登录会话、网易云 / QQ 音乐接口
│                  （Windows 用 CNG、WinHTTP、系统 ICU；Mac 开发机用 CommonCrypto、CoreFoundation）
├── cli/           musicunlock.exe，用法与 macOS CLI 相同；静态 CRT，kgm.mask 以资源内嵌
├── app/           WinUI 3 界面（C++/WinRT，未打包、Windows App SDK 自包含、静态 CRT）
├── tools/         desktop_shot.cpp：在独立桌面上运行界面预览渲染
├── tests/         core 与歌词测试，读取 ../../tests/fixtures/qmc
├── third_party/   nlohmann/json 3.12.0（单头文件，MIT）
└── scripts/       build.py：核心与命令行；smoke_cli.py：命令行冒烟测试；restore.py / build_app.py：界面；
                   screenshot.py：界面预览与端到端转换检查
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

GitHub Actions（`.github/workflows/windows.yml`）在 windows-2022 上执行同一脚本、编译界面并上传 `musicunlock.exe`；
手动触发时额外运行联网的平台接口检查。

## 构建界面

同样只需要 Build Tools 的 C++ 工作负载，不需要 Visual Studio IDE 或 UWP 组件：

```sh
python scripts/build_app.py          # 恢复 NuGet 包并用 MSBuild 编译，产物在 build/app-release/
python scripts/screenshot.py         # 空队列与示例队列的浅色 / 深色预览，输出到 build/previews/
python scripts/screenshot.py --convert   # 另外导入并实际转换示例文件，逐字节核对输出
```

- `restore.py` 按 `app/packages.config` 从 nuget.org 下载固定版本并校验 SHA-512，解压到 `build/packages/`。
  只引用界面用到的 Windows App SDK 组件，不用包含 AI、ML、Widgets、Search 的元包。
- 工程是普通桌面 C++ 项目：VC 只在 “Windows Store” 应用类型里调用 XAML 编译器，所以 `MusicUnlock.vcxproj`
  自己在 MIDL 前后运行两遍 WinUI 标记编译。
- 预览由 App 自己用 `RenderTargetBitmap` 渲染：`screenshot.py` 通过一次性计划任务在登录会话里启动
  `desktop_shot.exe`，它新建独立桌面运行 App，结束后关闭桌面并删除计划任务，不显示也不截取用户桌面。
  渲染时 `MUSICUNLOCK_DATA_DIR` 指向输出目录，设置不会写入用户的 AppData。

## 界面设计对应

布局、文案和交互沿用 [macOS 版 DESIGN.md](../macos/DESIGN.md)：左侧转换队列，右侧 268 px 导出设置，底栏显示状态与操作。

- Liquid Glass 换成 Windows 系统材质：窗口用 Mica，面板是半透明表面加细描边，不用自绘模糊。
- 只用黑白灰：系统强调色被覆盖为墨灰（浅色 #1C1C1C、深色 #F2F2F2），主按钮、选中指示与焦点随之变灰；
  状态靠 Segoe Fluent 图标和文字区分，不靠颜色。支持高对比度主题。
- 工具栏换成自定义标题栏：中间是工作区切换，右侧是添加文件夹、添加文件；队列筛选用 SelectorBar。
- 快捷键：Ctrl+O 添加文件、Ctrl+Shift+O 添加文件夹、Ctrl+Enter 开始、Ctrl+. 停止、Ctrl+1 / Ctrl+2 切换工作区。
- 导出位置与命名方式保存在 `%LOCALAPPDATA%\MusicUnlock\settings.json`；转换中关闭窗口会先确认。

## 验证

- 核心测试覆盖五组 QQ 音乐上游样本逐字节一致、密钥派生、KGM 常量、NCM 容器与元数据、ID3 / FLAC 标签、
  KWM / XM、同名文件不覆盖、超过 260 字符的输出路径、Windows 文件名规则（保留字符、设备名、结尾的点与空格）以及坏文件不崩溃。
- 歌词测试对照 macOS 的 `LyricsTests` 移植：LRC 解析（重复时间戳、offset、逐字 LRC、YRC）、LRC / SRT / VTT / TXT 导出的
  三种排列、时间偏移与精度、拼音（系统 ICU，与 macOS 结果逐字一致）、UTF-8 / UTF-8 BOM / UTF-16 LE 与 GBK 读取、
  导出不覆盖、链接识别、Set-Cookie 与会话规则；接口请求和解析用脚本化传输层测试，无需联网。
- 设置 `MUSICUNLOCK_LIVE_TESTS=1` 后在 Windows 上运行测试，会通过 WinHTTP 实际请求网易云和 QQ 音乐的搜索、
  链接解析、歌词和扫码接口。
- 与 macOS Swift CLI 对同一批 31 个样本（覆盖全部格式、封面与标签、错误文件和同名冲突）交叉比对，输出逐字节一致。

## 目录边界

Windows 工程、源码、测试与打包脚本均放在此目录。构建输出、IDE 缓存和签名私钥不提交。
开发验证需要 Windows 环境，重点检查真实账号授权、音频兼容性、资源占用和干净系统部署。

macOS 版本见 [../macos/](../macos/)。

项目代码采用根目录的 [MIT 许可证](../../LICENSE)，算法衍生部分和数据保留 [第三方声明](../../THIRD_PARTY_NOTICES.txt)。
