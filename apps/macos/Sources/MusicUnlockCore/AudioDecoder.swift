import Foundation

public struct AudioMetadata: Sendable {
    public var title: String = ""
    public var artist: String = ""
    public var album: String = ""
    public var cover: Data? = nil

    public init() {}
}

public struct DecodedAudio: Sendable {
    public let data: Data
    public let fileExtension: String
    public let metadata: AudioMetadata
}

public enum DecodeError: LocalizedError {
    case invalid(String)
    case unsupported(String)

    public var errorDescription: String? {
        switch self {
        case .invalid(let detail), .unsupported(let detail): return detail
        }
    }
}

/// Native ports of the MIT-licensed Unlock Music container algorithms.
/// Audio is unwrapped without lossy transcoding or network access.
public enum AudioDecoder {
    public static let qqExtensions: Set<String> = [
        "qmc0", "qmc2", "qmc3", "qmcflac", "qmcogg", "mflac", "mflac0",
        "mgg", "mgg0", "mgg1", "mggl", "tkm", "bkcmp3", "bkcm4a",
        "bkcflac", "bkcwav", "bkcape", "bkcogg", "bkcwma",
        "666c6163", "6d7033", "6f6767", "6d3461", "776176",
    ]
    public static let supportedExtensions = qqExtensions.union([
        "ncm", "uc", "kwm", "kgm", "kgma", "vpr", "xm", "tm0", "tm2", "tm3", "tm6",
    ])

    public static func platform(for ext: String) -> String {
        switch ext.lowercased() {
        case "ncm", "uc": return "网易云"
        case "kgm", "kgma", "vpr": return "酷狗"
        case "kwm": return "酷我"
        case "xm": return "虾米"
        default: return "QQ 音乐"
        }
    }

    public static func decode(_ data: Data, fileExtension: String, kgmMask: Data? = nil) throws -> DecodedAudio {
        let ext = fileExtension.lowercased()
        guard supportedExtensions.contains(ext) else {
            throw DecodeError.unsupported("暂不支持 .\(ext) 格式")
        }
        guard !data.isEmpty else { throw DecodeError.invalid("文件为空") }
        var metadata = AudioMetadata()
        var bytes: [UInt8]
        if ext == "ncm" {
            let result = try NCMDecoder.decode(data)
            bytes = result.bytes
            metadata = result.metadata
        } else if qqExtensions.contains(ext) {
            bytes = try QMCDecoder.decode([UInt8](data))
        } else {
            bytes = try LegacyDecoder.decode([UInt8](data), ext: ext, kgmMask: kgmMask)
        }
        guard let outputExt = sniff(bytes) else {
            throw DecodeError.unsupported("未识别出有效音频；文件可能损坏，或使用了此版本尚不支持的加密方式")
        }
        let audio = try AudioTags.restore(Data(bytes), ext: outputExt, metadata: metadata)
        return DecodedAudio(data: audio, fileExtension: outputExt, metadata: metadata)
    }

    static func sniff(_ bytes: [UInt8]) -> String? {
        if bytes.starts(with: Array("ID3".utf8)) { return "mp3" }
        if bytes.starts(with: Array("fLaC".utf8)) { return "flac" }
        if bytes.starts(with: Array("OggS".utf8)) { return "ogg" }
        if bytes.count >= 8, Array(bytes[4..<8]) == Array("ftyp".utf8) { return "m4a" }
        if bytes.count >= 12, bytes.starts(with: Array("RIFF".utf8)),
           Array(bytes[8..<12]) == Array("WAVE".utf8) { return "wav" }
        if bytes.starts(with: Array("MAC ".utf8)) { return "ape" }
        if bytes.starts(with: Array("FRM8".utf8)) { return "dff" }
        if bytes.starts(with: [0x30, 0x26, 0xb2, 0x75, 0x8e, 0x66, 0xcf, 0x11]) { return "wma" }
        if bytes.count >= 2, bytes[0] == 0xff {
            if bytes[1] & 0xf6 == 0xf0 { return "aac" }
            if bytes[1] & 0xe0 == 0xe0, bytes[1] & 0x06 != 0 { return "mp3" }
        }
        return nil
    }
}

struct ByteReader {
    let bytes: [UInt8]
    var position = 0

    mutating func take(_ count: Int) throws -> [UInt8] {
        guard count >= 0, position <= bytes.count, count <= bytes.count - position else {
            throw DecodeError.invalid("文件被截断，或加密容器头部无效")
        }
        defer { position += count }
        return Array(bytes[position..<(position + count)])
    }

    mutating func little32() throws -> Int {
        Int(try take(4).uint32LE())
    }
}

extension Collection where Element == UInt8, Index == Int {
    func uint32LE() -> UInt32 {
        enumerated().prefix(4).reduce(0) { $0 | UInt32($1.element) << ($1.offset * 8) }
    }

    func uint32BE() -> UInt32 {
        prefix(4).reduce(0) { ($0 << 8) | UInt32($1) }
    }
}
