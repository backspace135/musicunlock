import Foundation

public enum MusicProvider: String, CaseIterable, Identifiable, Codable, Sendable {
    case netease, qq
    public var id: String { rawValue }
    public var title: String { self == .netease ? "网易云音乐" : "QQ 音乐" }
    public var home: URL { URL(string: self == .netease ? "https://music.163.com/" : "https://y.qq.com/")! }
    public func owns(domain: String) -> Bool {
        let host = domain.lowercased().trimmingCharacters(in: CharacterSet(charactersIn: "."))
        let root = self == .netease ? "music.163.com" : "y.qq.com"
        return host == root || host.hasSuffix("." + root) || (self == .qq && host == "qq.com")
    }
}

public enum MusicSearchKind: String, CaseIterable, Identifiable, Sendable {
    case song = "歌曲", album = "专辑", playlist = "歌单"
    public var id: String { rawValue }
}

public struct LyricSong: Identifiable, Hashable, Sendable {
    public let provider: MusicProvider
    public let songID: String
    public var id: String { "\(provider.rawValue):\(songID)" }
    public var title: String
    public var artist: String
    public var album: String
    public var duration: Int
    public var numericID: String
    public var coverURL: URL?

    public init(provider: MusicProvider, songID: String, title: String, artist: String = "", album: String = "",
                duration: Int = 0, numericID: String = "", coverURL: URL? = nil) {
        self.provider = provider; self.songID = songID; self.title = title; self.artist = artist
        self.album = album; self.duration = duration; self.numericID = numericID; self.coverURL = coverURL
    }
}

public struct MusicCollection: Identifiable, Sendable {
    public let provider: MusicProvider
    public let collectionID: String
    public let kind: MusicSearchKind
    public var title: String
    public var subtitle: String
    public var id: String { "\(provider.rawValue):\(kind.rawValue):\(collectionID)" }
    public init(provider: MusicProvider, collectionID: String, kind: MusicSearchKind, title: String, subtitle: String) {
        self.provider = provider; self.collectionID = collectionID; self.kind = kind
        self.title = title; self.subtitle = subtitle
    }
}

public struct LyricDocument: Equatable, Sendable {
    public var original: String
    public var translation: String
    public var romanization: String
    public var wordTimed: String
    public init(original: String = "", translation: String = "", romanization: String = "", wordTimed: String = "") {
        self.original = original; self.translation = translation; self.romanization = romanization; self.wordTimed = wordTimed
    }
    public var isEmpty: Bool { LyricParser.parse(original).allSatisfy { $0.text.isEmpty } }
}

public struct LyricLine: Equatable, Sendable {
    public var milliseconds: Int?
    public var text: String
    public init(milliseconds: Int?, text: String) { self.milliseconds = milliseconds; self.text = text }
}

public enum LyricFormat: String, CaseIterable, Identifiable, Codable, Sendable {
    case lrc = "LRC", srt = "SRT", txt = "TXT", vtt = "VTT"
    public var id: String { rawValue }
    public var fileExtension: String { rawValue.lowercased() }
}

public enum LyricLayout: String, CaseIterable, Identifiable, Codable, Sendable {
    case stagger = "逐行交错", merge = "同一行合并", separate = "分段排列"
    public var id: String { rawValue }
}

public enum LyricEncoding: String, CaseIterable, Identifiable, Codable, Sendable {
    case utf8 = "UTF-8", utf8BOM = "UTF-8 BOM", utf16 = "UTF-16 LE"
    public var id: String { rawValue }
    public func data(_ text: String) -> Data {
        switch self {
        case .utf8: return Data(text.utf8)
        case .utf8BOM: return Data([0xef, 0xbb, 0xbf]) + Data(text.utf8)
        case .utf16: return Data([0xff, 0xfe]) + (text.data(using: .utf16LittleEndian) ?? Data())
        }
    }
}

public struct LyricOptions: Codable, Equatable, Sendable {
    public var format: LyricFormat = .lrc
    public var layout: LyricLayout = .stagger
    public var encoding: LyricEncoding = .utf8
    public var includeOriginal = true
    public var includeTranslation = true
    public var includeRomanization = false
    public var includePinyin = false
    public var ignoreEmpty = true
    public var skipInstrumental = true
    public var wordTiming = false
    public var precision = 3
    public var offset = 0
    public var separator = " / "
    public var filename = "${name} - ${singer}"
    public init() {}
}

public enum LyricError: LocalizedError {
    case message(String)
    public var errorDescription: String? { if case .message(let message) = self { return message }; return nil }
}

public enum LyricParser {
    public static func parse(_ input: String) -> [LyricLine] {
        let stamp = try! NSRegularExpression(pattern: #"\[(\d{1,6}):(\d{2})(?:[.:](\d{1,3}))?\]"#)
        let offset = try! NSRegularExpression(pattern: #"(?i)\[offset:([+-]?\d{1,9})\]"#)
        let ns = input as NSString
        let adjustment = offset.firstMatch(in: input, range: NSRange(location: 0, length: ns.length))
            .flatMap { Int(ns.substring(with: $0.range(at: 1))) } ?? 0
        var lines: [(Int, LyricLine)] = []
        for raw in input.replacingOccurrences(of: "\r\n", with: "\n").replacingOccurrences(of: "\r", with: "\n").components(separatedBy: "\n") {
            let row = raw.trimmingCharacters(in: .whitespacesAndNewlines)
            if row.isEmpty { continue }
            let source = row as NSString
            let matches = stamp.matches(in: row, range: NSRange(location: 0, length: source.length))
            if matches.isEmpty {
                if row.hasPrefix("[") && row.contains(":") && row.hasSuffix("]") { continue }
                lines.append((lines.count, LyricLine(milliseconds: nil, text: row)))
            } else {
                // Enhanced LRC contains inline word timestamps; the first timestamp is the line start.
                let isWordTimed = matches.count > 1 && matches.dropFirst().contains { match in
                    let prefix = source.substring(to: match.range.location)
                    return !prefix.hasSuffix("]")
                }
                let text = stamp.stringByReplacingMatches(in: row, range: NSRange(location: 0, length: source.length), withTemplate: "")
                    .trimmingCharacters(in: .whitespaces)
                for match in isWordTimed ? Array(matches.prefix(1)) : matches {
                    let minutes = Int(source.substring(with: match.range(at: 1))) ?? 0
                    let seconds = Int(source.substring(with: match.range(at: 2))) ?? 0
                    let fraction = match.range(at: 3).location == NSNotFound ? "" : source.substring(with: match.range(at: 3))
                    let ms = (Int(fraction) ?? 0) * (fraction.count == 1 ? 100 : fraction.count == 2 ? 10 : 1)
                    lines.append((lines.count, LyricLine(milliseconds: max(0, (minutes * 60 + seconds) * 1000 + ms + adjustment), text: text)))
                }
            }
        }
        return lines.sorted {
            let a = $0.1.milliseconds ?? Int.max, b = $1.1.milliseconds ?? Int.max
            return a == b ? $0.0 < $1.0 : a < b
        }.map(\.1)
    }

    /// NetEase YRC uses line and word durations in milliseconds.
    public static func enhancedLRC(fromYRC input: String) -> String {
        let linePattern = try! NSRegularExpression(pattern: #"^\[(\d+),(\d+)\]"#)
        let wordPattern = try! NSRegularExpression(pattern: #"\((\d+),(\d+),\d+\)([^\(]*)"#)
        return input.components(separatedBy: .newlines).compactMap { row -> String? in
            let source = row as NSString
            guard let header = linePattern.firstMatch(in: row, range: NSRange(location: 0, length: source.length)),
                  let start = Int(source.substring(with: header.range(at: 1))) else { return nil }
            let words = wordPattern.matches(in: row, range: NSRange(location: NSMaxRange(header.range), length: source.length - NSMaxRange(header.range)))
            guard !words.isEmpty else { return nil }
            return words.map { word in
                let time = Int(source.substring(with: word.range(at: 1))) ?? start
                return LyricExporter.timestamp(time, format: .lrc, precision: 3) + source.substring(with: word.range(at: 3))
            }.joined()
        }.joined(separator: "\n")
    }
}

public enum LyricExporter {
    public static func timestamp(_ value: Int, format: LyricFormat, precision: Int = 3) -> String {
        let ms = max(0, value)
        if format == .lrc {
            let digits = min(3, max(0, precision))
            let fraction = digits == 0 ? "" : "." + String(format: "%0*d", digits, (ms % 1000) / (digits == 1 ? 100 : digits == 2 ? 10 : 1))
            return String(format: "[%02d:%02d", ms / 60_000, (ms / 1000) % 60) + fraction + "]"
        }
        return String(format: "%02d:%02d:%02d%@%03d", ms / 3_600_000, (ms / 60_000) % 60, (ms / 1000) % 60, format == .srt ? "," : ".", ms % 1000)
    }

    public static func render(_ document: LyricDocument, options: LyricOptions, duration: Int = 0) throws -> String {
        guard abs(options.offset == Int.min ? Int.max : options.offset) <= 86_400_000 else {
            throw LyricError.message("时间偏移请设置在正负 24 小时以内。")
        }
        guard options.includeOriginal || options.includeTranslation || options.includeRomanization || options.includePinyin else {
            throw LyricError.message("请至少选择一种歌词内容。")
        }
        let original = LyricParser.parse(document.original)
        var tracks: [[LyricLine]] = []
        if options.includeOriginal { tracks.append(original) }
        if options.includeTranslation { tracks.append(aligned(LyricParser.parse(document.translation), to: original)) }
        if options.includeRomanization { tracks.append(aligned(LyricParser.parse(document.romanization), to: original)) }
        if options.includePinyin {
            tracks.append(original.map { line in
                let pinyin = line.text.applyingTransform(.mandarinToLatin, reverse: false) ?? line.text
                return LyricLine(milliseconds: line.milliseconds, text: pinyin)
            })
        }
        tracks = tracks.map { track in track.filter { !options.ignoreEmpty || !$0.text.isEmpty } }.filter { !$0.isEmpty }
        guard !tracks.isEmpty else { throw LyricError.message("所选内容没有可导出的歌词。可切换原文，或导入本地歌词。") }
        let subtitle = options.format == .srt || options.format == .vtt
        if subtitle && tracks.flatMap({ $0 }).contains(where: { $0.milliseconds == nil }) {
            throw LyricError.message("歌词中有未标记时间的行，无法生成字幕。请补上 [分:秒.毫秒]，或改用 TXT。")
        }
        if options.wordTiming {
            guard options.format == .lrc, options.includeOriginal, !options.includeTranslation,
                  !options.includeRomanization, !options.includePinyin else {
                throw LyricError.message("逐字导出仅支持 LRC 原文，请关闭译文、罗马音和拼音。")
            }
            guard !document.wordTimed.isEmpty else { throw LyricError.message("平台未提供这首歌的逐字歌词，请关闭逐字导出。") }
            return shiftedWordLRC(document.wordTimed, offset: options.offset, precision: options.precision) + "\n"
        }
        var rows: [LyricLine] = tracks.flatMap { $0 }
        if options.layout != .separate || subtitle {
            rows = rows.enumerated().sorted { a, b in
                let left = a.element.milliseconds ?? Int.max, right = b.element.milliseconds ?? Int.max
                return left == right ? a.offset < b.offset : left < right
            }.map(\.element)
        }
        if subtitle || options.layout == .merge {
            var merged: [LyricLine] = []
            for line in rows {
                if let last = merged.last, let time = line.milliseconds, time == last.milliseconds {
                    if !line.text.isEmpty { merged[merged.count - 1].text += (subtitle ? "\n" : options.separator) + line.text }
                } else { merged.append(line) }
            }
            rows = merged
        }
        if subtitle {
            // Empty timed rows are real cue boundaries even when not included in the export.
            let boundaries = Set((original + rows).compactMap(\.milliseconds)).sorted()
            var cues: [String] = []
            for line in rows where !line.text.isEmpty {
                guard let rawStart = line.milliseconds else { continue }
                let rawEnd = boundaries.first { $0 > rawStart } ?? (duration > rawStart ? duration : rawStart + 5000)
                let start = max(0, rawStart + options.offset), end = max(0, rawEnd + options.offset)
                if end <= start { continue }
                let range = "\(timestamp(start, format: options.format)) --> \(timestamp(end, format: options.format))"
                cues.append((options.format == .srt ? "\(cues.count + 1)\n" : "") + range + "\n" + line.text)
            }
            return (options.format == .vtt ? "WEBVTT\n\n" : "") + cues.joined(separator: "\n\n") + "\n"
        }
        return rows.map { line in
            let prefix = options.format == .lrc ? line.milliseconds.map { timestamp($0 + options.offset, format: .lrc, precision: options.precision) } ?? "" : ""
            return prefix + line.text
        }.joined(separator: "\n") + "\n"
    }

    private static func aligned(_ lines: [LyricLine], to originals: [LyricLine]) -> [LyricLine] {
        let starts = originals.compactMap(\.milliseconds)
        return lines.map { line in
            guard let time = line.milliseconds,
                  let nearest = starts.min(by: { abs($0 - time) < abs($1 - time) }), abs(nearest - time) <= 80 else { return line }
            return LyricLine(milliseconds: nearest, text: line.text)
        }
    }

    private static func shiftedWordLRC(_ text: String, offset: Int, precision: Int) -> String {
        let pattern = try! NSRegularExpression(pattern: #"\[\d+:\d{2}(?:\.\d{1,3})?\]"#)
        var result = text
        for match in pattern.matches(in: text, range: NSRange(text.startIndex..., in: text)).reversed() {
            guard let range = Range(match.range, in: result) else { continue }
            let token = String(result[range])
            if let time = LyricParser.parse(token).first?.milliseconds {
                result.replaceSubrange(range, with: timestamp(time + offset, format: .lrc, precision: precision))
            }
        }
        return result
    }

    public static func filename(song: LyricSong, template: String, index: Int) -> String {
        var name = template
        for (key, value) in [("name", song.title), ("singer", song.artist), ("album", song.album), ("id", song.songID), ("index", String(index))] {
            name = name.replacingOccurrences(of: "${\(key)}", with: value)
        }
        name = name.components(separatedBy: CharacterSet(charactersIn: "/:\0\n\r")).joined(separator: "_")
        name = String(name.prefix(160)).trimmingCharacters(in: .whitespacesAndNewlines.union(CharacterSet(charactersIn: ".")))
        return name.isEmpty ? "歌词" : name
    }

    @discardableResult
    public static func save(_ text: String, song: LyricSong, options: LyricOptions, index: Int, directory: URL) throws -> URL {
        let name = filename(song: song, template: options.filename, index: index)
        for suffix in 0..<10000 {
            let target = directory.appendingPathComponent(suffix == 0 ? name : "\(name) (\(suffix))").appendingPathExtension(options.format.fileExtension)
            do { try options.encoding.data(text).write(to: target, options: .withoutOverwriting); return target }
            catch let error as CocoaError where error.code == .fileWriteFileExists { continue }
        }
        throw LyricError.message("同名文件过多，请更换文件名或输出文件夹。")
    }
}

public struct MusicLink: Equatable, Sendable {
    public let provider: MusicProvider
    public let kind: MusicSearchKind
    public let identifier: String

    public static func parse(_ input: String, provider: MusicProvider, kind: MusicSearchKind) -> MusicLink? {
        let trimmed = input.trimmingCharacters(in: .whitespacesAndNewlines)
        if trimmed.range(of: #"^\d+$"#, options: .regularExpression) != nil {
            return MusicLink(provider: provider, kind: kind, identifier: trimmed)
        }
        if provider == .qq, trimmed.range(of: #"^[A-Za-z0-9]{14}$"#, options: .regularExpression) != nil {
            return MusicLink(provider: .qq, kind: kind, identifier: trimmed)
        }
        let regex = try! NSRegularExpression(pattern: #"https?://[^\s<>\"，]+"#)
        guard let match = regex.firstMatch(in: trimmed, range: NSRange(trimmed.startIndex..., in: trimmed)),
              let range = Range(match.range, in: trimmed), let url = URL(string: String(trimmed[range])), let host = url.host else { return nil }
        let source: MusicProvider
        if MusicProvider.netease.owns(domain: host) { source = .netease }
        else if host == "y.qq.com" || host.hasSuffix(".y.qq.com") { source = .qq }
        else { return nil }
        let normalized = url.absoluteString.replacingOccurrences(of: "#/", with: "")
        guard let components = URLComponents(string: normalized) else { return nil }
        let params = components.queryItems ?? []
        func param(_ keys: [String]) -> String? { params.first { keys.contains($0.name) }?.value }
        var detected = kind
        if normalized.contains("playlist") || normalized.contains("songlist") || normalized.contains("disstid") { detected = .playlist }
        else if normalized.contains("album") { detected = .album }
        else if normalized.contains("song") { detected = .song }
        let identifier = source == .netease ? param(["id"]) : param(detected == .playlist ? ["disstid", "id"] : detected == .album ? ["albummid", "albumid", "id"] : ["songmid", "songid", "id"])
        let pathID = components.path.components(separatedBy: "/").last?.replacingOccurrences(of: ".html", with: "")
        guard let id = identifier ?? pathID, id.range(of: #"^[A-Za-z0-9]+$"#, options: .regularExpression) != nil,
              id.rangeOfCharacter(from: .decimalDigits) != nil else { return nil }
        return MusicLink(provider: source, kind: detected, identifier: id)
    }
}
