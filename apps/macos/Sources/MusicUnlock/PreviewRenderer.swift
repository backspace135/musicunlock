import AppKit
import MusicUnlockCore
import SwiftUI

/// Local, off-screen renders of our own views. Does not capture the desktop or read user files.
@MainActor
enum PreviewRenderer {
    static func render(to directory: String) throws {
        let folder = URL(fileURLWithPath: directory, isDirectory: true)
        try FileManager.default.createDirectory(at: folder, withIntermediateDirectories: true)
        for dark in [false, true] {
            for populated in [false, true] {
                let model = AppModel()
                if populated {
                    model.jobs = [
                        AudioJob(source: URL(fileURLWithPath: "/示例/远行的旋律.ncm"), size: 28_400_000),
                        AudioJob(source: URL(fileURLWithPath: "/示例/午后唱片.mflac"), size: 36_800_000,
                                 state: .done, output: URL(fileURLWithPath: "/示例/午后唱片.flac"), outputExtension: "flac"),
                        AudioJob(source: URL(fileURLWithPath: "/示例/旧日时光.mgg"), size: 8_600_000,
                                 state: .failed, detail: "此文件未内嵌解密密钥，目前无法离线转换"),
                    ]
                }
                try snapshot(model: model, size: NSSize(width: 1060, height: 720), dark: dark,
                             to: folder.appendingPathComponent("\(populated ? "queue" : "empty")-\(dark ? "dark" : "light").png"))
            }
        }
        try snapshot(model: AppModel(), size: NSSize(width: 880, height: 640), dark: false,
                     to: folder.appendingPathComponent("compact-light.png"))
        for dark in [false, true] {
            for populated in [false, true] {
                let model = AppModel()
                model.workspace = .lyrics
                let lyrics = LyricsModel()
                if populated {
                    lyrics.query = "远行的旋律"
                    let song = LyricSong(provider: .netease, songID: "example", title: "远行的旋律", artist: "音匣示例", album: "旅途中的歌", duration: 216_000)
                    lyrics.songs = [song, LyricSong(provider: .netease, songID: "example2", title: "午后的唱片", artist: "音匣示例", album: "旅途中的歌", duration: 187_000)]
                    lyrics.selected = song
                    lyrics.document = LyricDocument(original: "[00:00.000] 远行的旋律\n[00:12.500] 把风装进行囊\n[00:17.800] 让脚步跟上远方\n[00:23.200] 城市慢慢退到身后\n[00:28.600] 我们向着光的方向", translation: "[00:12.500] Pack the wind into a bag\n[00:17.800] Follow the road beyond")
                    lyrics.checked = Set(lyrics.songs.map(\.id))
                }
                try snapshot(view: WorkspaceView(model: model, lyrics: lyrics), size: NSSize(width: 1060, height: 760), dark: dark,
                             to: folder.appendingPathComponent("lyrics-\(populated ? "editor" : "empty")-\(dark ? "dark" : "light").png"))
                if populated && !dark {
                    try snapshot(view: WorkspaceView(model: model, lyrics: lyrics), size: NSSize(width: 880, height: 640), dark: false,
                                 to: folder.appendingPathComponent("lyrics-compact-light.png"))
                }
            }
        }
    }

    private static func snapshot(model: AppModel, size: NSSize, dark: Bool, to url: URL) throws {
        try snapshot(view: WorkspaceView(model: model, lyrics: LyricsModel()), size: size, dark: dark, to: url)
    }

    private static func snapshot<Content: View>(view root: Content, size: NSSize, dark: Bool, to url: URL) throws {
        let appearance = NSAppearance(named: dark ? .darkAqua : .aqua)!
        NSApp.appearance = appearance
        let view = root
            .environment(\.colorScheme, dark ? .dark : .light)
            .environment(\.staticPreview, true)
            .frame(width: size.width, height: size.height)
        let hosting = NSHostingView(rootView: view)
        let window = NSWindow(contentRect: NSRect(origin: .zero, size: size), styleMask: [.borderless], backing: .buffered, defer: false)
        window.appearance = appearance
        window.contentView = hosting
        hosting.frame = NSRect(origin: .zero, size: size)
        hosting.layoutSubtreeIfNeeded()
        RunLoop.main.run(until: Date().addingTimeInterval(0.15))
        guard let bitmap = hosting.bitmapImageRepForCachingDisplay(in: hosting.bounds) else {
            throw CocoaError(.fileWriteUnknown)
        }
        hosting.cacheDisplay(in: hosting.bounds, to: bitmap)
        guard let data = bitmap.representation(using: .png, properties: [:]) else { throw CocoaError(.fileWriteUnknown) }
        try data.write(to: url)
        print("Rendered static layout (glass replaced for off-screen capture): \(url.path)")
        window.contentView = nil
    }
}
