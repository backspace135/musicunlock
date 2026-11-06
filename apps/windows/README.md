# 音匣 Windows

Windows 原生版本的开发目录，与 macOS 版本在同一个 Git 仓库维护。

**当前状态：解封装核心、命令行版与歌词核心已实现；WinUI 3 界面与单 exe 打包开发中。**

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
├── tests/         core 与歌词测试，读取 ../../tests/fixtures/qmc
├── third_party/   nlohmann/json 3.12.0（单头文件，MIT）
└── scripts/       build.py：配置、编译与测试
```

## 构建核心与命令行版

Windows 需要 Visual Studio 2022 Build Tools（“使用 C++ 的桌面开发”，含 MSVC x64、Windows SDK 与 CMake）和 Python 3：

```sh
python scripts/build.py              # Release，编译后运行测试
python scripts/build.py --config Debug --no-test
```

脚本通过 vswhere 定位 MSVC，使用 VS 自带的 CMake 与 Ninja。输出、缓存和临时文件都在 `build/` 下，
产物为 `build/windows-release/musicunlock.exe`，单文件即可运行：

```bat
musicunlock.exe D:\输出目录 D:\音乐\歌曲.ncm D:\另一个音乐目录
```

开发 Mac 上同一命令可编译核心与测试，用于快速验证算法（需要 CMake 与 Ninja）。

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
