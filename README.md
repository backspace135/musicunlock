# 音匣 MusicUnlock

音乐解封装与歌词工具。macOS 和 Windows 版本在同一个仓库维护，各自使用原生界面与系统集成。

## 平台状态

| 平台 | 状态 | 技术 | 入口 |
| --- | --- | --- | --- |
| macOS | 已实现，0.2.0 | Swift / SwiftUI / Apple Liquid Glass | [构建与使用说明](apps/macos/README.md) |
| Windows | 规划中，尚无可运行版本 | 计划采用 C++20 / C++/WinRT / WinUI 3 | [开发范围与技术方向](apps/windows/README.md) |

## 目录结构

```text
musicunlock/
├── apps/
│   ├── macos/                 macOS App、CLI、Swift 核心库与测试
│   │   ├── Sources/
│   │   ├── Tests/
│   │   ├── assets/
│   │   ├── scripts/
│   │   └── Package.swift
│   └── windows/               Windows 原生版本目录（待实现）
├── resources/                公共运行资源（KGM 数据表）
├── tests/fixtures/            跨平台解密测试向量
├── LICENSE                   项目 MIT 许可证
├── THIRD_PARTY_NOTICES.txt    算法与数据来源、第三方许可
└── README.md
```

平台源码、测试和打包脚本归各自目录管理。构建产物写入对应平台的 `build/`，不提交到 Git。
解密逻辑已移植为原生 Swift 实现。仓库不保留上游参考源码，仅保留必需数据和测试向量；克隆后即可构建 macOS 版。

## macOS 快速开始

需要 macOS 26+、Xcode 26+。

在 `apps/macos/` 目录运行：

```sh
python3 scripts/build-app.py
```

产物为 `apps/macos/build/音匣.app` 和 `apps/macos/build/MusicUnlock-macOS-arm64.zip`。

功能包括：

- 本地批量解封装 NCM、QMC、MFLAC、MGG 等音频，保留原有编码。
- 网易云音乐 / QQ 音乐搜索，歌曲、专辑和歌单完整链接解析。
- 原生歌词编辑、单首与批量导出 LRC / TXT / SRT / VTT。
- 网易云扫码及官方账号授权，自动保存登录会话，无需手动复制 Cookie。

测试同样在 `apps/macos/` 目录运行：

```sh
swift test --build-system native
python3 scripts/smoke-test.py
```

真实平台接口检查、登录方式、支持格式和功能限制见 [macOS README](apps/macos/README.md)。

## Windows 开发

Windows 版与 macOS 版共用产品设计和测试参考，但使用独立的原生平台实现。
当前仅建立目录与开发范围，不包含占位可执行文件。后续 Windows 源码、工程、测试和发布脚本放在 `apps/windows/`。

## 许可证与来源

本项目自行编写的代码采用 [MIT 许可证](LICENSE)。

音频解密算法移植自 Unlock Music，原生运行不调用上游 TypeScript。KGM 数据与解密测试向量保留在
[resources/](resources/README.md) 和 [tests/fixtures/](tests/fixtures/README.md)。
上游 MIT 许可和衍生 TEA 代码的 BSD-3-Clause 声明均保留在 [THIRD_PARTY_NOTICES.txt](THIRD_PARTY_NOTICES.txt)，并随 App 分发。

歌词工作流参照 [MusicLyricApp](https://github.com/maqianjun-1/MusicLyricApp) 独立实现；未包含其 Windows 可执行文件、反编译源码或 DLL。
