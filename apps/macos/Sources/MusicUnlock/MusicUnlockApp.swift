import AppKit
import SwiftUI

final class AppDelegate: NSObject, NSApplicationDelegate {
    func applicationDidFinishLaunching(_ notification: Notification) {
        if let directory = ProcessInfo.processInfo.environment["MUSICUNLOCK_RENDER_DIR"] {
            Task { @MainActor in
                do { try PreviewRenderer.render(to: directory) }
                catch { fputs("Render failed: \(error)\n", stderr) }
                NSApp.terminate(nil)
            }
            return
        }
        NSApp.setActivationPolicy(.regular)
        NSApp.activate(ignoringOtherApps: true)
    }

    // SwiftUI's lifecycle forwards open events here, not to the legacy openFiles: callback.
    func application(_ application: NSApplication, open urls: [URL]) {
        Task { @MainActor in
            AppModel.shared.workspace = .audio
            AppModel.shared.importURLs(urls.filter(\.isFileURL))
        }
    }

    func applicationShouldTerminate(_ sender: NSApplication) -> NSApplication.TerminateReply {
        guard AppModel.shared.running || LyricsModel.shared.exporting else { return .terminateNow }
        let alert = NSAlert()
        alert.messageText = "任务仍在进行"
        alert.informativeText = "现在退出将中断当前任务。已经完成的文件会保留。"
        alert.addButton(withTitle: "继续任务")
        alert.addButton(withTitle: "退出")
        return alert.runModal() == .alertSecondButtonReturn ? .terminateNow : .terminateCancel
    }

    func applicationShouldTerminateAfterLastWindowClosed(_ sender: NSApplication) -> Bool { true }
}

@main
struct MusicUnlockApp: App {
    @NSApplicationDelegateAdaptor(AppDelegate.self) var delegate
    @StateObject private var model = AppModel.shared
    @StateObject private var lyrics = LyricsModel.shared

    var body: some Scene {
        Window("音匣", id: "main") {
            WorkspaceView(model: model, lyrics: lyrics)
        }
        .defaultSize(width: 1060, height: 720)
        .windowResizability(.contentMinSize)
        .commands {
            CommandGroup(replacing: .appInfo) { Button("关于音匣", action: showAbout) }
            CommandGroup(replacing: .newItem) {
                Button(model.workspace == .audio ? "添加文件…" : "打开歌词…") {
                    if model.workspace == .audio { model.chooseFiles() } else { lyrics.importLyrics() }
                }.keyboardShortcut("o")
                Button("添加文件夹…") { model.workspace = .audio; model.chooseFiles(folder: true) }.keyboardShortcut("o", modifiers: [.command, .shift])
            }
            CommandGroup(after: .newItem) {
                Button("开始转换", action: model.start).keyboardShortcut(.return, modifiers: .command)
                    .disabled(model.running || model.waitingCount == 0)
                Button("停止转换") { model.stopRequested = true }.keyboardShortcut(".")
                    .disabled(!model.running)
            }
            CommandMenu("歌词") {
                Button("音乐解锁") { model.workspace = .audio }.keyboardShortcut("1")
                Button("歌词工具") { model.workspace = .lyrics }.keyboardShortcut("2")
                Divider()
                Button("搜索歌词") {
                    model.workspace = .lyrics
                    Task { @MainActor in
                        try? await Task.sleep(for: .milliseconds(100))
                        NotificationCenter.default.post(name: .focusLyricSearch, object: nil)
                    }
                }.keyboardShortcut("f")
                Button("导出当前歌词", action: lyrics.exportCurrent).keyboardShortcut("s")
                    .disabled(model.workspace != .lyrics || lyrics.selected == nil || lyrics.exporting || lyrics.loadingLyrics)
            }
        }
    }
}
