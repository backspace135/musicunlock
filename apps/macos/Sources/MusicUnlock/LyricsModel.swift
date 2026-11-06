import AppKit
import MusicUnlockCore
import SwiftUI
import UniformTypeIdentifiers

@MainActor
final class LyricsModel: ObservableObject {
    static let shared = LyricsModel()
    let accounts: MusicAccounts
    let api: MusicAPI
    @Published var provider: MusicProvider = .netease
    @Published var kind: MusicSearchKind = .song
    @Published var query = ""
    @Published var songs: [LyricSong] = []
    @Published var collections: [MusicCollection] = []
    @Published var selected: LyricSong?
    @Published var checked: Set<String> = []
    @Published var document = LyricDocument() {
        didSet { if let selected, !loadingLyrics { cache[selected.id] = document } }
    }
    @Published var options: LyricOptions {
        didSet { if let data = try? JSONEncoder().encode(options) { defaults.set(data, forKey: "lyricOptions") } }
    }
    @Published var outputDirectory: URL {
        didSet { defaults.set(outputDirectory.path, forKey: "lyricOutputDirectory") }
    }
    @Published var searching = false
    @Published var loadingLyrics = false
    @Published var exporting = false
    @Published var hasMore = false
    @Published var progress = 0
    @Published var exportTotal = 0
    @Published var notice: String?
    @Published var failures: [String] = []
    @Published var exported: [URL] = []
    @Published var showSettings = false
    @Published var collectionTitle: String?
    private var cache: [String: LyricDocument] = [:]
    private let defaults: UserDefaults
    private var searchTask: Task<Void, Never>?
    private var lyricTask: Task<Void, Never>?
    private var exportTask: Task<Void, Never>?
    private var page = 1
    private var lastSearch: (String, MusicProvider, MusicSearchKind)?
    private var pendingCollectionTitle: String?

    init(defaults: UserDefaults = .standard, accounts: MusicAccounts? = nil, api: MusicAPI = MusicAPI()) {
        self.defaults = defaults
        self.accounts = accounts ?? MusicAccounts()
        self.api = api
        options = defaults.data(forKey: "lyricOptions").flatMap { try? JSONDecoder().decode(LyricOptions.self, from: $0) } ?? LyricOptions()
        let downloads = FileManager.default.urls(for: .downloadsDirectory, in: .userDomainMask).first ?? FileManager.default.homeDirectoryForCurrentUser
        outputDirectory = defaults.string(forKey: "lyricOutputDirectory").map { URL(fileURLWithPath: $0, isDirectory: true) }
            ?? downloads.appendingPathComponent("音匣输出/歌词", isDirectory: true)
    }

    var preview: String {
        guard selected != nil else { return "" }
        return (try? LyricExporter.render(document, options: options, duration: selected?.duration ?? 0)) ?? ""
    }
    var previewError: String? {
        guard selected != nil, !loadingLyrics else { return nil }
        do { _ = try LyricExporter.render(document, options: options, duration: selected?.duration ?? 0); return nil }
        catch { return error.localizedDescription }
    }

    func search(more: Bool = false) {
        guard !exporting else { return }
        let text = more ? lastSearch?.0 ?? "" : query.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !text.isEmpty else { notice = "输入歌曲、歌手，或粘贴歌曲 / 专辑 / 歌单的完整链接。"; return }
        searchTask?.cancel()
        let source = more ? lastSearch?.1 ?? provider : provider
        let type = more ? lastSearch?.2 ?? kind : kind
        let nextPage = more ? page + 1 : 1
        let requestedTitle = pendingCollectionTitle
        pendingCollectionTitle = nil
        searching = true; notice = nil
        if !more {
            lyricTask?.cancel(); loadingLyrics = false; selected = nil; document = LyricDocument()
            songs = []; collections = []; checked = []; collectionTitle = nil; hasMore = false
        }
        searchTask = Task {
            do {
                if !more, let link = MusicLink.parse(text, provider: source, kind: type) {
                    provider = link.provider; kind = link.kind
                    let results = try await api.resolve(link, cookie: accounts.header(for: link.provider))
                    try Task.checkCancellation()
                    songs = deduplicate(results); collections = []; hasMore = false
                    collectionTitle = link.kind == .song ? nil : requestedTitle ?? "\(link.provider.title)\(link.kind.rawValue)"
                } else {
                    if text.contains("http://") || text.contains("https://") {
                        throw LyricError.message("暂不支持这个链接。请使用 music.163.com 或 y.qq.com 的完整歌曲、专辑、歌单链接；短链接可先在浏览器打开后复制完整地址。")
                    }
                    let result = try await api.search(text, provider: source, kind: type, page: nextPage, cookie: accounts.header(for: source))
                    try Task.checkCancellation()
                    songs = deduplicate(more ? songs + result.songs : result.songs)
                    var seen = Set<String>()
                    collections = (more ? collections + result.collections : result.collections).filter { seen.insert($0.id).inserted }
                    hasMore = result.hasMore; page = nextPage; lastSearch = (text, source, type)
                }
                searching = false
                if songs.isEmpty && collections.isEmpty { notice = "没有找到结果。试试歌曲名加歌手，或切换音乐来源。" }
                if let first = songs.first, !more { select(first) }
            } catch {
                if Task.isCancelled { return }
                searching = false; notice = error.localizedDescription
            }
        }
    }

    func open(_ collection: MusicCollection) {
        query = collection.provider == .netease
            ? "https://music.163.com/\(collection.kind == .album ? "album" : "playlist")?id=\(collection.collectionID)"
            : "https://y.qq.com/n/ryqq/\(collection.kind == .album ? "albumDetail" : "playlist")/\(collection.collectionID)"
        provider = collection.provider; kind = collection.kind
        pendingCollectionTitle = collection.title
        search()
        collectionTitle = collection.title
    }

    func select(_ song: LyricSong, reload: Bool = false) {
        guard !exporting else { return }
        lyricTask?.cancel()
        loadingLyrics = true
        selected = song
        document = cache[song.id] ?? LyricDocument()
        if let saved = cache[song.id], !reload {
            document = saved; loadingLyrics = false; return
        }
        lyricTask = Task {
            do {
                let result = try await api.lyrics(for: song, cookie: accounts.header(for: song.provider))
                try Task.checkCancellation()
                guard selected?.id == song.id else { return }
                document = result; loadingLyrics = false; cache[song.id] = result
                if result.isEmpty { notice = "平台未提供“\(song.title)”的歌词，可能是纯音乐。可导入本地 LRC 或手动编辑。" }
            } catch {
                if Task.isCancelled { return }
                loadingLyrics = false; notice = error.localizedDescription
            }
        }
    }

    func retryLyrics() {
        if let selected, !selected.songID.hasPrefix("local:") { select(selected, reload: true) }
    }

    func searchForAudio(_ job: AudioJob) {
        query = job.title.isEmpty ? job.source.deletingPathExtension().lastPathComponent : job.title
        kind = .song
        search()
    }

    func cancelSearch() {
        searchTask?.cancel(); searching = false
    }

    func toggle(_ song: LyricSong) {
        if checked.contains(song.id) { checked.remove(song.id) } else { checked.insert(song.id) }
    }

    func selectAll() {
        let ids = Set(songs.map(\.id))
        if ids.isSubset(of: checked) { checked.subtract(ids) } else { checked.formUnion(ids) }
    }

    func chooseOutput() {
        let panel = NSOpenPanel()
        panel.title = "选择歌词输出文件夹"; panel.prompt = "选择"
        panel.canChooseFiles = false; panel.canChooseDirectories = true; panel.canCreateDirectories = true
        panel.directoryURL = outputDirectory
        if panel.runModal() == .OK, let url = panel.url { outputDirectory = url }
    }

    func importLyrics() {
        guard !exporting else { return }
        let panel = NSOpenPanel()
        panel.title = "打开本地歌词"; panel.prompt = "打开"
        panel.allowedContentTypes = [UTType(filenameExtension: "lrc"), .plainText].compactMap { $0 }
        panel.allowsOtherFileTypes = true
        guard panel.runModal() == .OK, let url = panel.url else { return }
        do {
            let values = try url.resourceValues(forKeys: [.fileSizeKey])
            guard (values.fileSize ?? 0) <= 5_000_000 else { throw LyricError.message("歌词文件超过 5 MB，请选择较小的 LRC 或 TXT。") }
            let data = try Data(contentsOf: url)
            let text: String?
            if data.starts(with: [0xff, 0xfe]) || data.starts(with: [0xfe, 0xff]) { text = String(data: data, encoding: .utf16) }
            else { text = String(data: data, encoding: .utf8) ?? String(data: data, encoding: .utf16) }
            guard let text else { throw LyricError.message("无法读取文字编码，请将歌词转为 UTF-8 或 UTF-16。") }
            lyricTask?.cancel(); loadingLyrics = false
            let song = LyricSong(provider: provider, songID: "local:\(UUID().uuidString)", title: url.deletingPathExtension().lastPathComponent, artist: "本地歌词")
            selected = song; document = LyricDocument(original: text.replacingOccurrences(of: "\u{feff}", with: ""))
            songs.insert(song, at: 0)
            notice = "已打开本地歌词，可编辑并导出为其他格式。"
        } catch { notice = error.localizedDescription }
    }

    func copyPreview() {
        guard !preview.isEmpty else { return }
        NSPasteboard.general.clearContents(); NSPasteboard.general.setString(preview, forType: .string)
        notice = "已复制\(options.format.rawValue)歌词。"
    }

    func exportCurrent() {
        guard let selected, !loadingLyrics else { return }
        export([selected])
    }

    func exportChecked() { export(songs.filter { checked.contains($0.id) }) }

    private func export(_ items: [LyricSong]) {
        guard !exporting, !items.isEmpty, !searching else { return }
        // Freeze the currently visible edit before starting a batch and prevent an in-flight
        // preview fetch from replacing the document while that batch is running.
        if loadingLyrics { lyricTask?.cancel(); loadingLyrics = false }
        do { try FileManager.default.createDirectory(at: outputDirectory, withIntermediateDirectories: true) }
        catch { notice = "无法创建歌词输出目录：\(error.localizedDescription)"; return }
        let settings = options, directory = outputDirectory
        let existing = cache
        let headers = Dictionary(uniqueKeysWithValues: MusicProvider.allCases.map { ($0, accounts.header(for: $0)) })
        exporting = true; progress = 0; exportTotal = items.count; failures = []; exported = []; notice = nil
        exportTask = Task {
            var skipped = 0
            for (index, song) in items.enumerated() {
                if Task.isCancelled { break }
                do {
                    let lyrics: LyricDocument
                    if let saved = existing[song.id] { lyrics = saved }
                    else { lyrics = try await api.lyrics(for: song, cookie: headers[song.provider] ?? "") }
                    try Task.checkCancellation()
                    cache[song.id] = lyrics
                    if settings.skipInstrumental && lyrics.isEmpty { skipped += 1; progress += 1; continue }
                    let target = try await Task.detached(priority: .userInitiated) {
                        let text = try LyricExporter.render(lyrics, options: settings, duration: song.duration)
                        return try LyricExporter.save(text, song: song, options: settings, index: index + 1, directory: directory)
                    }.value
                    exported.append(target)
                } catch {
                    if Task.isCancelled { break }
                    failures.append("\(song.title)：\(error.localizedDescription)")
                }
                progress += 1
            }
            let stopped = Task.isCancelled
            exporting = false
            notice = "\(stopped ? "已停止" : "导出完成")：成功 \(exported.count) 首，跳过 \(skipped) 首，失败 \(failures.count) 首。\(skipped > 0 ? "可关闭“跳过无歌词歌曲”后重试。" : "")"
        }
    }

    func stopExport() { exportTask?.cancel() }
    func revealOutput() { NSWorkspace.shared.open(outputDirectory) }

    private func deduplicate(_ songs: [LyricSong]) -> [LyricSong] {
        var seen = Set<String>()
        return songs.filter { seen.insert($0.id).inserted }
    }
}
