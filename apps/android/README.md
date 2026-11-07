# 音匣 Android

Android 原生版本的开发目录，与 macOS、Windows 版本在同一个 Git 仓库维护。

**当前状态：工程骨架。App 只有一个显示“音匣”的占位页面；`:core` 用 NDK 把 `shared/core` 编进 `libmusicunlock.so`，
JNI 桥接在阶段 2 加入。**

## 技术方向

- Kotlin + Jetpack Compose；业务逻辑放在 `shared/core`（C++，与 Windows 共用），Kotlin 只做界面和平台实现。
- minSdk 26，compileSdk / targetSdk 37，只打 arm64-v8a。
- 只用黑白灰，状态靠图标和文字区分，不靠颜色。

## 目录

```text
apps/android/
├── app/           应用模块（io.github.backspace135.musicunlock），Compose 界面
├── core/          Android 库模块：shared/core 的 JNI 桥接、Kotlin 封装和平台实现
│   └── src/main/cpp/  CMake 工程：add_subdirectory 引入 shared/core，产出 libmusicunlock.so 和核心测试
├── gradle/        Gradle wrapper 与版本目录 libs.versions.toml（NDK、CMake 的版本也写在这里）
└── scripts/       build.py：编译 APK，在主机和模拟器上跑核心测试
```

## 环境要求

- JDK 21
- Android SDK：`platforms;android-37.0`、`platform-tools`；在模拟器上运行还需要
  `emulator` 和 `system-images;android-37.0;google_apis;arm64-v8a`
- build-tools、NDK 和 CMake：版本固定在 `gradle/libs.versions.toml`（`buildTools`、`ndk`、`cmake`），用 sdkmanager
  装进上面的 SDK：

  ```sh
  sdkmanager "build-tools;36.0.0" "ndk;30.0.16248370" "cmake;4.1.2"
  ```

  CMake 自带 Ninja；`build.py` 编核心测试时也用这两个版本，不用系统里的。
- 在主机上跑核心测试需要 C++20 编译器（Mac 用 Xcode 命令行工具的 clang）
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
python3 apps/android/scripts/build.py                   # debug APK + 主机上的核心测试
python3 apps/android/scripts/build.py --config release  # release APK（未签名）+ 主机上的核心测试
python3 apps/android/scripts/build.py --no-test         # 只编 APK
python3 apps/android/scripts/build.py --device --serial emulator-5554  # 另外在 arm64 模拟器上跑核心测试
```

产物在 `apps/android/build/app/outputs/apk/<debug|release>/`。所有构建产物、临时文件都写在 `apps/android/build/`，
不提交到 Git。

装到正在运行的模拟器上（连着多台设备时用 `-s` 指定）：

```sh
adb -s emulator-5554 install -r apps/android/build/app/outputs/apk/debug/app-debug.apk
adb -s emulator-5554 shell am start -n io.github.backspace135.musicunlock/.MainActivity
```

## 核心测试

`shared/core` 的 `musicunlock_core_tests` 有两种跑法，都由 `build.py` 用固定版本的 CMake 编译 `core/src/main/cpp`：

- **主机**（默认）：Mac 和 Linux 上用 Android 同款的纯 C++ AES（`shared/core/src/platform/portable/`，CMake 选项
  `MU_PORTABLE_AES`）编译运行，产物在 `build/core-tests-host/`。Windows 主机跳过这一步，那里的核心测试由
  `apps/windows/scripts/build.py` 负责。
- **模拟器**（`--device`）：用 NDK 交叉编译成 arm64 可执行文件（`build/core-tests-android/`），和 `tests/fixtures/qmc`
  一起推到 `/data/local/tmp/musicunlock-core-tests/` 运行，结束后删除。只接受 `emulator-*` 且 `ro.kernel.qemu` 为 1
  的设备，不会碰连着的真机；没给 `--serial` 时用 `ANDROID_SERIAL`，再没有就要求恰好只有一台模拟器在线。

Android 和 Linux 上暂时没有平台文字转换（拼音、GBK），`platform_text` 是返回 `std::nullopt` 的占位实现，相关检查在
测试里跳过；阶段 2 用 JNI 实现后恢复。

## CI

`.github/workflows/android.yml` 在 Ubuntu 上装固定版本的 NDK 和 CMake，编 debug / release APK，并在主机上跑核心测试
（portable AES），上传 debug APK。改动 `apps/android/`、`shared/`、`tests/fixtures/` 或 `resources/kgm.mask` 时触发。
