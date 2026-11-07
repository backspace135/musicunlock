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
└── scripts/       build.py：编译 APK、跑核心测试；device.py：装到设备和调试；setup_toolchain.py：安装工具链；
                   android_env.py：三者共用的环境设置
```

## 环境要求

- Python 3.10+
- JDK 21、Android SDK（`platforms;android-37.0`、`platform-tools`），以及固定在 `gradle/libs.versions.toml` 里的
  build-tools、NDK 和 CMake（`buildTools`、`ndk`、`cmake`）。CMake 自带 Ninja；`build.py` 编核心测试时也用这两个
  版本，不用系统里的。都由 `scripts/setup_toolchain.py` 安装，见下一节。
- 在模拟器上运行还需要 `emulator` 和 `system-images;android-37.0;google_apis;arm64-v8a`（只在 Mac 上用）
- 在主机上跑核心测试需要 C++20 编译器（Mac 用 Xcode 命令行工具的 clang）

## 工具链

```sh
python apps/android/scripts/setup_toolchain.py             # Windows：装仓库内工具链
python3 apps/android/scripts/setup_toolchain.py            # Mac：把缺的包装进 ~/Library/Android/sdk
python3 apps/android/scripts/setup_toolchain.py --in-repo  # Mac / Linux：也改用仓库内工具链
```

可以重复运行，已经装好的部分直接跳过。

**仓库内工具链**放在**主检出**的 `build/android-toolchain/`（`git rev-parse --git-common-dir` 的上一级），同一个仓库的
所有工作树共用一份，NDK 只下载一次，调试签名和 adb 配对也只有一套：

| 目录 | 内容 |
| --- | --- |
| `jdk-21.0.11+10/` | Temurin JDK 21，下载后按脚本里写死的 SHA-256 校验 |
| `sdk/` | Android SDK：命令行工具 23.0（同样校验 SHA-256），再由它装 platform-tools 和版本目录里固定的包，许可由脚本同意 |
| `gradle-home/` | `GRADLE_USER_HOME`：Gradle 发行版、依赖缓存、守护进程 |
| `android-user-home/` | `ANDROID_USER_HOME`：调试签名 `debug.keystore`、SDK 工具的设置和 Android CLI |
| `adb-home/` | adb 的日志；Mac / Linux 上 adb 的 `HOME`，密钥在 `adb-home/.android/` |
| `downloads/` | 下载的压缩包 |
| `tmp/` | `TMP` / `TEMP` |

`scripts/android_env.py` 是 `build.py`、`device.py`、`setup_toolchain.py` 共用的环境设置：Windows 上总是用仓库内
工具链；Mac 和 Linux 上装过 `--in-repo` 就用它，否则用 `JAVA_HOME` / `ANDROID_HOME`，没有设置时用
`~/jdk/jdk-21.0.11+10/Contents/Home` 和 `~/Library/Android/sdk`。用仓库内工具链时 Kotlin 在 Gradle 进程里编译
（单独的 Kotlin 守护进程会往用户的应用数据目录写文件），临时文件（`TMP` / `TEMP`）也放在工具链的 `tmp/`：
所有工作树共用一个 Gradle 守护进程，它会一直占着启动时那个临时目录里的文件，放在工作树里就删不掉那个工作树。
不用仓库内工具链时，临时文件在当前检出的 `apps/android/build/tmp/`。环境变量 `MUSICUNLOCK_ANDROID_TOOLCHAIN` 可以把工具链换到别的目录。

`build.py` 和 `setup_toolchain.py` 开头会打印 `JAVA_HOME`、`ANDROID_HOME`、`GRADLE_USER_HOME`、`ANDROID_USER_HOME`、
`TMP`，确认缓存位置看这几行（或 Gradle `--info` 输出里的路径）即可。

adb 不认 `ANDROID_USER_HOME`。Mac / Linux 上脚本只给 adb 子进程改 `HOME`；Windows 上 adb 通过系统接口
（`SHGetFolderPath(CSIDL_PROFILE)`）取用户目录，`USERPROFILE` 等环境变量都改不了，它的密钥和配置只能放在
`%USERPROFILE%\.android\`（g16win 上的例外见下文）。adb 的后台服务从工具链目录启动，日志也写在那里，不会占住
要删除的工作树。

## 构建

在仓库根目录运行（Windows 用 `python`）：

```sh
python3 apps/android/scripts/build.py                   # debug APK + 主机上的核心测试
python3 apps/android/scripts/build.py --config release  # release APK（未签名）+ 主机上的核心测试
python3 apps/android/scripts/build.py --no-test         # 只编 APK
python3 apps/android/scripts/build.py --device --serial emulator-5554  # 另外在 arm64 模拟器上跑核心测试
python3 apps/android/scripts/build.py --suffix unlock   # debug 版带后缀：io.github.backspace135.musicunlock.unlock
```

产物在 `apps/android/build/app/outputs/apk/<debug|release>/`。所有构建产物、临时文件都写在 `apps/android/build/`，
不提交到 Git。

## 在设备上安装和调试

`scripts/device.py` 通过 adb 操作手机或模拟器（在仓库根目录运行；Windows 用 `python`）：

```sh
python3 apps/android/scripts/device.py devices                    # 已连接的设备
python3 apps/android/scripts/device.py pair <ip:port> <配对码>     # 无线调试首次配对（手机：开发者选项 → 无线调试 → 使用配对码配对）
python3 apps/android/scripts/device.py connect <ip:port>          # 连接已配对的手机
python3 apps/android/scripts/device.py install                    # 编当前分支的 debug 版，安装并启动
python3 apps/android/scripts/device.py uninstall                  # 卸载当前分支装的那一份
python3 apps/android/scripts/device.py logcat [--dump]            # 只看当前分支那一份 App 的日志；--dump 输出现有日志后退出
python3 apps/android/scripts/device.py screenshot <名字>           # 截图存到 build/screenshots/<名字>.png
python3 apps/android/scripts/device.py test                       # instrumented tests，然后是 musicunlock_core_tests
python3 apps/android/scripts/device.py pull <手机路径> build/<目录>/  # 只能拉到仓库根目录的 build/ 下
python3 apps/android/scripts/device.py shell -- <命令>             # 在设备上运行命令
```

- **每个分支装自己的一份**：分支名最后一段做 applicationIdSuffix 和桌面名称，`android/unlock` 装成
  `io.github.backspace135.musicunlock.unlock`，显示为“音匣 (unlock)”，几个会话可以同时装在一台手机上。
  不合法的字符换成 `_`（`android/device-survey` → `.device_survey`）。`--suffix` 可以直接指定；工作树处于
  detached HEAD 时用环境变量 `MUSICUNLOCK_ANDROID_BRANCH` 给出分支名（`remote.py` 会设置）。release 版不加后缀。
- 连着多台设备时用 `--serial` 指定（放在子命令前面，如 `device.py --serial emulator-5554 install`），
  否则用 `ANDROID_SERIAL`，再没有就要求恰好只有一台在线。
- `logcat` 按 App 的 uid 过滤（Android 11 以下按当前进程）。
- `test` 用 Gradle 的 `connectedDebugAndroidTest`（只在选中的设备上跑），核心测试推到
  `/data/local/tmp/musicunlock-core-tests-<后缀>/`，跑完删除。Gradle 跑完 instrumented tests 会卸载它装的 App，
  之后需要的话重新 `install`。
- 小米手机安装时报 `INSTALL_FAILED_USER_RESTRICTED`：需要在手机上确认，或者打开“开发者选项 → USB 安装”。

## 在 g16win 上远程编译

**访问范围的唯一例外（2026-11-07 用户批准）：** g16win 上只能动 `D:\project\musicunlock` 和开发工具链，缓存、下载、
临时文件都在仓库 `build/` 下。adb 是例外：Windows 版 adb 只认用户目录，允许它在 `%USERPROFILE%\.android\` 下写
配对密钥（`adbkey`、`adbkey.pub`）和它自己的少量配置（如无线调试记录 `adb_known_hosts.pb`）。这个目录里的其他
东西不读、不改，也不要手动查看或清理它；其余访问规则不变。

Mac 上的会话可以用 `apps/windows/scripts/remote.py` 把当前工作区同步到 g16win 的独立工作树
（`D:\project\musicunlock\.worktrees\<分支>`），在那里编译、装到连着 g16win 的手机：

```sh
python3 apps/windows/scripts/remote.py android-setup               # 在 g16win 上装仓库内工具链（所有工作树共用）
python3 apps/windows/scripts/remote.py android-build [-- --no-test]  # build.py
python3 apps/windows/scripts/remote.py android-install             # device.py install，后缀取 Mac 上的分支名
python3 apps/windows/scripts/remote.py android-screenshot <名字>    # 截图并取回到 Mac 的 build/screenshots/
python3 apps/windows/scripts/remote.py android-device -- logcat --dump  # 其他 device.py 命令
python3 apps/windows/scripts/remote.py remove                      # 用完删掉 g16win 上的工作树
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
