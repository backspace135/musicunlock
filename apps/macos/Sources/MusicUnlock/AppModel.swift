import AppKit
import MusicUnlockCore
import SwiftUI
import UniformTypeIdentifiers

enum JobState: Equatable {
    case waiting, running, done, failed

    var label: String {
        switch self {
        case .waiting: return "待转换"
        case .running: return "转换中"
        case .done: return "已完成"
        case .failed: return "未完成"
        }
    }

    var symbol: String {
        switch self {
        case .waiting: return "circle.dotted"
        case .running: return "arrow.triangle.2.circlepath"
        case .done: return "checkmark.circle.fill"
        case .failed: return "exclamationmark.circle.fill"
        }
    }
}

struct AudioJob: Identifiable {
    let id = UUID()
    let source: URL
    let size: Int64
    var state: JobState = .waiting
    var output: URL?
    var outputExtension = ""
    var title = ""
    var detail = ""
    var cover: NSImage?
}

enum QueueFilter: String, CaseIterable, Identifiable {
    case all = "全部", waiting = "待转换", done = "已完成", failed = "未完成"
    var id: String { rawValue }

    func includes(_ job: AudioJob) -> Bool {
        switch self {
        case .all: return true
        case .waiting: return job.state == .waiting || job.state == .running
        case .done: return job.state == .done
        case .failed: return job.state == .failed
        }
    }
}

@MainActor
final class AppModel: ObservableObject {
    static let shared = AppModel()
    @Published var workspace: AppWorkspace = .audio
    @Published var jobs: [AudioJob] = []
    @Published var filter: QueueFilter = .all
    @Published var importing = false
    @Published var running = false
    @Published var stopRequested = false
    @Published var batchFinished = 0
    @Published var batchCount = 0
    @Published var notice: String?
    private var importCount = 0
    private let defaults: UserDefaults
    @Published var outputDirectory: URL {
        didSet { defaults.set(outputDirectory.path, forKey: "outputDirectory") }
    }
    @Published var naming: NamingPolicy {
        didSet { defaults.set(naming.rawValue, forKey: "namingPolicy") }
    }

    var visibleJobs: [AudioJob] { jobs.filter(filter.includes) }
    var waitingCount: Int { jobs.filter { $0.state == .waiting }.count }
    var doneCount: Int { jobs.filter { $0.state == .done }.count }
    var failedCount: Int { jobs.filter { $0.state == .failed }.count }
    var totalSize: Int64 { jobs.reduce(0) { $0 + $1.size } }
    var currentTitle: String { jobs.first { $0.state == .running }?.source.lastPathComponent ?? "" }

    init(defaults: UserDefaults = .standard) {
        self.defaults = defaults
        let downloads = FileManager.default.urls(for: .downloadsDirectory, in: .userDomainMask).first
            ?? FileManager.default.homeDirectoryForCurrentUser
        outputDirectory = defaults.string(forKey: "outputDirectory").map { URL(fileURLWithPath: $0, isDirectory: true) }
            ?? downloads.appendingPathComponent("音匣输出", isDirectory: true)
        naming = defaults.string(forKey: "namingPolicy").flatMap(NamingPolicy.init(rawValue:)) ?? .original
    }

    func chooseFiles(folder: Bool = false) {
        let panel = NSOpenPanel()
        panel.title = folder ? "添加音乐文件夹" : "添加加密音乐文件"
        panel.prompt = "添加"
        panel.canChooseDirectories = folder
        panel.canChooseFiles = !folder
        panel.allowsMultipleSelection = true
        if !folder {
            panel.allowedContentTypes = AudioDecoder.supportedExtensions.sorted().compactMap { UTType(filenameExtension: $0) }
            panel.allowsOtherFileTypes = true
        }
        if panel.runModal() == .OK { importURLs(panel.urls) }
    }

    func chooseOutput() {
        let panel = NSOpenPanel()
        panel.title = "选择输出文件夹"
        panel.prompt = "选择"
        panel.canChooseFiles = false
        panel.canChooseDirectories = true
        panel.canCreateDirectories = true
        panel.directoryURL = outputDirectory
        if panel.runModal() == .OK, let url = panel.url { outputDirectory = url }
    }

    func importURLs(_ urls: [URL]) {
        guard !urls.isEmpty else { return }
        importCount += 1
        importing = true
        Task {
            let scan = await Task.detached(priority: .userInitiated) {
                var collected: [(URL, Int64)] = []
                var skipped = 0
                let keys: Set<URLResourceKey> = [.isRegularFileKey, .isDirectoryKey, .fileSizeKey]
                func addFile(_ url: URL) {
                    guard AudioDecoder.supportedExtensions.contains(url.pathExtension.lowercased()),
                          let values = try? url.resourceValues(forKeys: keys), values.isRegularFile == true else {
                        skipped += 1
                        return
                    }
                    collected.append((url.standardizedFileURL.resolvingSymlinksInPath(), Int64(values.fileSize ?? 0)))
                }
                for url in urls {
                    if (try? url.resourceValues(forKeys: keys).isDirectory) == true {
                        if let files = FileManager.default.enumerator(at: url, includingPropertiesForKeys: Array(keys), options: [.skipsHiddenFiles, .skipsPackageDescendants]) {
                            while let file = files.nextObject() as? URL {
                                if (try? file.resourceValues(forKeys: keys).isRegularFile) == true { addFile(file) }
                            }
                        }
                    } else { addFile(url) }
                }
                return (collected.sorted { $0.0.path.localizedStandardCompare($1.0.path) == .orderedAscending }, skipped)
            }.value
            var existing = Set(jobs.map(\.source))
            var added = 0
            for (url, size) in scan.0 where existing.insert(url).inserted {
                jobs.append(AudioJob(source: url, size: size))
                added += 1
            }
            importCount -= 1
            importing = importCount > 0
            filter = .all
            if added == 0 { notice = scan.1 > 0 ? "没有找到支持的加密音频文件。可添加 NCM、QMC、MFLAC、MGG 等格式。" : "这些文件已在队列中。" }
            else if scan.1 > 0 { notice = "已添加 \(added) 个文件，跳过 \(scan.1) 个其他格式的文件。" }
            else { notice = nil }
        }
    }

    func start() {
        guard !running, waitingCount > 0 else { return }
        do { try FileManager.default.createDirectory(at: outputDirectory, withIntermediateDirectories: true) }
        catch { notice = "无法创建输出目录：\(error.localizedDescription)"; return }
        let pending = jobs.filter { $0.state == .waiting }.map(\.id)
        let directory = outputDirectory
        let policy = naming
        let maskURL = Bundle.main.url(forResource: "kgm", withExtension: "mask")
        running = true
        stopRequested = false
        batchCount = pending.count
        batchFinished = 0
        notice = nil
        Task {
            let mask = await Task.detached { maskURL.flatMap { try? Data(contentsOf: $0) } }.value
            for id in pending {
                if stopRequested { break }
                guard let index = jobs.firstIndex(where: { $0.id == id }) else { continue }
                jobs[index].state = .running
                let source = jobs[index].source
                let result = await Task.detached(priority: .userInitiated) {
                    Result { try Conversion.run(source: source, directory: directory, naming: policy, kgmMask: mask) }
                }.value
                if let index = jobs.firstIndex(where: { $0.id == id }) {
                    switch result {
                    case .success(let output):
                        jobs[index].state = .done
                        jobs[index].output = output.url
                        jobs[index].outputExtension = output.fileExtension
                        jobs[index].title = output.metadata.title
                        jobs[index].detail = output.url.lastPathComponent
                        jobs[index].cover = output.metadata.cover.flatMap(NSImage.init(data:))
                    case .failure(let error):
                        jobs[index].state = .failed
                        jobs[index].detail = error.localizedDescription
                    }
                }
                batchFinished += 1
            }
            running = false
            if stopRequested { notice = "已停止。未开始的文件仍保留在队列中，可以继续转换。" }
            stopRequested = false
        }
    }

    func retryFailed() {
        guard !running else { return }
        for index in jobs.indices where jobs[index].state == .failed {
            jobs[index].state = .waiting
            jobs[index].detail = ""
        }
        filter = .all
        start()
    }

    func remove(_ id: UUID) { jobs.removeAll { $0.id == id && $0.state != .running } }
    func clearCompleted() { jobs.removeAll { $0.state == .done } }
    func clearQueue() {
        guard !running else { return }
        jobs.removeAll()
        batchFinished = 0
        batchCount = 0
        notice = nil
    }
    func revealOutput() { NSWorkspace.shared.open(outputDirectory) }
}
