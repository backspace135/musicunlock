# 音匣 Android

Android 原生版本的开发目录，与 macOS、Windows 版本在同一个 Git 仓库维护。

**当前状态：工程骨架。App 只有一个显示“音匣”的占位页面；NDK 编译 `shared/core` 和核心测试在后续提交中加入。**

## 技术方向

- Kotlin + Jetpack Compose；业务逻辑放在 `shared/core`（C++，与 Windows 共用），Kotlin 只做界面和平台实现。
- minSdk 26，compileSdk / targetSdk 37，只打 arm64-v8a。
- 只用黑白灰，状态靠图标和文字区分，不靠颜色。

## 目录

```text
apps/android/
├── app/           应用模块（io.github.backspace135.musicunlock），Compose 界面
├── core/          Android 库模块：shared/core 的 JNI 桥接、Kotlin 封装和平台实现
├── gradle/        Gradle wrapper 与版本目录 libs.versions.toml
└── scripts/       build.py：编译 APK
```

## 环境要求

- JDK 21
- Android SDK：`platforms;android-37.0`、`build-tools`、`platform-tools`；在模拟器上运行还需要
  `emulator` 和 `system-images;android-37.0;google_apis;arm64-v8a`
- Python 3.10+

`build.py` 优先使用环境变量 `JAVA_HOME` 和 `ANDROID_HOME`，没有设置时：

| 主机 | JDK | Android SDK |
| --- | --- | --- |
| macOS | `~/jdk/jdk-21.0.11+10/Contents/Home` | `~/Library/Android/sdk` |
| Windows | 必须设置 `JAVA_HOME` | `%LOCALAPPDATA%\Android\Sdk` |

Windows 主机上 Gradle 和 SDK 工具的缓存（`GRADLE_USER_HOME`、`ANDROID_USER_HOME`）放在 `build/` 下，不写用户目录。

## 构建

在仓库根目录运行（Windows 用 `python`）：

```sh
python3 apps/android/scripts/build.py                   # debug APK
python3 apps/android/scripts/build.py --config release  # release APK（未签名）
```

产物在 `apps/android/build/app/outputs/apk/<debug|release>/`。所有构建产物、临时文件都写在 `apps/android/build/`，
不提交到 Git。

装到正在运行的模拟器上：

```sh
adb install -r apps/android/build/app/outputs/apk/debug/app-debug.apk
adb shell am start -n io.github.backspace135.musicunlock/.MainActivity
```
