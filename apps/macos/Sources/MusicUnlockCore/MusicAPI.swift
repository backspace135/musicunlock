import CommonCrypto
import CryptoKit
import Foundation

public struct MusicSearchPage: Sendable {
    public var songs: [LyricSong] = []
    public var collections: [MusicCollection] = []
    public var hasMore = false
}

/// A NetEase QR login, made the way the official Windows client does it: eapi requests that identify as the
/// PC client, with the same device id in every request and in the code's chainId. A bare web-style request
/// still gets 801, but NetEase refuses the login after the phone scans it (code 8821).
public struct MusicQRCode: Sendable {
    public let key: String
    public let url: URL  // the QR code's content
    public let device: String  // random per code, sent with each status query
}

public struct MusicQRStatus: Sendable {
    public let code: Int
    public let cookies: [HTTPCookie]
}

/// First-party requests only. No external API service, proxy, Node runtime, or shared cookie jar.
public actor MusicAPI {
    private let session: URLSession
    public init(session: URLSession? = nil) {
        if let session { self.session = session }
        else {
            let config = URLSessionConfiguration.ephemeral
            config.httpShouldSetCookies = false
            config.httpCookieStorage = nil
            config.urlCache = nil
            config.timeoutIntervalForRequest = 25
            config.timeoutIntervalForResource = 40
            self.session = URLSession(configuration: config)
        }
    }

    private func request(_ url: URL, provider: MusicProvider, cookie: String, body: [String: Any]? = nil) async throws -> ([String: Any], [HTTPCookie]) {
        var request = URLRequest(url: url)
        request.setValue("Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/605.1.15 Version/18.0 Safari/605.1.15", forHTTPHeaderField: "User-Agent")
        request.setValue(provider.home.absoluteString, forHTTPHeaderField: "Referer")
        request.setValue("application/json", forHTTPHeaderField: "Accept")
        if !cookie.isEmpty { request.setValue(cookie, forHTTPHeaderField: "Cookie") }
        if let body {
            request.httpMethod = "POST"
            request.setValue("application/json; charset=utf-8", forHTTPHeaderField: "Content-Type")
            request.httpBody = try JSONSerialization.data(withJSONObject: body)
        }
        return try await send(request, provider: provider)
    }

    private func send(_ request: URLRequest, provider: MusicProvider) async throws -> ([String: Any], [HTTPCookie]) {
        try Task.checkCancellation()
        let url = request.url!
        let (data, response) = try await session.data(for: request)
        try Task.checkCancellation()
        guard let http = response as? HTTPURLResponse, (200..<300).contains(http.statusCode) else {
            throw LyricError.message("\(provider.title)暂时无法连接，请稍后重试。")
        }
        guard data.count <= 20_000_000 else { throw LyricError.message("平台响应过大，请缩小搜索范围。") }
        // QQ truncates some playlist descriptions mid-codepoint. Repair those bytes before JSON decoding.
        let text = String(decoding: data, as: UTF8.self)
        var jsonData = Data(text.utf8)
        if !text.trimmingCharacters(in: .whitespacesAndNewlines).hasPrefix("{") {
            // Some QQ endpoints wrap JSON in a fixed callback.
            if let start = text.firstIndex(of: "{"), let end = text.lastIndex(of: "}") { jsonData = Data(text[start...end].utf8) }
        }
        guard let json = try? JSONSerialization.jsonObject(with: jsonData) as? [String: Any] else {
            throw LyricError.message("\(provider.title)返回了无法识别的内容，请重试或登录后再试。")
        }
        let headers = http.allHeaderFields.reduce(into: [String: String]()) { result, item in
            if let name = item.key as? String { result[name] = String(describing: item.value) }
        }
        return (json, HTTPCookie.cookies(withResponseHeaderFields: headers, for: url).filter { provider.owns(domain: $0.domain) })
    }

    private func netease(_ path: String, _ parameters: [String: String], cookie: String = "") async throws -> ([String: Any], [HTTPCookie]) {
        var components = URLComponents(string: "https://music.163.com" + path)!
        var values = parameters
        values["_t"] = String(Int(Date().timeIntervalSince1970 * 1000))
        components.queryItems = values.sorted { $0.key < $1.key }.map { URLQueryItem(name: $0.key, value: $0.value) }
        let result = try await request(components.url!, provider: .netease, cookie: cookie)
        let code = Self.number(result.0["code"])
        if [301, 302, 401, 50000005].contains(code) {
            throw LyricError.message("这项内容需要登录网易云音乐，或登录已过期。请点击“账号登录”重新授权。")
        }
        guard code == 200 || [800, 801, 802, 803].contains(code) else {
            throw LyricError.message((result.0["message"] as? String ?? result.0["msg"] as? String) ?? "网易云音乐请求失败（\(code)），请稍后重试。")
        }
        return result
    }

    /// An eapi request as NetEase's Windows client sends it: the client identity goes both into the cookies and
    /// into the encrypted payload's `header`. Responses stay plain JSON (`e_r` false). Only QR login uses it, so
    /// any answer but 200 or 800-803 is a refused login.
    private func neteasePC(_ path: String, _ payload: [String: Any], device: String) async throws -> ([String: Any], [HTTPCookie]) {
        let now = Int(Date().timeIntervalSince1970 * 1000)
        let header: [String: String] = [
            "os": "pc", "appver": Self.pcAppVersion, "osver": "Microsoft-Windows-10-Professional-build-19045-64bit",
            "deviceId": device, "versioncode": "140", "buildver": String(now / 1000), "resolution": "1920x1080",
            "__csrf": "", "channel": "netease", "requestId": "\(now)_" + String(format: "%04d", now % 1000),
        ]
        var body = payload
        body["header"] = header
        body["e_r"] = false
        let text = String(decoding: try JSONSerialization.data(withJSONObject: body, options: [.sortedKeys, .withoutEscapingSlashes]), as: UTF8.self)
        var request = URLRequest(url: URL(string: "https://interfacepc.music.163.com/eapi/" + path.dropFirst(5))!)
        request.httpMethod = "POST"
        request.setValue("Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/91.0.4472.164 NeteaseMusicDesktop/\(Self.pcAppVersion) Safari/537.36",
                         forHTTPHeaderField: "User-Agent")
        request.setValue("application/x-www-form-urlencoded", forHTTPHeaderField: "Content-Type")
        let unreserved = CharacterSet(charactersIn: "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-._~")
        request.setValue(header.sorted { $0.key < $1.key }.map { name, value in
            name.addingPercentEncoding(withAllowedCharacters: unreserved)! + "=" + value.addingPercentEncoding(withAllowedCharacters: unreserved)!
        }.joined(separator: "; "), forHTTPHeaderField: "Cookie")
        request.httpBody = Data(("params=" + (try Self.eapiParams(path: path, payload: text))).utf8)
        let result = try await send(request, provider: .netease)
        let code = Self.number(result.0["code"])
        guard code == 200 || (800...803).contains(code) else {
            throw LyricError.message(Self.qrRefusalMessage(code: code, platformMessage: result.0["message"] as? String ?? result.0["msg"] as? String ?? ""))
        }
        return result
    }

    static let pcAppVersion = "3.1.17.204416"

    /// The eapi `params` form value: AES-128-ECB over "<path>-36cd479b6b5-<json>-36cd479b6b5-<md5>", upper-case hex.
    static func eapiParams(path: String, payload: String) throws -> String {
        let digest = Insecure.MD5.hash(data: Data("nobody\(path)use\(payload)md5forencrypt".utf8)).map { String(format: "%02x", $0) }.joined()
        let plain = Array("\(path)-36cd479b6b5-\(payload)-36cd479b6b5-\(digest)".utf8)
        let key = Array("e82ckenh8dichen8".utf8)
        var output = [UInt8](repeating: 0, count: plain.count + kCCBlockSizeAES128)
        var written = 0
        let capacity = output.count
        let status = output.withUnsafeMutableBytes { destination in
            plain.withUnsafeBytes { source in
                key.withUnsafeBytes { keyBytes in
                    CCCrypt(CCOperation(kCCEncrypt), CCAlgorithm(kCCAlgorithmAES), CCOptions(kCCOptionECBMode | kCCOptionPKCS7Padding),
                            keyBytes.baseAddress, key.count, nil, source.baseAddress, plain.count, destination.baseAddress, capacity, &written)
                }
            }
        }
        guard status == kCCSuccess else { throw LyricError.message("无法生成网易云音乐登录请求，请重试。") }
        return output.prefix(written).map { String(format: "%02X", $0) }.joined()
    }

    /// What to show when NetEase refuses a QR login, e.g. its risk control's 8821 "请切换其他登录方式或升级新版本再试":
    /// the platform's words alone leave nothing to try, so this points to the official login page.
    public static func qrRefusalMessage(code: Int, platformMessage: String) -> String {
        let reason = platformMessage.isEmpty ? String(code) : "\(code)：\(platformMessage)"
        return "网易云音乐拒绝了这次扫码登录（\(reason)）。请点击“使用官方登录页”，在网易云音乐的网页上登录。"
    }

    private func qq(_ path: String, _ parameters: [String: String], cookie: String) async throws -> [String: Any] {
        var components = URLComponents(string: "https://c.y.qq.com" + path)!
        let pairs = cookie.components(separatedBy: ";").compactMap { item -> (String, String)? in
            let pair = item.trimmingCharacters(in: .whitespaces).split(separator: "=", maxSplits: 1, omittingEmptySubsequences: false)
            guard pair.count == 2 else { return nil }; return (String(pair[0]), String(pair[1]))
        }
        let values = Dictionary(pairs, uniquingKeysWith: { left, _ in left })
        let key = values["qm_keyst"] ?? values["qqmusic_key"] ?? values["p_skey"] ?? values["skey"] ?? ""
        let token = key.utf16.reduce(UInt32(5381)) { ($0 &* 33) &+ UInt32($1) } & 0x7fff_ffff
        components.queryItems = (parameters.merging(["format": "json", "inCharset": "utf8", "outCharset": "utf8", "g_tk": String(token)]) { left, _ in left })
            .sorted { $0.key < $1.key }.map { URLQueryItem(name: $0.key, value: $0.value) }
        let (json, _) = try await request(components.url!, provider: .qq, cookie: cookie)
        guard Self.number(json["code"]) == 0 else {
            throw LyricError.message("QQ 音乐暂时未能提供内容（\(Self.number(json["code"]))）。请登录后重试。")
        }
        return json
    }

    public func search(_ query: String, provider: MusicProvider, kind: MusicSearchKind, page: Int, cookie: String) async throws -> MusicSearchPage {
        let limit = 30
        if provider == .netease {
            let type = kind == .song ? 1 : kind == .album ? 10 : 1000
            let (json, _) = try await netease("/api/search/get/web", ["s": query, "type": String(type), "limit": String(limit), "offset": String((page - 1) * limit)], cookie: cookie)
            let result = json["result"] as? [String: Any] ?? [:]
            var output = MusicSearchPage()
            if kind == .song { output.songs = Self.array(result["songs"]).compactMap(Self.neteaseSong) }
            else {
                output.collections = Self.array(result[kind == .album ? "albums" : "playlists"]).compactMap { item in
                    let id = Self.string(item["id"])
                    guard !id.isEmpty else { return nil }
                    let owner = (item[kind == .album ? "artist" : "creator"] as? [String: Any]) ?? [:]
                    return MusicCollection(provider: .netease, collectionID: id, kind: kind, title: Self.string(item["name"]),
                                           subtitle: Self.string(owner[kind == .album ? "name" : "nickname"]))
                }
            }
            output.hasMore = page * limit < Self.number(result[kind == .song ? "songCount" : kind == .album ? "albumCount" : "playlistCount"])
            return output
        }
        let type = kind == .song ? 0 : kind == .album ? 8 : 2
        let path = kind == .playlist ? "/soso/fcgi-bin/client_music_search_songlist" : "/soso/fcgi-bin/client_search_cp"
        let params = kind == .playlist ? ["query": query, "page_no": String(page - 1), "num_per_page": String(limit), "remoteplace": "txt.yqq.playlist"] :
            ["w": query, "t": String(type), "p": String(page), "n": String(limit), "cr": "1"]
        let json = try await qq(path, params, cookie: cookie)
        let data = json["data"] as? [String: Any] ?? [:]
        let result = kind == .playlist ? data : data[kind == .song ? "song" : "album"] as? [String: Any] ?? [:]
        var output = MusicSearchPage()
        if kind == .song { output.songs = Self.array(result["list"]).compactMap { Self.qqSong($0) } }
        else {
            output.collections = Self.array(result["list"]).compactMap { item in
                let id = Self.string(item[kind == .album ? "albumMID" : "dissid"] ?? item["albummid"] ?? item["albumID"])
                guard !id.isEmpty else { return nil }
                let creator = item["creator"] as? [String: Any] ?? [:]
                return MusicCollection(provider: .qq, collectionID: id, kind: kind,
                                       title: Self.decodeEntities(Self.string(item[kind == .album ? "albumName" : "dissname"] ?? item["albumname"])),
                                       subtitle: Self.decodeEntities(Self.string(item["singerName"] ?? creator["name"])))
            }
        }
        output.hasMore = page * limit < Self.number(result["totalnum"] ?? result["sum"] ?? result["display_num"])
        return output
    }

    public func resolve(_ link: MusicLink, cookie: String) async throws -> [LyricSong] {
        if link.provider == .netease {
            if link.kind == .song { return try await neteaseDetails([link.identifier], cookie: cookie) }
            if link.kind == .album {
                let (json, _) = try await netease("/api/v1/album/" + link.identifier, [:], cookie: cookie)
                return Self.array(json["songs"]).compactMap(Self.neteaseSong)
            }
            let (json, _) = try await netease("/api/v6/playlist/detail", ["id": link.identifier, "n": "1000", "s": "0"], cookie: cookie)
            let playlist = json["playlist"] as? [String: Any] ?? [:]
            let ids = Self.array(playlist["trackIds"]).map { Self.string($0["id"]) }.filter { !$0.isEmpty }
            if ids.isEmpty { return Self.array(playlist["tracks"]).compactMap(Self.neteaseSong) }
            guard ids.count <= 10_000 else { throw LyricError.message("这个歌单超过 10,000 首，请拆分歌单后重试。") }
            var songs: [LyricSong] = []
            for start in stride(from: 0, to: ids.count, by: 300) {
                try Task.checkCancellation()
                songs += try await neteaseDetails(Array(ids[start..<min(start + 300, ids.count)]), cookie: cookie)
            }
            return songs
        }
        if link.kind == .song {
            let numeric = link.identifier.allSatisfy(\.isNumber)
            let json = try await qq("/v8/fcg-bin/fcg_play_single_song.fcg", [numeric ? "songid" : "songmid": link.identifier, "tpl": "yqq_song_detail", "platform": "yqq", "needNewCode": "0"], cookie: cookie)
            return Self.array(json["data"]).compactMap { Self.qqSong($0) }
        }
        if link.kind == .album {
            let json = try await qq("/v8/fcg-bin/fcg_v8_album_info_cp.fcg", [link.identifier.allSatisfy(\.isNumber) ? "albumid" : "albummid": link.identifier], cookie: cookie)
            let data = json["data"] as? [String: Any] ?? [:]
            return Self.array(data["list"]).compactMap { Self.qqSong($0, album: Self.string(data["name"])) }
        }
        let json = try await qq("/qzone/fcg-bin/fcg_ucc_getcdinfo_byids_cp.fcg", ["disstid": link.identifier, "type": "1", "json": "1", "utf8": "1", "onlysong": "0", "new_format": "1"], cookie: cookie)
        guard let playlist = Self.array(json["cdlist"]).first else { return [] }
        let songs = Self.array(playlist["songlist"]).compactMap { Self.qqSong($0) }
        let total = Self.number(playlist["total_song_num"] ?? playlist["songnum"])
        if total > songs.count {
            throw LyricError.message("QQ 音乐仅返回了歌单的 \(songs.count) / \(total) 首歌曲。请登录后重试，或拆分为较小歌单。")
        }
        return songs
    }

    private func neteaseDetails(_ ids: [String], cookie: String) async throws -> [LyricSong] {
        let payload = try JSONSerialization.data(withJSONObject: ids.map { ["id": $0] })
        let (json, _) = try await netease("/api/v3/song/detail", ["c": String(decoding: payload, as: UTF8.self)], cookie: cookie)
        return Self.array(json["songs"]).compactMap(Self.neteaseSong)
    }

    public func lyrics(for song: LyricSong, cookie: String) async throws -> LyricDocument {
        if song.provider == .netease {
            let (json, _) = try await netease("/api/song/lyric", ["id": song.songID, "lv": "-1", "kv": "-1", "tv": "-1", "rv": "-1"], cookie: cookie)
            func lyric(_ key: String) -> String { (json[key] as? [String: Any])?["lyric"] as? String ?? "" }
            var result = LyricDocument(original: lyric("lrc"), translation: lyric("tlyric"), romanization: lyric("romalrc"))
            // YRC is optional; its failure must not hide an otherwise available standard lyric.
            if let response = try? await netease("/api/song/lyric/v1", ["id": song.songID, "lv": "0", "tv": "0", "rv": "0", "yv": "0", "ytv": "0", "yrv": "0", "cp": "false"], cookie: cookie),
               let yrc = response.0["yrc"] as? [String: Any], let text = yrc["lyric"] as? String {
                result.wordTimed = LyricParser.enhancedLRC(fromYRC: text)
            }
            try Task.checkCancellation()
            return result
        }
        let json = try await qq("/lyric/fcgi-bin/fcg_query_lyric_new.fcg", ["songmid": song.songID, "nobase64": "1", "platform": "yqq", "needNewCode": "0"], cookie: cookie)
        func text(_ key: String) -> String {
            let value = Self.string(json[key])
            if value.contains("[") || value.contains("\n") { return Self.decodeEntities(value) }
            return Self.decodeEntities(Data(base64Encoded: value).flatMap { String(data: $0, encoding: .utf8) } ?? value)
        }
        return LyricDocument(original: text("lyric"), translation: text("trans"), romanization: text("roma"))
    }

    /// Both throw LyricError with a hint towards the official login page when NetEase refuses the login.
    public func createQRCode() async throws -> MusicQRCode {
        let device = (0..<32).map { _ in String("0123456789ABCDEF".randomElement()!) }.joined()
        let (json, _) = try await neteasePC("/api/login/qrcode/unikey", ["type": 3], device: device)
        // The chainId ties the scan to this device's login; without it NetEase refuses the login after the scan.
        var components = URLComponents(string: "https://music.163.com/login")!
        guard let key = json["unikey"] as? String, !key.isEmpty else { throw LyricError.message("无法生成登录二维码，请重试。") }
        components.queryItems = [URLQueryItem(name: "codekey", value: key),
                                 URLQueryItem(name: "chainId", value: "v1_\(device)_pc_login_\(Int(Date().timeIntervalSince1970 * 1000))")]
        guard let url = components.url else { throw LyricError.message("无法生成登录二维码，请重试。") }
        return MusicQRCode(key: key, url: url, device: device)
    }

    public func checkQRCode(_ code: MusicQRCode) async throws -> MusicQRStatus {
        let (json, cookies) = try await neteasePC("/api/login/qrcode/client/login", ["key": code.key, "type": 3], device: code.device)
        return MusicQRStatus(code: Self.number(json["code"]), cookies: cookies)
    }

    public func accountName(provider: MusicProvider, cookie: String) async throws -> String? {
        guard !cookie.isEmpty else { return nil }
        if provider == .netease {
            let (json, _) = try await netease("/api/w/nuser/account/get", [:], cookie: cookie)
            guard let profile = json["profile"] as? [String: Any] else { return nil }
            return profile["nickname"] as? String ?? "网易云用户"
        }
        return "QQ 音乐用户"
    }

    private static func number(_ value: Any?) -> Int { (value as? NSNumber)?.intValue ?? Int(value as? String ?? "") ?? 0 }
    private static func string(_ value: Any?) -> String {
        if let text = value as? String { return text }
        return (value as? NSNumber)?.stringValue ?? ""
    }
    private static func array(_ value: Any?) -> [[String: Any]] { value as? [[String: Any]] ?? [] }

    private static func neteaseSong(_ item: [String: Any]) -> LyricSong? {
        let id = string(item["id"])
        guard !id.isEmpty else { return nil }
        let album = item["al"] as? [String: Any] ?? item["album"] as? [String: Any] ?? [:]
        return LyricSong(provider: .netease, songID: id, title: string(item["name"]),
                         artist: array(item["ar"] ?? item["artists"]).map { string($0["name"]) }.joined(separator: " / "),
                         album: string(album["name"]), duration: number(item["dt"] ?? item["duration"]), numericID: id,
                         coverURL: URL(string: string(album["picUrl"])))
    }

    private static func qqSong(_ raw: [String: Any], album albumName: String = "") -> LyricSong? {
        let item = raw["songInfo"] as? [String: Any] ?? raw["musicData"] as? [String: Any] ?? raw
        let id = string(item["mid"] ?? item["songmid"])
        guard !id.isEmpty else { return nil }
        let album = item["album"] as? [String: Any] ?? [:]
        let mid = string(album["mid"] ?? item["albummid"])
        return LyricSong(provider: .qq, songID: id, title: decodeEntities(string(item["title"] ?? item["songname"] ?? item["name"])),
                         artist: array(item["singer"]).map { decodeEntities(string($0["name"])) }.joined(separator: " / "),
                         album: decodeEntities(string(album["name"] ?? item["albumname"] ?? albumName)), duration: number(item["interval"]) * 1000,
                         numericID: string(item["id"] ?? item["songid"]),
                         coverURL: mid.isEmpty ? nil : URL(string: "https://y.gtimg.cn/music/photo_new/T002R300x300M000\(mid).jpg"))
    }

    private static func decodeEntities(_ value: String) -> String {
        var text = value.replacingOccurrences(of: "&amp;", with: "&").replacingOccurrences(of: "&quot;", with: "\"")
            .replacingOccurrences(of: "&apos;", with: "'").replacingOccurrences(of: "&lt;", with: "<").replacingOccurrences(of: "&gt;", with: ">")
        let pattern = try! NSRegularExpression(pattern: #"&#(x[0-9a-fA-F]+|\d+);"#)
        for match in pattern.matches(in: text, range: NSRange(text.startIndex..., in: text)).reversed() {
            guard let full = Range(match.range, in: text), let digits = Range(match.range(at: 1), in: text) else { continue }
            let raw = String(text[digits]); let number = raw.hasPrefix("x") ? UInt32(raw.dropFirst(), radix: 16) : UInt32(raw)
            if let number, let scalar = UnicodeScalar(number) { text.replaceSubrange(full, with: String(scalar)) }
        }
        return text
    }
}
