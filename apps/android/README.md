# 音匣 Android

Android 原生版本的开发目录，与 macOS、Windows 版本在同一个 Git 仓库维护。

**当前状态：界面基础、组件和应用外壳已完成，两个工作区和账号页还是假数据的占位页面；`:core` 用 NDK 把 `shared/core`
编进 `libmusicunlock.so`，并用 JNI 把核心封装成 Kotlin API（见下文“架构”和“调用核心”）。**

## 技术方向

- Kotlin + Jetpack Compose；业务逻辑放在 `shared/core`（C++，与 Windows 共用），Kotlin 只做界面和平台实现。
- minSdk 26，compileSdk / targetSdk 37，只打 arm64-v8a。
- 只用黑白灰，状态靠图标和文字区分，不靠颜色。
- 界面的 token、动效参数、页面和文件约定、每个组件的用法见 [DESIGN.md](DESIGN.md)，写页面前先读。

## 目录

```text
apps/android/
├── app/           应用模块（io.github.backspace135.musicunlock），Compose 界面
│   └── src/debug/ 只在 debug 版里的组件演示与调参页
├── core/          Android 库模块：shared/core 的 JNI 桥接、Kotlin 封装和平台实现
│   ├── src/main/cpp/     CMake 工程：add_subdirectory 引入 shared/core，产出 libmusicunlock.so 和核心测试；*_jni.cpp 是桥接
│   ├── src/main/kotlin/  Kotlin API，包名 io.github.backspace135.musicunlock.core
│   ├── src/androidTest/  instrumented tests（在模拟器上跑）
│   └── consumer-rules.pro  R8 保留清单：C++ 按名字调用的 Kotlin 类和方法
├── gradle/        Gradle wrapper 与版本目录 libs.versions.toml（NDK、CMake 的版本也写在这里）
├── scripts/       build.py：编译 APK、跑核心测试；device.py：装到设备和调试；setup_toolchain.py：安装工具链；
│                  android_env.py：三者共用的环境设置
└── DESIGN.md      界面设计与组件用法
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
- **网络**：核心的请求经 `HttpTransport` 回调 Kotlin，App 用 `OkHttpTransport`（和 Windows 的 WinHTTP 规则一样：只走
  HTTPS、不存 Cookie、不缓存、25 秒超时、响应最多读 20 MB）。`CancelToken` 取消时，核心在请求之间停下，正在进行的
  OkHttp 请求也一起取消；取消调用它的协程也会取消 token。
- **会话加密**：`KeystoreSessionCipher` 在 Android 密钥库里生成不可导出的 AES-256-GCM 密钥，实现核心的
  `SessionCipher`（seal / open），`SessionStore` 用它读写会话文件。
- **R8**：C++ 按名字找的类和方法（异常、回调、`NativeHttpResult`、`ConvertedAudio`，以及所有 `native` 方法）都列在
  `core/consumer-rules.pro`，App 开启混淆后照样能用；`JNI_OnLoad` 找不到时直接中止，问题会在启动时暴露。
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

### 歌词（#32）

| 类 | 作用 |
| --- | --- |
| `LyricWorkspace` | 核心的歌词工作区：搜索结果、勾选、预览、编辑、导出批次和全部文案。只在主线程用 |
| `MusicApi` | 在后台执行工作区给的请求：`runSearch`、`lyrics`、`exportSong`；`MusicApi.shared` 走 OkHttp |
| `CancelToken` | 取消一个请求（连同正在进行的 HTTP 请求）。每个新请求换一个新的 |
| `LyricSettings` | 导出设置（`lyrics.json`：`options` + `outputDirectory`，和 Windows 一样） |
| `LocalLyrics` | “打开本地歌词”：读取并解码选中的文件 |
| `Lyrics` | 核心的歌词函数（`renderLyrics`、`parseLink`、`pinyinSupported` 等） |
| `LyricOptions`、`LyricFormat`、`LyricLayout`、`LyricEncoding`、`LyricTab`、`MusicProvider`、`SearchKind` | 模型和枚举，`label` / `title` 是核心文案 |

**请求循环**：工作区返回请求，后台执行，结果交回工作区。三种请求各用一个 `CancelToken`：

```kotlin
class LyricsViewModel : ViewModel() {
    val workspace = LyricWorkspace()                  // 主线程创建，只在主线程调用
    val tick = workspace.changes                      // 界面收集它，再读 workspace 的属性
    private var searchToken = CancelToken()
    private var lyricsToken = CancelToken()
    private var exportToken = CancelToken()
    private val api = MusicApi.shared

    init {
        workspace.options = LyricSettings.values.value.options
    }

    fun search(more: Boolean = false) {
        val request = workspace.search(more) ?: return    // 为空时 workspace.notice 已经说明原因
        searchToken.cancel(); searchToken = CancelToken()
        if (!more) cancelLyrics()                         // 新搜索同时取消取歌词
        val token = searchToken
        viewModelScope.launch {
            try {
                val result = api.runSearch(request, AccountStore.cookieHeader(request.provider), token)
                workspace.searchFinished(request, result)?.let(::fetch)   // 自动选中第一首，取它的歌词
            } catch (_: CoreCancelledException) {
            } catch (error: CoreException) {
                workspace.searchFailed(request, error.message)
            }
        }
    }

    fun select(songId: String, reload: Boolean = false) { workspace.select(songId, reload)?.let(::fetch) }

    private fun fetch(request: LyricsRequest) {
        cancelLyrics()
        val token = lyricsToken
        viewModelScope.launch {
            try {
                workspace.lyricsLoaded(request, api.lyrics(request.song, AccountStore.cookieHeader(request.song.provider), token))
            } catch (_: CoreCancelledException) {
            } catch (error: CoreException) {
                workspace.lyricsFailed(request, error.message)
            }
        }
    }

    private fun cancelLyrics() { lyricsToken.cancel(); lyricsToken = CancelToken() }

    fun export(checked: Boolean, context: Context) {
        val batch = (if (checked) workspace.exportChecked() else workspace.exportCurrent()) ?: return
        cancelLyrics()
        exportToken = CancelToken()
        val token = exportToken
        val folder = LyricSettings.values.value.outputFolder
        val cookies = MusicProvider.entries.associateWith { AccountStore.cookieHeader(it) }  // 整批用同一份
        val task = ProcessingService.Task.LYRICS
        ProcessingService.start(context, task, ProcessingService.Progress(workspace.progressText, 0, batch.jobs.size))
        appScope.launch {                                 // 应用级 scope：切到后台也继续
            for (job in batch.jobs) {
                if (token.isCancelled) break
                try {
                    workspace.exportSucceeded(job, api.exportSong(job, batch.options, folder, cookies.getValue(job.song.provider), token))
                } catch (_: CoreCancelledException) {
                    break
                } catch (error: CoreException) {
                    workspace.exportFailed(job, error.message)
                }
                ProcessingService.update(task, ProcessingService.Progress(workspace.progressText, workspace.progress, workspace.exportTotal))
            }
            workspace.endExport()                         // notice 变成“导出完成：成功 18 首…”
            ProcessingService.stop(context, task)
        }
    }

    fun stopExport() { workspace.requestStop(); exportToken.cancel() }

    override fun onCleared() { searchToken.cancel(); lyricsToken.cancel(); workspace.close() }
}
```

和解封装一样，导出要在“不保留活动”下继续，工作区和导出循环需要放在应用级对象里（上面的 `appScope` 指应用级的
`MainScope()`），ViewModel 只转发。

**界面读什么**：`workspace.notice`（`dismissNotice()` 关闭）、`resultsTitle`、`resultsSubtitle`、`loadingText`、
`songs`、`collections`、`selected`、`isChecked(id)`、`allChecked`、`hasMore`、`searching`、`loadingLyrics`、
`text(tab)`、`lineCount`、`preview`、`previewError`、`canRetry`、`canExportCurrent`、`exporting`、`progressText`、
`progress` / `exportTotal`、`failuresTitle`、`failures`、`exportCheckedLabel`、`exported`（导出的文件，带 URI）；歌曲行用
`LyricWorkspace.songArtist(song)`、`songDetail(song)`、`song.duration`。编辑框改动调用 `workspace.edit(tab, text)`（换行统一
`\n`）；程序更新文字框时不要再调用 `edit`。

**打开本地歌词**：

```kotlin
val pickLyrics = rememberLauncherForActivityResult(ActivityResultContracts.OpenDocument()) { uri ->
    if (uri != null) scope.launch {
        if (!LocalLyrics.isLyricFile(LocalLyrics.displayName(uri))) return@launch   // 只接受 .lrc / .txt
        try {
            val file = LocalLyrics.read(uri)                     // 核心解码：UTF-8 / UTF-16 / GBK，最大 5 MB
            if (workspace.importLocal(file.title, file.text)) openEditor()
        } catch (error: LyricException) {
            workspace.showNotice(error.message)
        }
    }
}
pickLyrics.launch(arrayOf("text/*", "application/octet-stream"))
```

**导出设置**：

```kotlin
val settings by LyricSettings.values.collectAsState()
LyricSettings.setOptions(settings.options.copy(format = LyricFormat.SRT))   // 立即保存
workspace.options = LyricSettings.values.value.options                      // 同步给工作区
// 选项文字：LyricFormat.entries.map { it.label }、LyricEncoding…、LyricLayout…
// Android 9 及以下没有系统拼音：隐藏“中文拼音（系统转换）”
if (!Lyrics.pinyinSupported) { /* 不显示这一项；LyricSettings 读取时已把它取消 */ }
// 输出文件夹：默认 OutputFolder.DefaultLyrics（Download/音匣/歌词），更改和解封装一样用 OutputFolders.remember
LyricSettings.setOutputFolder(OutputFolders.remember(tree, settings.outputFolder))
```

### 账号登录（#33）

| 类 | 作用 |
| --- | --- |
| `AccountStore` | 已保存的登录：`signedIn`、`cookieHeader`、`accept`、`logout`、`nickname`、`signedInState`（`StateFlow`） |
| `QrLogin` | 网易云扫码登录：`start()` 返回 `Flow<QrLoginUpdate>`（状态、文案、`QrMatrix`、确认后的 Cookie） |
| `QrMatrix` | 二维码模块；`toBitmap()` 画成白底黑码（带留白），可显示也可存相册 |
| `KeystoreSessionCipher` | Android 密钥库里的 AES-GCM 密钥（不可导出），`AccountStore` 用它加密会话文件 |

会话文件在 App 私有目录 `files/sessions/<netease|qq>.bin`，内容是核心 `cookies_to_json` 的结果经 AES-256-GCM 加密。

**登录状态（歌词工作台顶栏、账号页）**：

```kotlin
val signedIn by AccountStore.signedInState.collectAsState()
val netease = signedIn.getValue(MusicProvider.NETEASE)   // 登录或退出后立即更新
// 账号行：先显示“正在读取账号信息…”，再显示 nickname 返回的整行文字（四种之一，和 Windows 一样）
LaunchedEffect(netease) { if (netease) accountLine = AccountStore.nickname(MusicProvider.NETEASE) }
```

**网易云扫码**：

```kotlin
private var qrJob: Job? = null

fun startQr() {
    qrJob?.cancel()                                     // 刷新、关闭、切到网页登录前都先取消上一轮
    qrJob = viewModelScope.launch {
        QrLogin().start().collect { update ->
            qrState = update                            // update.state / update.message / update.matrix / update.url
            if (update.state == QrLoginState.CONFIRMED) {
                notice = AccountStore.accept(MusicProvider.NETEASE, update.session)
                    ?: "平台没有返回有效登录，请使用官方登录页重试。"
                delay(800)
                closeSheet()
            }
        }
    }
}
// 画二维码：Image(update.matrix!!.toBitmap().asImageBitmap(), "网易云音乐登录二维码", filterQuality = FilterQuality.None)
// “在网易云音乐中打开”：Intent(Intent.ACTION_VIEW, Uri.parse(update.url))
```

**网页登录读 Cookie**（QQ 音乐，以及网易云官方登录页）：

```kotlin
val header = CookieManager.getInstance().getCookie("https://y.qq.com/")
val cookies = AccountStore.cookiesFromCookieManager("y.qq.com", header)   // 网易云用 "music.163.com"
if (AccountStore.isMusicSession(MusicProvider.QQ, cookies)) {
    if (++validReads >= 2) notice = AccountStore.accept(MusicProvider.QQ, cookies)   // 连续两次有效才接受
} else {
    validReads = 0
}
```

**退出**：`notice = AccountStore.logout(provider)`（删除会话文件，返回“已退出{平台名}。”）；清除 WebView 里这个平台的
Cookie 和存储由 #33 做。`Cookie.toString()` 不含值，可以放心记日志；但不要记录 `cookieHeader`。

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
python3 apps/android/scripts/device.py input tap <x> <y>           # 点击；还有 swipe、text、keyevent，只发给最前面的本分支 App
```

- **每个分支装自己的一份**：分支名最后一段做 applicationIdSuffix 和桌面名称，`android/unlock` 装成
  `io.github.backspace135.musicunlock.unlock`，显示为“音匣 (unlock)”，几个会话可以同时装在一台手机上。
  不合法的字符换成 `_`（`android/device-survey` → `.device_survey`）。`--suffix` 可以直接指定；工作树处于
  detached HEAD 时用环境变量 `MUSICUNLOCK_ANDROID_BRANCH` 给出分支名（`remote.py` 会设置）。release 版不加后缀。
- 连着多台设备时用 `--serial` 指定（放在子命令前面，如 `device.py --serial emulator-5554 install`），
  否则用 `ANDROID_SERIAL`，再没有就要求恰好只有一台在线。
- `logcat` 按 App 的 uid 过滤（Android 11 以下按当前进程）。
- **往设备发点击、滑动、文字、按键只用 `input`**：它先查 `dumpsys activity activities`（最前面的 Activity）和
  `dumpsys window`（焦点窗口），两者都是当前分支那一份 App 才发送；最前面是其他 App、通知栏或系统弹窗时拒绝并退出
  （退出码 1）。`shell` 拒绝执行 `input` 和 `monkey`。连续发送时每一步都用 `&&` 连起来，任何一步失败就停下。
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

在最深的工作树路径（如 `.worktrees\g16-android-device-survey`）里编译和跑 `device.py test` 都没问题，但 AGP 的
中间文件路径会超过 260 个字符（实测最长 262，在 `build\core\intermediates\desugar_graph\debugAndroidTest\…`）。
Java 能处理这么长的路径，git 默认处理不了，所以删除工作树要用
`git -c core.longpaths=true worktree remove --force <工作树>`（`remote.py remove` 已经这样做），不需要改系统的
长路径设置。

## 单元测试

界面的纯逻辑（页面栈、底部弹窗吸附）有 JVM 单元测试：

```sh
cd apps/android
JAVA_HOME=~/jdk/jdk-21.0.11+10/Contents/Home ANDROID_HOME=~/Library/Android/sdk ./gradlew :app:testDebugUnitTest
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
  Gradle 的 connected 任务默认装到所有在线设备上，`build.py` 用 `ANDROID_SERIAL` 把它限定在选中的模拟器。网络相关的
  测试用预设响应（`ScriptedTransport`）和本机 HTTPS 测试服务器，不连外网。
- **真实接口检查**（`--device --live`）：和 Windows 的 `MUSICUNLOCK_LIVE_TESTS` 一样，只在手动开启时运行
  `LiveTest`：在网易云和 QQ 音乐上真实搜索“晴天 周杰伦”、取歌词、导出 SRT（和核心渲染逐字节比较）、解析链接，并向
  网易云申请一个登录二维码。需要模拟器能联网。

平台文字转换（拼音、GBK）在 App 里由 JNI 调 Android 的 API 实现（`platform_text_jni.cpp` → `PlatformText.kt`）：
拼音用 `android.icu.text.Transliterator` 的 `Han-Latin`（API 29 起，结果和 macOS、Windows 一致；API 26–28 返回
`std::nullopt`，核心按“不支持拼音”处理），GBK 用 `java.nio.charset` 的 GB18030。核心测试不在 JVM 里运行，链接的是
返回 `std::nullopt` 的占位实现，相关检查跳过，由 instrumented tests 覆盖。

## CI

`.github/workflows/android.yml` 在 Ubuntu 上装固定版本的 NDK 和 CMake，编 debug / release APK，在主机上跑核心测试
（portable AES）和 `:app` 的 JVM 单元测试，上传 debug APK。`:core` 只有 instrumented tests，需要模拟器，CI 不跑。改动 `apps/android/`、`shared/`、`tests/fixtures/` 或 `resources/kgm.mask` 时触发。
