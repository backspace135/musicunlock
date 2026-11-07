# 音匣 Android

Android 原生版本的开发目录，与 macOS、Windows 版本在同一个 Git 仓库维护。

**当前状态：App 只有一个显示“音匣”的占位页面；`:core` 用 NDK 把 `shared/core` 编进 `libmusicunlock.so`，并用 JNI
把核心封装成 Kotlin API（见下文“架构”和“调用核心”）。**

## 技术方向

- Kotlin + Jetpack Compose；业务逻辑放在 `shared/core`（C++，与 Windows 共用），Kotlin 只做界面和平台实现。
- minSdk 26，compileSdk / targetSdk 37，只打 arm64-v8a。
- 只用黑白灰，状态靠图标和文字区分，不靠颜色。

## 目录

```text
apps/android/
├── app/           应用模块（io.github.backspace135.musicunlock），Compose 界面
├── core/          Android 库模块：shared/core 的 JNI 桥接、Kotlin 封装和平台实现
│   ├── src/main/cpp/     CMake 工程：add_subdirectory 引入 shared/core，产出 libmusicunlock.so 和核心测试；*_jni.cpp 是桥接
│   ├── src/main/kotlin/  Kotlin API，包名 io.github.backspace135.musicunlock.core
│   ├── src/androidTest/  instrumented tests（在模拟器上跑）
│   └── consumer-rules.pro  R8 保留清单：C++ 按名字调用的 Kotlin 类和方法
├── gradle/        Gradle wrapper 与版本目录 libs.versions.toml（NDK、CMake 的版本也写在这里）
└── scripts/       build.py：编译 APK，在主机和模拟器上跑核心测试
```

## 架构

```text
界面（:app，Compose）
  │  只调用 io.github.backspace135.musicunlock.core 里的 Kotlin API
  ▼
Kotlin API（:core/src/main/kotlin）  ── 平台实现：MediaStore / SAF、OkHttp、Android 密钥库、ICU
  │  JNI（:core/src/main/cpp/*_jni.cpp）
  ▼
shared/core（C++，和 Windows 共用）：解封装、队列、歌词、登录规则和全部界面文案
```

- **JNI 约定**：`JNI_OnLoad` 用 `RegisterNatives` 按名字注册每个 Kotlin 类的 `native*` 方法，并缓存 C++ 要回调的类；
  名字对不上时加载即失败，不会拖到调用时才报错。字符串一律按 UTF-8 ↔ UTF-16 转换（不用 `NewStringUTF`，它会弄坏
  emoji）。结构化数据（队列行、歌曲列表）用 JSON 传。
- **异常**：每个 JNI 入口都包在 `guarded` 里，C++ 异常不会穿过 JNI 边界：`DecodeError` → `DecodeException`，
  `LyricError` → `LyricException`，`Cancelled` → `CoreCancelledException`（是 `CancellationException`），其他 →
  `CoreException`。`message` 就是核心的原始中文提示，直接显示。Kotlin 回调抛出的异常按同样的对应关系变回 C++ 异常。
- **线程**：`AudioQueue` 和核心一样不是线程安全的，只能在创建它的线程（主线程）上调用，换线程调用会抛
  `IllegalStateException`。耗时的调用（转换、扫描、网络）都是 `suspend` 函数，内部切到 `Dispatchers.IO`。C++
  回调 Kotlin 时，如果当前线程是 C++ 自己开的，会先挂到 JVM 上，结束时卸下（`jni_support.h` 的 `ThreadEnv`）。
- **初始化**：`:core` 的 Manifest 里有一个 `CoreInitializer`（ContentProvider），进程启动时把 Context 交给核心，
  并清理上次运行留下的缓存。功能代码不用调用任何初始化函数。

### 文件：`content://` URI 和核心的路径接口

核心按文件路径工作，Android 给的是 `content://` URI。方案是“路径只当身份，读写走文件描述符和 MediaStore / SAF”：

- **导入**（`DocumentImporter`）：每个文档按 URI 推导出一个稳定路径
  `<cacheDir>/musicunlock/<本次运行>/imports/<URI 的 SHA-256 前 24 位>/<显示名>`，和大小一起交给核心的
  `AudioQueue.add`。同一个 URI 加两次得到同一个路径，核心提示“这些文件已在队列中。”；显示名保证行上的标题、
  格式和原文件一致，大小来自文档提供方。从文件选择器、文件夹选择器来的文档不复制，路径下面没有文件。
- **递归导入文件夹**：用 `DocumentsContract` 遍历 `OpenDocumentTree` 给的目录，规则和 `import.cpp` 一致：跳过以“.”
  开头的项（文件夹不进入）、读不了的文件夹忽略、只收 `is_supported_extension` 认的扩展名、其他文件计入跳过、
  按文件夹内路径用 `natural_less` 排序（这两个函数都经 JNI 调核心）。
- **分享进来的文件**（`ACTION_SEND` / `ACTION_SEND_MULTIPLE` / `ACTION_VIEW`）：发送方给的读取授权随接收它的
  Activity 结束，所以先复制到上面那个路径。转换成功、移出队列或清空后删除，下次启动时也会清掉。
- **转换**（`AudioConverter`）：按路径找回 URI，用 `ParcelFileDescriptor` 打开，把文件描述符交给 C++。普通文件用
  `mmap` 映射（不在 Java 和 C++ 之间拷贝，也不占 App 自己的堆），管道（云端文档）才分块读入。核心的
  `convert_data` 把结果写进一个空的暂存目录 `<cacheDir>/musicunlock/<本次运行>/staging/<随机>/`，再发布到输出位置，
  最后删除暂存目录。
- **输出**：默认 `Music/音匣`（歌词默认 `Download/音匣/歌词`），Android 10 起用 MediaStore 写入，不需要权限；
  Android 9 及以下直接写共享存储（需要 `WRITE_EXTERNAL_STORAGE`，见 `OutputFolders.requiredPermissions`）。用户也可以
  用 `OpenDocumentTree` 选一个文件夹（SAF）。同名文件按核心的规则编号：依次尝试 `numbered_file_name` 给出的
  `名字.ext`、`名字 (1).ext`……，每个名字创建后检查实际得到的名字，被 MediaStore 或文档提供方改过名就删掉、试下一个，
  所以不会用 MediaStore 自己的改名规则，也不会覆盖已有文件。
- **缓存清理**：每次运行用自己的 `<本次运行>` 目录，启动时删除其他运行留下的目录；所以转换失败、App 被杀后残留的
  暂存文件和复制件，下次启动就没了。
- **内存**：120 MB 的 NCM（FLAC，带标签和封面，最费内存的情况）转换时，App 的匿名内存峰值增加约 2 倍文件大小
  （解密后的音频 + 写回标签时的一份拷贝；Android 17 模拟器上 RssAnon 为 2.0 倍，Android 9 上按 native heap 计 2.2 倍）。
  源文件是映射的，算文件页，不算进去（`LargeFileTest`）。QMC 等不写回标签的格式只有 1 倍。

## 调用核心：`:core` 的 Kotlin API

功能代码只用 `io.github.backspace135.musicunlock.core` 包里的公开类，不碰 JNI。下面每段都能直接照抄；完整说明在各个
类的 KDoc 里。规则：

- 界面上的文字（状态、提示、标签）一律用 API 返回的核心文案，不在 Kotlin 里自己拼。
- `suspend` 函数自己会切到后台线程，在 `viewModelScope` / 应用级 `CoroutineScope` 里直接调用。
- 失败时抛 `CoreException`（或子类），`error.message` 就是要显示的中文。`CoreCancelledException` 表示被取消，什么都
  不用显示。

### 解封装（#31）

| 类 | 作用 |
| --- | --- |
| `DocumentImporter` | 把选择器 / 分享给的 URI 变成 `ImportScan`（`scanDocuments`、`scanTree`、`scanIntent`） |
| `AudioQueue` | 核心的转换队列：任务、筛选、批次、全部文案。只在主线程用 |
| `AudioConverter` | `convert(source, folder, naming)`：转换一个文件并写到输出文件夹 |
| `OutputFolder` / `OutputFolders` | 输出位置（默认 `Music/音匣` 或用户选的文件夹）、名字和位置、权限、“在文件管理中查看” |
| `UnlockSettings` | `outputDirectory` / `namingPolicy` 设置，键名和 Windows 一致 |
| `AudioFormats` | `isSupportedExtension`、`naturalOrder`、`formatFileSize` 等核心函数 |
| `NamingPolicy` / `JobState` / `QueueFilter` | 枚举，`label` 是核心的文案 |

**导入**：

```kotlin
// 文件：加密格式没有 MIME 类型，所以选全部文件，由核心判断扩展名。
val pickFiles = rememberLauncherForActivityResult(ActivityResultContracts.OpenMultipleDocuments()) { uris ->
    scope.launch { notice = queue.add(DocumentImporter.scanDocuments(uris)) }
}
pickFiles.launch(arrayOf("*/*"))

// 文件夹：递归扫描，规则和 import.cpp 一致。扫描期间显示“正在扫描文件…”。
val pickFolder = rememberLauncherForActivityResult(ActivityResultContracts.OpenDocumentTree()) { tree ->
    if (tree != null) scope.launch {
        scanning = true
        notice = queue.add(DocumentImporter.scanTree(tree))
        scanning = false
    }
}

// 从其他 App 分享 / 打开（在 Activity 的 onCreate 和 onNewIntent 里）：
lifecycleScope.launch {
    DocumentImporter.scanIntent(intent)?.let { scan -> notice = queue.add(scan) }
}
```

`AndroidManifest.xml` 里接收分享和打开（`:app` 的 Activity）：

```xml
<intent-filter>
    <action android:name="android.intent.action.SEND" />
    <action android:name="android.intent.action.SEND_MULTIPLE" />
    <category android:name="android.intent.category.DEFAULT" />
    <data android:mimeType="*/*" />
</intent-filter>
<intent-filter>
    <action android:name="android.intent.action.VIEW" />
    <category android:name="android.intent.category.DEFAULT" />
    <data android:scheme="content" android:mimeType="*/*" />
</intent-filter>
```

**队列和转换循环**：`AudioQueue` 不是线程安全的，只在主线程调用；转换放在 `AudioConverter.convert`（内部切到
后台），一次一个。队列要活过 Activity 重建（包括“不保留活动”），所以放在应用级的对象里，不要放在 ViewModel：

```kotlin
/** 应用级：持有队列，跑转换批次。ViewModel 只读它、转发按钮。 */
object UnlockSession {
    val queue = AudioQueue()                 // 第一次访问时在主线程创建
    private val scope = MainScope()          // 主线程；转换本身由 AudioConverter 切到后台
    var notice by mutableStateOf("")         // 提示横幅：add() / endBatch() 的返回值

    fun start(context: Context) {
        if (queue.running || queue.count(JobState.WAITING) == 0) return
        val (folder, naming) = UnlockSettings.values.value   // 开始时记下，本批不变
        if (!OutputFolders.isUsable(folder)) {
            notice = "无法写入输出文件夹：" + OutputFolders.label(folder).name
            return
        }
        val ids = queue.beginBatch()
        notice = ""
        val task = ProcessingService.Task.UNLOCK
        ProcessingService.start(context, task, ProcessingService.Progress(queue.statusTitle, 0, ids.size))
        scope.launch {
            for (id in ids) {
                if (queue.stopRequested) break
                val job = queue.find(id) ?: continue          // 已被移出
                queue.jobStarted(id)
                ProcessingService.update(task, ProcessingService.Progress(queue.statusTitle, queue.batchFinished, queue.batchCount))
                try {
                    queue.jobSucceeded(id, AudioConverter.convert(job.source, folder, naming))
                } catch (error: CoreException) {
                    queue.jobFailed(id, AudioQueue.failureMessage(error))
                }
            }
            notice = queue.endBatch()
            ProcessingService.stop(context, task)
        }
    }

    init {
        // 通知里的“停止”按钮
        scope.launch {
            ProcessingService.stopRequests.collect { if (it == ProcessingService.Task.UNLOCK) stop() }
        }
    }

    fun stop() = queue.requestStop()
}
```

界面读队列：`queue.changes` 每次变化加一，收集它再读 `jobs` / `visible(filter)` / `statusTitle` / `statusDetail`：

```kotlin
val tick by UnlockSession.queue.changes.collectAsState()
val jobs = remember(tick, filter) { UnlockSession.queue.visible(filter) }
Text(UnlockSession.queue.statusTitle)          // “正在转换 2 / 5”
if (UnlockSession.queue.running) {
    LinearProgressIndicator(progress = {
        UnlockSession.queue.batchFinished.toFloat() / maxOf(1, UnlockSession.queue.batchCount)
    })
} else {
    Text(UnlockSession.queue.statusDetail)
}
// 每一行：job.displayTitle、job.subtitle、job.sourceFormat、job.state.label、job.detail（失败原因）、
// job.outputExtension.uppercase()；封面：if (job.hasCover) queue.cover(job.id)（JPEG / PNG 字节）。
```

其他按钮：`queue.remove(id)`、`queue.clearCompleted()`、`queue.clear()`、`queue.retryFailed()`（然后再 `start`）。
用完（App 退出前不需要）可以 `queue.close()` 释放 C++ 内存。

**前台服务**（`ProcessingService`，解封装和歌词导出共用）：开始时 `start`，每处理一个文件 `update`，结束或停止后
`stop`（见上面的循环）。Android 15 起类型是 `mediaProcessing`，更低版本是 `dataSync`，Manifest 里的声明和权限都在
`:core` 里，`:app` 不用再写。Android 13 起通知需要权限，第一次开始前申请一次，用户拒绝也照常转换：

```kotlin
val askNotifications = rememberLauncherForActivityResult(ActivityResultContracts.RequestPermission()) { start() }
val permission = ProcessingService.notificationPermission
if (permission != null && !asked) askNotifications.launch(permission) else start()
```

**打开和分享转换结果**：`job.outputUri` 是 `content://` URI：

```kotlin
val open = Intent(Intent.ACTION_VIEW).setDataAndType(job.outputUri, "audio/*")
    .addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION)
val share = Intent(Intent.ACTION_SEND).setType("audio/*").putExtra(Intent.EXTRA_STREAM, job.outputUri)
    .addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION)
context.startActivity(Intent.createChooser(share, null))
```

**输出文件夹和命名设置**：

```kotlin
val settings by UnlockSettings.values.collectAsState()
val label = OutputFolders.label(settings.outputFolder)  // label.name = “音匣”，label.location = “内部存储/Music”
Text("输出到 ${label.name} · ${settings.naming.label}")

// “更改”：选文件夹并保留读写权限
val pickOutput = rememberLauncherForActivityResult(ActivityResultContracts.OpenDocumentTree()) { tree ->
    if (tree != null) UnlockSettings.setOutputFolder(OutputFolders.remember(tree, settings.outputFolder))
}

// 命名方式
NamingPolicy.entries.forEach { policy -> /* 单选项文字：policy.label */ UnlockSettings.setNaming(policy) }

// “在文件管理中查看”
try {
    context.startActivity(OutputFolders.viewIntent(settings.outputFolder))
} catch (_: ActivityNotFoundException) {
    notice = "请在文件管理中打开 ${OutputFolders.path(settings.outputFolder)}"
}

// Android 9 及以下写默认文件夹需要存储权限，开始转换前申请（其他版本返回空列表）：
val needed = OutputFolders.requiredPermissions(settings.outputFolder)
```

## 环境要求

- JDK 21
- Android SDK：`platforms;android-37.0`、`build-tools`、`platform-tools`；在模拟器上运行还需要
  `emulator` 和 `system-images;android-37.0;google_apis;arm64-v8a`
- NDK 和 CMake：版本固定在 `gradle/libs.versions.toml`（`ndk`、`cmake`），用 sdkmanager 装进上面的 SDK：

  ```sh
  sdkmanager "ndk;30.0.16248370" "cmake;4.1.2"
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

- **instrumented tests**（`--device`，在核心测试之后）：`:core:connectedDebugAndroidTest`，测 JNI 桥接和平台实现。
  Gradle 的 connected 任务默认装到所有在线设备上，`build.py` 用 `ANDROID_SERIAL` 把它限定在选中的模拟器。

平台文字转换（拼音、GBK）在 App 里由 JNI 调 Android 的 API 实现（`platform_text_jni.cpp` → `PlatformText.kt`）：
拼音用 `android.icu.text.Transliterator` 的 `Han-Latin`（API 29 起，结果和 macOS、Windows 一致；API 26–28 返回
`std::nullopt`，核心按“不支持拼音”处理），GBK 用 `java.nio.charset` 的 GB18030。核心测试不在 JVM 里运行，链接的是
返回 `std::nullopt` 的占位实现，相关检查跳过，由 instrumented tests 覆盖。

## CI

`.github/workflows/android.yml` 在 Ubuntu 上装固定版本的 NDK 和 CMake，编 debug / release APK，并在主机上跑核心测试
（portable AES），上传 debug APK。改动 `apps/android/`、`shared/`、`tests/fixtures/` 或 `resources/kgm.mask` 时触发。
