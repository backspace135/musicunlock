import CryptoKit
import Foundation
@testable import MusicUnlockCore
import XCTest

/// Answers requests from a table of URL fragments and records what was sent, like the Windows core's scripted transport.
final class ScriptedProtocol: URLProtocol {
    struct Answer {
        var body: String
        var cookies: [String] = []
    }

    nonisolated(unsafe) static var answers: [(fragment: String, replies: [Answer])] = []
    nonisolated(unsafe) static var sent: [URLRequest] = []
    nonisolated(unsafe) static var bodies: [Data] = []
    private static let lock = NSLock()

    static func reset(_ routes: [(String, [Answer])]) {
        lock.withLock { answers = routes.map { (fragment: $0.0, replies: $0.1) }; sent = []; bodies = [] }
    }

    static var session: URLSession {
        let config = URLSessionConfiguration.ephemeral
        config.protocolClasses = [ScriptedProtocol.self]
        config.httpShouldSetCookies = false
        config.httpCookieStorage = nil
        return URLSession(configuration: config)
    }

    override class func canInit(with request: URLRequest) -> Bool { true }
    override class func canonicalRequest(for request: URLRequest) -> URLRequest { request }
    override func stopLoading() {}

    override func startLoading() {
        var body = Data()
        if let stream = request.httpBodyStream {
            stream.open()
            var buffer = [UInt8](repeating: 0, count: 4096)
            while stream.hasBytesAvailable {
                let count = stream.read(&buffer, maxLength: buffer.count)
                if count <= 0 { break }
                body.append(buffer, count: count)
            }
            stream.close()
        } else if let data = request.httpBody { body = data }
        let url = request.url!
        let answer: Answer? = Self.lock.withLock {
            Self.sent.append(request); Self.bodies.append(body)
            guard let index = Self.answers.firstIndex(where: { url.absoluteString.contains($0.fragment) }) else { return nil }
            let replies = Self.answers[index].replies
            if replies.count > 1 { Self.answers[index].replies.removeFirst() }  // the last answer repeats
            return replies.first
        }
        guard let answer else { client?.urlProtocol(self, didFailWithError: URLError(.notConnectedToInternet)); return }
        var headers = ["Content-Type": "application/json"]
        // HTTPURLResponse folds header fields; Foundation splits a comma-joined Set-Cookie back into cookies.
        if !answer.cookies.isEmpty { headers["Set-Cookie"] = answer.cookies.joined(separator: ", ") }
        let response = HTTPURLResponse(url: url, statusCode: 200, httpVersion: "HTTP/1.1", headerFields: headers)!
        client?.urlProtocol(self, didReceive: response, cacheStoragePolicy: .notAllowed)
        client?.urlProtocol(self, didLoad: Data(answer.body.utf8))
        client?.urlProtocolDidFinishLoading(self)
    }
}

final class NeteaseQRTests: XCTestCase {
    /// The JSON payload of an eapi request body, after checking its form, encryption and digest.
    private func openEAPI(_ body: Data, path: String) throws -> [String: Any] {
        let text = String(decoding: body, as: UTF8.self)
        XCTAssertTrue(text.hasPrefix("params="))
        let hex = Array(text.dropFirst(7))
        XCTAssertEqual(String(hex), String(hex).uppercased(), "upper-case hex")
        let sealed = stride(from: 0, to: hex.count, by: 2).compactMap { UInt8(String(hex[$0..<min($0 + 2, hex.count)]), radix: 16) }
        let plain = String(decoding: try NCMDecoder.decryptAES(sealed, key: Array("e82ckenh8dichen8".utf8)), as: UTF8.self)
        let parts = plain.components(separatedBy: "-36cd479b6b5-")
        XCTAssertEqual(parts.count, 3)
        XCTAssertEqual(parts[0], path)
        let digest = Insecure.MD5.hash(data: Data("nobody\(path)use\(parts[1])md5forencrypt".utf8)).map { String(format: "%02x", $0) }.joined()
        XCTAssertEqual(parts[2], digest, "digest")
        return try XCTUnwrap(JSONSerialization.jsonObject(with: Data(parts[1].utf8)) as? [String: Any])
    }

    func testQRLoginRequestsAsPCClient() async throws {
        ScriptedProtocol.reset([("qrcode/unikey", [.init(body: #"{"code":200,"unikey":"KEY"}"#)]),
                                ("qrcode/client/login", [.init(body: #"{"code":801,"message":"等待扫码"}"#)])])
        let api = MusicAPI(session: ScriptedProtocol.session)
        let code = try await api.createQRCode()
        XCTAssertEqual(code.key, "KEY")
        XCTAssertEqual(code.device.count, 32)
        XCTAssertTrue(code.device.allSatisfy { $0.isHexDigit && !$0.isLowercase })
        let query = URLComponents(url: code.url, resolvingAgainstBaseURL: false)!
        XCTAssertEqual(query.host, "music.163.com"); XCTAssertEqual(query.path, "/login")
        XCTAssertEqual(query.queryItems?.first { $0.name == "codekey" }?.value, "KEY")
        let chain = try XCTUnwrap(query.queryItems?.first { $0.name == "chainId" }?.value)
        XCTAssertTrue(chain.hasPrefix("v1_\(code.device)_pc_login_"), chain)
        XCTAssertNotNil(Int(chain.dropFirst("v1_\(code.device)_pc_login_".count)))
        let status = try await api.checkQRCode(code)
        XCTAssertEqual(status.code, 801)

        XCTAssertEqual(ScriptedProtocol.sent.count, 2)
        XCTAssertEqual(ScriptedProtocol.sent[0].url?.absoluteString, "https://interfacepc.music.163.com/eapi/login/qrcode/unikey")
        XCTAssertEqual(ScriptedProtocol.sent[1].url?.absoluteString, "https://interfacepc.music.163.com/eapi/login/qrcode/client/login")
        for request in ScriptedProtocol.sent {
            XCTAssertEqual(request.httpMethod, "POST")
            XCTAssertEqual(request.value(forHTTPHeaderField: "Content-Type"), "application/x-www-form-urlencoded")
            XCTAssertTrue(request.value(forHTTPHeaderField: "User-Agent")?.contains("NeteaseMusicDesktop/3.1.17.204416") == true)
            XCTAssertNil(request.value(forHTTPHeaderField: "Referer"))
            let cookie = request.value(forHTTPHeaderField: "Cookie") ?? ""
            for part in ["os=pc", "appver=3.1.17.204416", "channel=netease", "deviceId=\(code.device)",
                         "osver=Microsoft-Windows-10-Professional-build-19045-64bit"] {
                XCTAssertTrue(cookie.contains(part), part)
            }
        }
        let first = try openEAPI(ScriptedProtocol.bodies[0], path: "/api/login/qrcode/unikey")
        let second = try openEAPI(ScriptedProtocol.bodies[1], path: "/api/login/qrcode/client/login")
        XCTAssertEqual(first["type"] as? Int, 3); XCTAssertNil(first["key"])
        XCTAssertEqual(second["type"] as? Int, 3); XCTAssertEqual(second["key"] as? String, "KEY")
        for payload in [first, second] {
            XCTAssertEqual(payload["e_r"] as? Bool, false)
            let header = try XCTUnwrap(payload["header"] as? [String: String])
            XCTAssertEqual(header["os"], "pc"); XCTAssertEqual(header["appver"], "3.1.17.204416")
            XCTAssertEqual(header["deviceId"], code.device)
        }
        let other = try await api.createQRCode()
        XCTAssertNotEqual(other.device, code.device, "a fresh device per code")
    }

    func testQRStatusesUntilConfirmed() async throws {
        ScriptedProtocol.reset([("qrcode/unikey", [.init(body: #"{"code":200,"unikey":"KEY"}"#)]),
                                ("qrcode/client/login", [.init(body: #"{"code":801}"#), .init(body: #"{"code":802}"#),
                                                         .init(body: #"{"code":803}"#,
                                                               cookies: ["MUSIC_U=secret; Domain=.music.163.com; Max-Age=86400; Path=/",
                                                                         "evil=1; Domain=.example.com; Path=/"])])])
        let api = MusicAPI(session: ScriptedProtocol.session)
        let code = try await api.createQRCode()
        var codes: [Int] = []
        var cookies: [HTTPCookie] = []
        for _ in 0..<3 {
            let status = try await api.checkQRCode(code)
            codes.append(status.code); cookies = status.cookies
        }
        XCTAssertEqual(codes, [801, 802, 803])
        XCTAssertEqual(cookies.map(\.name), ["MUSIC_U"])
        XCTAssertTrue(cookies.allSatisfy { MusicProvider.netease.owns(domain: $0.domain) })
        let devices = try ScriptedProtocol.bodies.dropFirst().map {
            try openEAPI($0, path: "/api/login/qrcode/client/login")["header"] as? [String: String]
        }.map { $0?["deviceId"] }
        XCTAssertEqual(devices, [code.device, code.device, code.device])
    }

    func testQRRefusalsPointToTheOfficialPage() async throws {
        ScriptedProtocol.reset([("qrcode/unikey", [.init(body: #"{"code":200,"unikey":"KEY"}"#)]),
                                ("qrcode/client/login", [.init(body: #"{"code":8821,"message":"请切换其他登录方式或升级新版本再试"}"#)])])
        let api = MusicAPI(session: ScriptedProtocol.session)
        let code = try await api.createQRCode()
        do {
            _ = try await api.checkQRCode(code)
            XCTFail("8821 must throw")
        } catch {
            XCTAssertEqual(error.localizedDescription,
                           "网易云音乐拒绝了这次扫码登录（8821：请切换其他登录方式或升级新版本再试）。请点击“使用官方登录页”，在网易云音乐的网页上登录。")
        }
        ScriptedProtocol.reset([("qrcode/unikey", [.init(body: #"{"code":-462,"msg":"","data":{"verifyType":40}}"#)])])
        do {
            _ = try await MusicAPI(session: ScriptedProtocol.session).createQRCode()
            XCTFail("-462 must throw")
        } catch {
            XCTAssertTrue(error.localizedDescription.contains("（-462）") && error.localizedDescription.contains("使用官方登录页"))
        }
    }

    func testLiveNeteaseQRWaitsForScan() async throws {
        guard ProcessInfo.processInfo.environment["MUSICUNLOCK_LIVE_TESTS"] == "1" else { throw XCTSkip("设置 MUSICUNLOCK_LIVE_TESTS=1 验证实际平台接口") }
        // Two requests: a fresh key and one status query. Nobody logs in.
        let api = MusicAPI()
        let code = try await api.createQRCode()
        XCTAssertTrue(code.url.absoluteString.contains("&chainId=v1_\(code.device)_pc_login_"))
        let status = try await api.checkQRCode(code)
        print("Live check: NetEase QR status \(status.code)")
        XCTAssertEqual(status.code, 801)
    }
}
