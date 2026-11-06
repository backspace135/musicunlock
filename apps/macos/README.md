# 音匣 MusicUnlock

使用 SwiftUI 与 Apple Liquid Glass 的原生 macOS 音频解封装与歌词工具。
解密逻辑移植自 Unlock Music；原项目的网页界面不参与编译。
App 只链接 Apple 系统框架，不包含 Chromium、Electron、Node 或第三方 JavaScript 运行时。
工作区全部使用 SwiftUI；仅官方账号授权页面使用系统 WebKit。

## 环境

- macOS 26 或更新版本（原生 Liquid Glass）
- Xcode 26 或更新版本，Swift 5.9+ 包格式
- 当前打包脚本构建本机架构，本机为 Apple Silicon / arm64

## 构建

在仓库的 `apps/macos` 目录执行：

```sh
python3 scripts/build-app.py
```

输出：

- `build/音匣.app`：可双击的 App，可复制到应用程序目录
- `build/MusicUnlock-macOS-arm64.zip`：本地压缩包
- `build/musicunlock`：命令行版本

产物使用本地 ad-hoc 签名，不含 Developer ID 签名或 Apple 公证。
源码与生成文件分开存放，`build/` 和 `.build/` 均已忽略。

## 使用

1. 拖入文件/文件夹，或点击“添加文件”。⌘O 添加文件，⇧⌘O 添加文件夹。
2. 右侧选择输出目录和命名方式，默认输出到 `~/Downloads/音匣输出`。
3. 点击“开始转换”（⌘↩）。同名文件自动编号，不修改源文件。
4. 每项显示成功或具体失败原因；失败文件可以单独重试。⌘. 在当前文件完成后停止。

命令行：

```sh
./build/musicunlock /输出目录 /音乐文件.ncm /另一个音乐目录
```

CLI 转换酷狗时，可用环境变量 `MUSICUNLOCK_KGM_MASK` 指向
`../../resources/kgm.mask`。App 内已包含该数据。

## 歌词工具

点击窗口顶部的“歌词工具”（⌘2），或在转换队列的文件上右键选择“查找这首歌的歌词”。⌘1 返回音乐解锁。

1. 选择网易云音乐或 QQ 音乐，按歌曲、专辑、歌单搜索。支持名称、歌曲 ID，以及 `music.163.com` / `y.qq.com` 完整链接；分享文字中夹带的完整链接也能识别。
2. 点击歌曲查看原文、平台译文和平台罗马音；在原生编辑区直接修改。编辑结果在当前 App 会话中按歌曲保留，重新获取会替换修改。打开本地 LRC / TXT 可离线编辑转换。
3. 在“导出设置”选择内容、逐行交错 / 同行合并 / 分段排列、时间偏移、LRC 精度、文件名、编码和输出目录。
4. 导出当前歌词，或勾选多首批量导出。支持 **LRC、SRT、TXT、VTT**，UTF-8 / UTF-8 BOM / UTF-16 LE。同名文件自动编号；逐首显示失败原因，导出过程中可停止。

默认歌词输出目录：`~/Downloads/音匣输出/歌词`。⌘F 聚焦歌词搜索，⌘S 导出当前歌词，歌词工作区中 ⌘O 打开本地歌词。

### 无需手动获取 Cookie

- 公开搜索和歌词优先免登录，直接请求音乐平台；没有第三方 API 服务或本地后台服务。
- 网易云音乐：点击“账号登录 → 登录网易云音乐”，手机扫码并确认；原生二维码失效时可切换官方登录页。
- QQ 音乐：点击“账号登录 → 登录 QQ 音乐”，在系统 WebKit 中完成官方登录。音匣观察平台返回的有效音乐登录会话，自动保存并关闭授权窗口。
- 会话保存在 **macOS 系统钥匙串**，官方页面自身数据保存在系统 WebKit 数据存储中。再次打开 App 自动使用有效会话，过期后重新登录即可。
- “账号登录”菜单可重新登录或退出。退出清除钥匙串中的会话和该平台的 WebKit 登录数据。

扫码授权需要使用者自己的账号在手机确认；自动验证不会代替用户完成授权。平台风控、区域和账号权限可能影响个别内容，界面会显示具体错误。

### 歌词功能范围

原项目仓库仅有 `MusicLyricApp.exe` 与配置文件。此处参照其功能流程独立使用 Swift 实现，没有运行或打包原 Windows 程序。

- 平台提供的原文、译文、罗马音；中文拼音由 Apple 系统文字转换生成，多音字可编辑校正。
- 网易云提供 YRC 时可导出逐字 LRC；需仅选原文，平台缺少逐字内容会提示，不会生成假逐字时间。
- SRT / VTT 自动将同一时间的多语言歌词合成一条字幕，保留空歌词行作为字幕结束边界。无时间戳的 TXT 不能直接导出字幕。
- 暂不移植原软件依赖外部插件的 QQ 逐字歌词解密，以及需要另配服务密钥的百度 / 彩云机器翻译。平台没有提供的译文可手动填入。
- 歌词编辑保存在内存中；需要留存请导出。导出设置与目录跨启动记忆。

## 支持范围

| 来源 | 格式 |
| --- | --- |
| 网易云 | NCM、UC 缓存 |
| QQ 音乐 / Moo | QMC 静态、Map、RC4；QTag 与旧版内嵌 EKey；QMC0/2/3、QMCFLAC、QMCOGG、MFLAC/0、MGG/0/1/L、TKM、部分 BKC |
| QQ iOS | TM0/2/3/6 |
| 酷我 | 老版 KWM |
| 酷狗 | KGM/KGMA/VPR v3，需要内置 mask 支持的文件长度 |
| 虾米 | XM |

这是解封装工具，不是音频转码器；输出沿用原文件的音频编码。
新版外置 EKey / STag、未知新版加密格式、JOOX 和 QQ cache 数据库未移植。
音频转换没有联网补全，NCM 的内嵌封面与标签本地恢复，已有音频标签保留。
歌词工具仅在使用时直接连接音乐平台；没有更新检查或遥测。

## 验证

```sh
swift test --build-system native
python3 scripts/smoke-test.py
# 可选：真实平台搜索、歌曲/专辑/歌单、歌词和二维码接口检查，不进行账号登录
MUSICUNLOCK_LIVE_TESTS=1 swift test --build-system native --filter LyricsTests/testLiveFirstPartySearchResolveLyricsAndQR
```

测试读取仓库 `../../tests/fixtures/qmc` 中保留的参考数据，
验证五组 QQ 音乐样本逐字节一致，并验证 NCM、标签写入、坏文件和输出文件名冲突处理。
App 工作流测试覆盖文件夹递归扫描、去重、转换、失败重试和偏好记忆。
歌词测试覆盖双语字幕边界、多时间戳、逐字 LRC、时间偏移、编码与同名文件保护，
以及原生编辑缓存、批量导出、设置持久化和有效音乐会话识别。
打包后的 CLI 冒烟测试在 `build/` 下创建独立临时目录验证实际导出，结束后自动清理。

可离屏渲染原生 SwiftUI 视图，生成转换队列与歌词工作台的浅色、深色、空态和紧凑窗口预览：

```sh
MUSICUNLOCK_RENDER_DIR="$PWD/build/previews" ./build/音匣.app/Contents/MacOS/MusicUnlock
```

这是本 App 自身视图的离屏布局渲染，不是桌面截图。离屏模式以普通表面替代玻璃，
用于检查文字、列表和尺寸；玻璃的实时反射与折射以运行中的窗口为准。
设计决策和参考见 [DESIGN.md](DESIGN.md)，项目采用 [MIT 许可证](../../LICENSE)，
算法来源和第三方声明见 [THIRD_PARTY_NOTICES.txt](../../THIRD_PARTY_NOTICES.txt)。
