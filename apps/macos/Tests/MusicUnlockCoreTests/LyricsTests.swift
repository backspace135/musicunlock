import Foundation
@testable import MusicUnlockCore
import XCTest

final class LyricsTests: XCTestCase {
    func testRepeatedTimestampsOffsetAndEnhancedLRC() throws {
        let lines = LyricParser.parse("[ti:测试]\n[offset:-50]\n[00:01.20][00:03.000]重唱\n[00:02.000]\n[00:04.000]一[00:04.500]二")
        XCTAssertEqual(lines, [LyricLine(milliseconds: 1150, text: "重唱"), LyricLine(milliseconds: 1950, text: ""),
                               LyricLine(milliseconds: 2950, text: "重唱"), LyricLine(milliseconds: 3950, text: "一二")])
        let word = LyricParser.enhancedLRC(fromYRC: "{\"t\":0}\n[1000,900](1000,400,0)一(1400,500,0)二")
        XCTAssertEqual(word, "[00:01.000]一[00:01.400]二")
    }

    func testBilingualSRTMergesCuesAndRespectsBlankBoundary() throws {
        let lyrics = LyricDocument(original: "[00:01.000]甲\n[00:02.000]\n[00:03.000]乙",
                                   translation: "[00:01.020]A\n[00:03.000]B")
        var options = LyricOptions()
        options.format = .srt; options.offset = 100
        let srt = try LyricExporter.render(lyrics, options: options, duration: 6000)
        XCTAssertEqual(srt, "1\n00:00:01,100 --> 00:00:02,100\n甲\nA\n\n2\n00:00:03,100 --> 00:00:06,100\n乙\nB\n")
        options.format = .vtt
        XCTAssertTrue(try LyricExporter.render(lyrics, options: options).hasPrefix("WEBVTT\n\n00:00:01.100 --> 00:00:02.100"))
    }

    func testUntimedSubtitleAndInvalidSelectionAreActionableErrors() throws {
        var options = LyricOptions(); options.format = .srt
        XCTAssertThrowsError(try LyricExporter.render(LyricDocument(original: "纯文字歌词"), options: options))
        options.format = .txt; options.includeOriginal = false; options.includeTranslation = false
        XCTAssertThrowsError(try LyricExporter.render(LyricDocument(original: "[00:01]歌词"), options: options))
        options.includeOriginal = true; options.offset = Int.max
        XCTAssertThrowsError(try LyricExporter.render(LyricDocument(original: "[00:01]歌词"), options: options))
    }

    func testWordTimingPreservesWordStampsAndRejectsUnavailableContent() throws {
        var options = LyricOptions(); options.includeTranslation = false; options.wordTiming = true; options.offset = -100
        let document = LyricDocument(original: "[00:01.000]一二", wordTimed: "[00:01.000]一[00:01.400]二")
        XCTAssertEqual(try LyricExporter.render(document, options: options), "[00:00.900]一[00:01.300]二\n")
        XCTAssertThrowsError(try LyricExporter.render(LyricDocument(original: document.original), options: options))
        options.includeTranslation = true
        XCTAssertThrowsError(try LyricExporter.render(document, options: options))
    }

    func testLocalExportNeverOverwritesAndPreservesEncoding() throws {
        let directory = FileManager.default.temporaryDirectory.appendingPathComponent("lyric-export-\(UUID().uuidString)")
        try FileManager.default.createDirectory(at: directory, withIntermediateDirectories: true)
        defer { try? FileManager.default.removeItem(at: directory) }
        let song = LyricSong(provider: .netease, songID: "1", title: "../../歌曲", artist: "歌手")
        var options = LyricOptions(); options.encoding = .utf8BOM
        let first = try LyricExporter.save("原始", song: song, options: options, index: 1, directory: directory)
        let second = try LyricExporter.save("修改", song: song, options: options, index: 1, directory: directory)
        XCTAssertNotEqual(first, second)
        XCTAssertEqual(first.deletingLastPathComponent().path, directory.path)
        XCTAssertEqual(try Data(contentsOf: first), Data([0xef, 0xbb, 0xbf]) + Data("原始".utf8))
        XCTAssertTrue(second.lastPathComponent.contains("(1)"))
        XCTAssertEqual(LyricEncoding.utf16.data("中文").prefix(2), Data([0xff, 0xfe]))
    }

    func testMusicLinksSelectCorrectProviderAndKind() {
        let netease = MusicLink.parse("分享歌曲 https://music.163.com/#/song?id=186016&userid=1", provider: .qq, kind: .playlist)
        XCTAssertEqual(netease?.provider, .netease); XCTAssertEqual(netease?.kind, .song); XCTAssertEqual(netease?.identifier, "186016")
        let qq = MusicLink.parse("https://y.qq.com/n/ryqq/albumDetail/000MkMni19ClKG", provider: .netease, kind: .song)
        XCTAssertEqual(qq?.provider, .qq); XCTAssertEqual(qq?.kind, .album); XCTAssertEqual(qq?.identifier, "000MkMni19ClKG")
        XCTAssertNil(MusicLink.parse("https://music.163.com.example.com/song?id=1", provider: .netease, kind: .song))
        XCTAssertNil(MusicLink.parse("晴天 周杰伦", provider: .netease, kind: .song))
        XCTAssertTrue(MusicProvider.netease.owns(domain: ".music.163.com"))
        XCTAssertFalse(MusicProvider.qq.owns(domain: "notqq.com"))
    }

    func testLiveFirstPartySearchResolveLyrics() async throws {
        guard ProcessInfo.processInfo.environment["MUSICUNLOCK_LIVE_TESTS"] == "1" else { throw XCTSkip("设置 MUSICUNLOCK_LIVE_TESTS=1 验证实际平台接口") }
        let api = MusicAPI()
        for provider in MusicProvider.allCases {
            for kind in MusicSearchKind.allCases {
                print("Live check: \(provider.title) \(kind.rawValue) search")
                let page = try await api.search("周杰伦", provider: provider, kind: kind, page: 1, cookie: "")
                if kind == .song { XCTAssertFalse(page.songs.isEmpty, provider.title) }
                else { XCTAssertFalse(page.collections.isEmpty, "\(provider.title)\(kind.rawValue)") }
            }
            let songLink = MusicLink.parse(provider == .netease ? "186016" : "0039MnYb0qxYhV", provider: provider, kind: .song)!
            print("Live check: \(provider.title) song details")
            let songs = try await api.resolve(songLink, cookie: "")
            let song = try XCTUnwrap(songs.first, provider.title)
            XCTAssertEqual(song.title, "晴天")
            let lyric = try await api.lyrics(for: song, cookie: "")
            XCTAssertFalse(lyric.isEmpty, provider.title)
            XCTAssertTrue(lyric.original.contains("["))
            let album = MusicLink.parse(provider == .netease ? "18905" : "000MkMni19ClKG", provider: provider, kind: .album)!
            print("Live check: \(provider.title) album details")
            let albumSongs = try await api.resolve(album, cookie: "")
            XCTAssertGreaterThan(albumSongs.count, 1, provider.title)
            let playlist = MusicLink.parse(provider == .netease ? "3778678" : "3235982757", provider: provider, kind: .playlist)!
            print("Live check: \(provider.title) playlist details")
            let playlistSongs = try await api.resolve(playlist, cookie: "")
            XCTAssertGreaterThan(playlistSongs.count, 1, provider.title)
        }
    }
}
