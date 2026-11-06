import Foundation

public enum NamingPolicy: String, CaseIterable, Identifiable, Sendable {
    case original = "保留原文件名"
    case artistTitle = "歌手 - 歌曲名"
    case title = "歌曲名"

    public var id: String { rawValue }
}

public struct ConversionOutput: Sendable {
    public let url: URL
    public let fileExtension: String
    public let metadata: AudioMetadata
}

public enum Conversion {
    public static func run(source: URL, directory: URL, naming: NamingPolicy, kgmMask: Data? = nil) throws -> ConversionOutput {
        let original = try Data(contentsOf: source, options: .mappedIfSafe)
        let result = try AudioDecoder.decode(original, fileExtension: source.pathExtension, kgmMask: kgmMask)
        let originalName = source.deletingPathExtension().lastPathComponent
        var name = originalName
        switch naming {
        case .original: break
        case .title: if !result.metadata.title.isEmpty { name = result.metadata.title }
        case .artistTitle:
            if !result.metadata.title.isEmpty {
                name = result.metadata.artist.isEmpty ? result.metadata.title : "\(result.metadata.artist) - \(result.metadata.title)"
            }
        }
        name = name.components(separatedBy: CharacterSet(charactersIn: "/:\0\n\r")).joined(separator: "_")
        name = String(name.prefix(180)).trimmingCharacters(in: .whitespacesAndNewlines)
        if name.isEmpty { name = "音频" }
        // withoutOverwriting handles collisions atomically, including simultaneous app/CLI exports.
        for suffix in 0..<10000 {
            let stem = suffix == 0 ? name : "\(name) (\(suffix))"
            let target = directory.appendingPathComponent(stem).appendingPathExtension(result.fileExtension)
            do {
                try result.data.write(to: target, options: .withoutOverwriting)
                return ConversionOutput(url: target, fileExtension: result.fileExtension, metadata: result.metadata)
            } catch let error as CocoaError where error.code == .fileWriteFileExists {
                continue
            }
        }
        throw DecodeError.invalid("输出目录同名文件过多，请选择其他目录")
    }
}
