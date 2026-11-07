import AppKit
import CoreImage.CIFilterBuiltins
import MusicUnlockCore
import Security
import SwiftUI
import WebKit

private struct SavedMusicCookie: Codable {
    let name: String
    let value: String
    let domain: String
    let path: String
    let expires: Date?
    let secure: Bool

    init(_ cookie: HTTPCookie) {
        name = cookie.name; value = cookie.value; domain = cookie.domain; path = cookie.path
        expires = cookie.expiresDate; secure = cookie.isSecure
    }

    var cookie: HTTPCookie? {
        if let expires, expires <= Date() { return nil }
        var properties: [HTTPCookiePropertyKey: Any] = [.name: name, .value: value, .domain: domain, .path: path, .secure: secure ? "TRUE" : "FALSE"]
        if let expires { properties[.expires] = expires }
        return HTTPCookie(properties: properties)
    }
}

@MainActor
final class MusicAccounts: ObservableObject {
    @Published private(set) var signedIn: Set<MusicProvider> = []
    @Published var loginProvider: MusicProvider?
    @Published var message: String?
    private var cookies: [MusicProvider: [HTTPCookie]] = [:]
    private let service = "app.musicunlock.music-accounts"

    init(loadSavedSession: Bool = true) {
        // Layout renders must not touch the user's credentials.
        guard loadSavedSession, ProcessInfo.processInfo.environment["MUSICUNLOCK_RENDER_DIR"] == nil else { return }
        for provider in MusicProvider.allCases {
            let query: [String: Any] = [kSecClass as String: kSecClassGenericPassword,
                kSecAttrService as String: service, kSecAttrAccount as String: provider.rawValue,
                kSecReturnData as String: true, kSecMatchLimit as String: kSecMatchLimitOne]
            var result: CFTypeRef?
            if SecItemCopyMatching(query as CFDictionary, &result) == errSecSuccess,
               let data = result as? Data, let saved = try? JSONDecoder().decode([SavedMusicCookie].self, from: data) {
                let valid = saved.compactMap(\.cookie).filter { provider.owns(domain: $0.domain) }
                cookies[provider] = valid
                if Self.hasAuthentication(valid, provider: provider) { signedIn.insert(provider) }
            }
        }
    }

    func header(for provider: MusicProvider) -> String {
        let valid = (cookies[provider] ?? []).filter { $0.expiresDate.map { $0 > Date() } ?? true }
        if !Self.hasAuthentication(valid, provider: provider) { signedIn.remove(provider) }
        return valid.map { "\($0.name)=\($0.value)" }.joined(separator: "; ")
    }

    static func hasAuthentication(_ cookies: [HTTPCookie], provider: MusicProvider) -> Bool {
        let valid = cookies.filter { provider.owns(domain: $0.domain) && !$0.value.isEmpty && ($0.expiresDate.map { $0 > Date() } ?? true) }
        if provider == .netease { return valid.contains { $0.name == "MUSIC_U" } }
        let names = Set(valid.map(\.name))
        let hasKey = names.contains("qqmusic_key") || names.contains("qm_keyst") || names.contains("qqmusic_session")
        return hasKey && !names.isDisjoint(with: ["uin", "qqmusic_uin", "wxuin"])
    }

    @discardableResult
    func accept(_ received: [HTTPCookie], for provider: MusicProvider) -> Bool {
        let valid = received.filter { provider.owns(domain: $0.domain) && ($0.expiresDate.map { $0 > Date() } ?? true) }
        guard Self.hasAuthentication(valid, provider: provider) else { return false }
        cookies[provider] = valid
        signedIn.insert(provider)
        do {
            let data = try JSONEncoder().encode(valid.map(SavedMusicCookie.init))
            let query: [String: Any] = [kSecClass as String: kSecClassGenericPassword, kSecAttrService as String: service,
                kSecAttrAccount as String: provider.rawValue]
            var status = SecItemUpdate(query as CFDictionary, [kSecValueData as String: data] as CFDictionary)
            if status == errSecItemNotFound {
                var item = query
                item[kSecValueData as String] = data
                item[kSecAttrAccessible as String] = kSecAttrAccessibleAfterFirstUnlockThisDeviceOnly
                status = SecItemAdd(item as CFDictionary, nil)
            }
            if status != errSecSuccess { throw LyricError.message("系统钥匙串写入失败（\(status)）。") }
            message = "\(provider.title)登录成功，会话已保存到系统钥匙串。"
        } catch { message = "已登录，本次可用；保存登录失败：\(error.localizedDescription)" }
        loginProvider = nil
        return true
    }

    func prepareWebLogin(provider: MusicProvider, store: WKHTTPCookieStore) async {
        // A requested re-login must not immediately re-accept a stale saved music credential.
        // Keep the official account site's SSO cookies, but require it to issue a fresh music session.
        let names: Set<String> = provider == .netease ? ["MUSIC_U"] : ["qqmusic_key", "qm_keyst", "qqmusic_session"]
        for cookie in await store.allCookies() where provider.owns(domain: cookie.domain) && names.contains(cookie.name) {
            await store.deleteCookie(cookie)
        }
    }

    func logout(_ provider: MusicProvider) {
        cookies[provider] = nil
        signedIn.remove(provider)
        let query: [String: Any] = [kSecClass as String: kSecClassGenericPassword, kSecAttrService as String: service, kSecAttrAccount as String: provider.rawValue]
        let status = SecItemDelete(query as CFDictionary)
        message = status == errSecSuccess || status == errSecItemNotFound ? "已退出\(provider.title)。" : "已退出本次会话；钥匙串删除失败（\(status)），可在“钥匙串访问”中删除音匣的登录记录。"
        Task {
            let store = WKWebsiteDataStore.default().httpCookieStore
            for cookie in await store.allCookies() where provider.owns(domain: cookie.domain) { await store.deleteCookie(cookie) }
            let dataStore = WKWebsiteDataStore.default()
            let types = WKWebsiteDataStore.allWebsiteDataTypes()
            let records = await dataStore.dataRecords(ofTypes: types)
            let owned = records.filter { provider.owns(domain: $0.displayName) }
            await dataStore.removeData(ofTypes: types, for: owned)
        }
    }
}

struct MusicLoginSheet: View {
    @ObservedObject var accounts: MusicAccounts
    let provider: MusicProvider
    let api: MusicAPI
    @State private var officialPage = false
    @State private var revision = 0
    @State private var qr: NSImage?
    @State private var status = "正在生成二维码…"
    @State private var loading = true
    @State private var expired = false

    var body: some View {
        VStack(spacing: 0) {
            HStack {
                VStack(alignment: .leading, spacing: 5) {
                    Text("登录\(provider.title)").font(.title3.weight(.semibold))
                    Text("在官方页面授权，音匣自动保存登录，无需复制 Cookie。")
                        .font(.system(size: 12)).foregroundStyle(Theme.secondary)
                }
                Spacer()
                Button("完成", action: { accounts.loginProvider = nil }).keyboardShortcut(.cancelAction)
            }.padding(20)
            Divider()
            if officialPage || provider == .qq {
                OfficialMusicLogin(provider: provider, accounts: accounts)
                HStack {
                    Text("完成扫码或账号登录后会自动关闭此窗口。若未关闭，点击完成并重试搜索。")
                        .font(.system(size: 11)).foregroundStyle(Theme.secondary)
                    Spacer()
                    if provider == .netease { Button("改用原生扫码") { officialPage = false } }
                }.padding(16)
            } else {
                VStack(spacing: 20) {
                    Spacer()
                    Group {
                        if let qr {
                            Image(nsImage: qr).resizable().interpolation(.none).scaledToFit()
                                .padding(12).background(.white, in: RoundedRectangle(cornerRadius: 12, style: .continuous))
                                .accessibilityLabel("网易云音乐登录二维码")
                        } else if loading { ProgressView().controlSize(.large) }
                        else { Image(systemName: "qrcode").font(.system(size: 70)).foregroundStyle(Theme.secondary) }
                    }.frame(width: 210, height: 210)
                    Text(status).font(.system(size: 14, weight: .medium)).multilineTextAlignment(.center)
                    Text("打开手机网易云音乐，使用扫一扫并确认登录。")
                        .font(.system(size: 12)).foregroundStyle(Theme.secondary)
                    HStack(spacing: 16) {
                        Button(expired ? "重新生成二维码" : "刷新二维码") { revision += 1 }.disabled(loading)
                        Button("使用官方登录页") { officialPage = true }
                    }
                    Spacer()
                    Label("登录信息保存在这台 Mac 的系统钥匙串中", systemImage: "key.horizontal")
                        .font(.system(size: 11)).foregroundStyle(Theme.secondary)
                }.padding(24)
                .task(id: revision) { await pollQRCode() }
            }
        }
        .frame(width: 760, height: 640)
        .background(Theme.background).foregroundStyle(Theme.text).tint(Theme.accent)
    }

    @MainActor
    private func pollQRCode() async {
        qr = nil; loading = true; expired = false; status = "正在生成二维码…"
        do {
            let code = try await api.createQRCode()
            try Task.checkCancellation()
            let filter = CIFilter.qrCodeGenerator()
            filter.message = Data(code.url.absoluteString.utf8)
            if let image = filter.outputImage?.transformed(by: CGAffineTransform(scaleX: 8, y: 8)),
               let cg = CIContext().createCGImage(image, from: image.extent) {
                qr = NSImage(cgImage: cg, size: NSSize(width: 210, height: 210))
            }
            loading = false; status = "请使用网易云音乐扫码"
            for _ in 0..<90 {
                try await Task.sleep(for: .seconds(2))
                let response = try await api.checkQRCode(code)
                try Task.checkCancellation()
                switch response.code {
                case 800: expired = true; status = "二维码已过期，请刷新。"; return
                case 801: status = "请使用网易云音乐扫码"
                case 802: status = "已扫码，请在手机上确认登录"
                case 803:
                    if accounts.accept(response.cookies, for: .netease) {
                        let store = WKWebsiteDataStore.default().httpCookieStore
                        for cookie in response.cookies { await store.setCookie(cookie) }
                    } else { status = "平台没有返回有效登录，请使用官方登录页重试。"; expired = true }
                    return
                default: throw LyricError.message("平台登录状态异常，请刷新或使用官方登录页。")
                }
            }
            expired = true; status = "二维码已超时，请刷新。"
        } catch is CancellationError { return }
        catch {
            if Task.isCancelled { return }
            loading = false; expired = true; status = error.localizedDescription
        }
    }
}

/// WebKit is used exclusively for first-party account authorization; the app's tools are SwiftUI.
private struct OfficialMusicLogin: NSViewRepresentable {
    let provider: MusicProvider
    let accounts: MusicAccounts

    func makeCoordinator() -> Coordinator { Coordinator(provider: provider, accounts: accounts) }
    func makeNSView(context: Context) -> WKWebView {
        let config = WKWebViewConfiguration()
        config.websiteDataStore = .default()
        let web = WKWebView(frame: .zero, configuration: config)
        web.navigationDelegate = context.coordinator
        web.uiDelegate = context.coordinator
        Task { @MainActor in
            await accounts.prepareWebLogin(provider: provider, store: config.websiteDataStore.httpCookieStore)
            guard context.coordinator.active else { return }
            config.websiteDataStore.httpCookieStore.add(context.coordinator)
            let url = provider == .netease ? URL(string: "https://music.163.com/#/login")! : URL(string: "https://y.qq.com/portal/profile.html")!
            web.load(URLRequest(url: url))
        }
        return web
    }
    func updateNSView(_ nsView: WKWebView, context: Context) {}
    static func dismantleNSView(_ nsView: WKWebView, coordinator: Coordinator) {
        coordinator.active = false
        coordinator.captureTask?.cancel()
        nsView.stopLoading()
        nsView.configuration.websiteDataStore.httpCookieStore.remove(coordinator)
        nsView.navigationDelegate = nil; nsView.uiDelegate = nil
    }

    @MainActor
    final class Coordinator: NSObject, WKNavigationDelegate, WKUIDelegate, WKHTTPCookieStoreObserver {
        let provider: MusicProvider
        let accounts: MusicAccounts
        var active = true
        var completed = false
        var captureTask: Task<Void, Never>?
        init(provider: MusicProvider, accounts: MusicAccounts) { self.provider = provider; self.accounts = accounts }

        func cookiesDidChange(in cookieStore: WKHTTPCookieStore) {
            captureTask?.cancel()
            captureTask = Task { @MainActor in
                // Login redirects often set the key and account ID in separate responses.
                try? await Task.sleep(for: .milliseconds(700))
                guard !Task.isCancelled else { return }
                let cookies = await cookieStore.allCookies()
                guard active, !completed, !Task.isCancelled else { return }
                if accounts.accept(cookies, for: provider) { completed = true }
            }
        }
        func webView(_ webView: WKWebView, didFinish navigation: WKNavigation!) {
            cookiesDidChange(in: webView.configuration.websiteDataStore.httpCookieStore)
        }
        func webView(_ webView: WKWebView, decidePolicyFor navigationAction: WKNavigationAction, decisionHandler: @escaping (WKNavigationActionPolicy) -> Void) {
            let scheme = navigationAction.request.url?.scheme?.lowercased()
            // No custom-scheme launching, file navigation, downloads, or injected scripts.
            decisionHandler(scheme == "https" || scheme == "about" ? .allow : .cancel)
        }
        func webView(_ webView: WKWebView, createWebViewWith configuration: WKWebViewConfiguration, for navigationAction: WKNavigationAction, windowFeatures: WKWindowFeatures) -> WKWebView? {
            if navigationAction.request.url?.scheme == "https" { webView.load(navigationAction.request) }
            return nil
        }
    }
}
