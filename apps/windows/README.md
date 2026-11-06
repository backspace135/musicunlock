# 音匣 Windows

Windows 原生版本的开发目录，与 macOS 版本在同一个 Git 仓库维护。

**当前状态：规划中，尚未创建工程或实现功能。**

## 技术方向

- C++20 + C++/WinRT + WinUI 3 / XAML，使用原生界面。
- 沿用 macOS 版的品牌色、布局与双工作区；使用 Windows 系统材质、控件和交互。
- 原生音频解封装引擎，使用 `../../tests/fixtures/qmc/` 中的相同测试向量验证；公共 KGM 数据位于 `../../resources/kgm.mask`。
- 歌词搜索、编辑与导出，优先使用原生扫码授权和 Windows 系统凭据保护。
- 自包含分发，用户无需额外安装 .NET 或 Windows App Runtime；必要原生库随程序携带。
- 原生扫码能否覆盖 QQ 音乐登录需要在 Windows 上验证，不能假定浏览器授权自动提供 App 会话。

## 目录边界

Windows 工程、源码、测试与打包脚本均放在此目录。构建输出、IDE 缓存和签名私钥不提交。
开发验证需要 Windows 环境，重点检查真实账号授权、音频兼容性、资源占用和干净系统部署。

macOS 版本见 [../macos/](../macos/)。

项目代码采用根目录的 [MIT 许可证](../../LICENSE)，算法衍生部分和数据保留 [第三方声明](../../THIRD_PARTY_NOTICES.txt)。
