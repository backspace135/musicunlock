import Foundation
import MusicUnlockCore
@testable import MusicUnlock
import XCTest

final class LyricWorkflowTests: XCTestCase {
    @MainActor
    func testEditedLyricsBatchExportAndPreferencePersistence() async throws {
        let suite = "app.musicunlock.lyrics.tests.\(UUID().uuidString)"
        let defaults = UserDefaults(suiteName: suite)!
        let directory = FileManager.default.temporaryDirectory.appendingPathComponent(suite)
        defer {
            try? FileManager.default.removeItem(at: directory)
            defaults.removePersistentDomain(forName: suite)
        }
        let accounts = MusicAccounts(loadSavedSession: false)
        let model = LyricsModel(defaults: defaults, accounts: accounts)
        let song = LyricSong(provider: .netease, songID: "local:test", title: "测试", artist: "歌手", duration: 5000)
        let second = LyricSong(provider: .qq, songID: "local:test2", title: "测试二", artist: "歌手", duration: 4000)
        model.outputDirectory = directory
        model.options.format = .srt
        model.selected = song
        model.document = LyricDocument(original: "[00:01.000]修改后的歌词", translation: "[00:01.000]Edited lyric")
        model.selected = second
        model.document = LyricDocument(original: "[00:00.000]第二首")
        model.select(song)
        XCTAssertTrue(model.document.original.contains("修改后"))
        model.songs = [song, second]
        model.selectAll()
        XCTAssertEqual(model.checked.count, 2)
        model.exportChecked()
        for _ in 0..<200 {
            if !model.exporting { break }
            try await Task.sleep(for: .milliseconds(20))
        }
        XCTAssertFalse(model.exporting)
        XCTAssertEqual(model.exported.count, 2)
        XCTAssertTrue(model.failures.isEmpty)
        let first = try XCTUnwrap(model.exported.first)
        let contents = try String(contentsOf: first, encoding: .utf8)
        XCTAssertTrue(contents.contains("修改后的歌词\nEdited lyric"))
        XCTAssertTrue(contents.contains("00:00:01,000 --> 00:00:05,000"))
        let reloaded = LyricsModel(defaults: defaults, accounts: accounts)
        XCTAssertEqual(reloaded.outputDirectory.path, directory.path)
        XCTAssertEqual(reloaded.options.format, .srt)
    }

    @MainActor
    func testPlatformCookiesRequireMusicAuthentication() {
        func cookie(_ name: String, domain: String, expires: Date = Date().addingTimeInterval(3600)) -> HTTPCookie {
            HTTPCookie(properties: [.name: name, .value: "fixture-not-a-credential", .domain: domain, .path: "/", .expires: expires])!
        }
        XCTAssertFalse(MusicAccounts.hasAuthentication([cookie("skey", domain: ".qq.com")], provider: .qq))
        XCTAssertFalse(MusicAccounts.hasAuthentication([cookie("qqmusic_key", domain: ".y.qq.com")], provider: .qq))
        XCTAssertTrue(MusicAccounts.hasAuthentication([cookie("qqmusic_key", domain: ".y.qq.com"), cookie("uin", domain: ".qq.com")], provider: .qq))
        XCTAssertFalse(MusicAccounts.hasAuthentication([cookie("MUSIC_U", domain: ".example.com")], provider: .netease))
        XCTAssertFalse(MusicAccounts.hasAuthentication([cookie("MUSIC_U", domain: ".music.163.com", expires: Date(timeIntervalSince1970: 0))], provider: .netease))
        XCTAssertTrue(MusicAccounts.hasAuthentication([cookie("MUSIC_U", domain: ".music.163.com")], provider: .netease))
    }
}
