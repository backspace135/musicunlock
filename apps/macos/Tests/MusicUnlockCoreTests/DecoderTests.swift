import CommonCrypto
import Foundation
@testable import MusicUnlockCore
import XCTest

final class DecoderTests: XCTestCase {
    private var fixtures: URL {
        let package = URL(fileURLWithPath: #filePath).deletingLastPathComponent()
            .deletingLastPathComponent().deletingLastPathComponent()
        return package.deletingLastPathComponent().deletingLastPathComponent()
            .appendingPathComponent("tests/fixtures/qmc")
    }

    func testQMCUpstreamReferenceFiles() throws {
        for name in ["mflac0_rc4", "mflac_rc4", "mflac_map", "mgg_map", "qmc0_static"] {
            let folder = fixtures
            let raw = try Data(contentsOf: folder.appendingPathComponent("\(name)_raw.bin"))
            let suffix = try Data(contentsOf: folder.appendingPathComponent("\(name)_suffix.bin"))
            let expected = try Data(contentsOf: folder.appendingPathComponent("\(name)_target.bin"))
            XCTAssertEqual(try Data(QMCDecoder.decode([UInt8](raw + suffix))), expected, name)
        }
    }

    func testQMCUpstreamDerivedKeys() throws {
        for name in ["mflac0_rc4", "mflac_rc4", "mflac_map", "mgg_map"] {
            let folder = fixtures
            let raw = try Data(contentsOf: folder.appendingPathComponent("\(name)_key_raw.bin"))
            let expected = try Data(contentsOf: folder.appendingPathComponent("\(name)_key.bin"))
            XCTAssertEqual(try Data(QMCDecoder.deriveKey([UInt8](raw))), expected, name)
        }
    }

    func testKGMConstantsMatchReferenceTable() throws {
        let values = [UInt8](try Data(contentsOf: fixtures.appendingPathComponent("kgm-predefined.bin")))
        XCTAssertEqual(values.count, 272)
        XCTAssertEqual(LegacyDecoder.kgmPredefined, values)
    }

    func testStaticCipherBoundary() {
        var bytes = [UInt8](repeating: 0, count: 16)
        QMCCipher.applyStatic(&bytes, offset: 0x7ff8)
        XCTAssertEqual(bytes, [0xd8,0x52,0xf7,0x67,0x90,0xca,0xd6,0x4a,0x4a,0xd6,0xca,0x90,0x67,0xf7,0x52,0xd8])
    }

    func testNCMContainerAndMetadata() throws {
        let fixture = try ncmFixture()
        let decoded = try NCMDecoder.decode(fixture.container)
        XCTAssertEqual(Data(decoded.bytes), fixture.audio)
        XCTAssertEqual(decoded.metadata.title, "原生测试")
        XCTAssertEqual(decoded.metadata.artist, "测试歌手")
        let result = try AudioDecoder.decode(fixture.container, fileExtension: "ncm")
        XCTAssertEqual(result.fileExtension, "wav")
        XCTAssertEqual(result.data, fixture.audio)
    }

    func testID3AndFlacMetadata() throws {
        var metadata = AudioMetadata()
        metadata.title = "测试标题"
        metadata.artist = "测试歌手"
        metadata.cover = Data([0xff, 0xd8, 0xff, 0xd9])
        let mp3 = Data([0xff, 0xfb, 0x90, 0x64]) + Data(repeating: 0, count: 64)
        let tagged = try AudioTags.restore(mp3, ext: "mp3", metadata: metadata)
        XCTAssertTrue(tagged.starts(with: Data("ID3".utf8)))
        XCTAssertEqual(tagged.suffix(mp3.count), mp3)
        XCTAssertNotNil(tagged.range(of: Data("APIC".utf8)))
        let flac = Data("fLaC".utf8) + Data([0x80, 0, 0, 34]) + Data(repeating: 0, count: 34) + Data([0xff, 0xf8, 1, 2])
        let flacTagged = try AudioTags.restore(flac, ext: "flac", metadata: metadata)
        XCTAssertNotNil(flacTagged.range(of: Data("TITLE=测试标题".utf8)))
        XCTAssertEqual(flacTagged.suffix(4), Data([0xff, 0xf8, 1, 2]))
    }

    func testExportDoesNotOverwriteAndKeepsOriginal() throws {
        let folder = FileManager.default.temporaryDirectory.appendingPathComponent("musicunlock-tests-\(ProcessInfo.processInfo.processIdentifier)")
        try FileManager.default.createDirectory(at: folder, withIntermediateDirectories: true)
        defer { try? FileManager.default.removeItem(at: folder) }
        let source = folder.appendingPathComponent("中文歌曲.uc")
        let audio = waveFixture()
        let encrypted = Data(audio.map { $0 ^ 0xa3 })
        try encrypted.write(to: source)
        let first = try Conversion.run(source: source, directory: folder, naming: .original)
        let second = try Conversion.run(source: source, directory: folder, naming: .original)
        XCTAssertNotEqual(first.url, second.url)
        XCTAssertEqual(second.url.lastPathComponent, "中文歌曲 (1).wav")
        XCTAssertEqual(try Data(contentsOf: first.url), audio)
        XCTAssertEqual(try Data(contentsOf: source), encrypted)
    }

    func testMalformedFilesThrowInsteadOfCrashing() {
        for ext in AudioDecoder.supportedExtensions {
            for count in [0, 1, 4, 8, 16, 32, 1024] {
                XCTAssertThrowsError(try AudioDecoder.decode(Data(repeating: 0, count: count), fileExtension: ext), ext)
            }
        }
        XCTAssertThrowsError(try AudioDecoder.decode(Data("not musicSTag".utf8), fileExtension: "mflac"))
        XCTAssertThrowsError(try AudioDecoder.decode(Data([1]), fileExtension: "unknown"))
    }

    private func waveFixture() -> Data {
        // Valid PCM WAV containing 80 silent samples, with no third-party audio.
        var data = Data("RIFF".utf8) + le(196) + Data("WAVEfmt ".utf8)
        data += le(16) + Data([1, 0, 1, 0]) + le(8000)
        data += le(16000) + Data([2, 0, 16, 0]) + Data("data".utf8)
        data += le(160) + Data(repeating: 0, count: 160)
        return data
    }

    private func ncmFixture() throws -> (container: Data, audio: Data) {
        let key = Array("fixture-key".utf8)
        let encryptedKey = try aesEncrypt(Array("neteasecloudmusic".utf8) + key, key: NCMDecoder.coreKey).map { $0 ^ 0x64 }
        let json = Data("music:{\"musicName\":\"原生测试\",\"artist\":[[\"测试歌手\",1]],\"album\":\"测试专辑\",\"format\":\"wav\"}".utf8)
        let meta = Data(try aesEncrypt([UInt8](json), key: NCMDecoder.metaKey)).base64EncodedString()
        let encryptedMeta = Array(("163 key(Don't modify):" + meta).utf8).map { $0 ^ 0x63 }
        var box = Array(UInt8.min...UInt8.max)
        var j = 0
        for i in 0..<256 { j = (Int(box[i]) + j + Int(key[i % key.count])) & 255; box.swapAt(i, j) }
        let plain = waveFixture()
        let audio = [UInt8](plain).enumerated().map { pair -> UInt8 in
            let i = (pair.offset + 1) & 255
            let a = Int(box[i])
            let b = Int(box[(i + a) & 255])
            return pair.element ^ box[(a + b) & 255]
        }
        var container = Data("CTENFDAM".utf8) + Data([0, 0])
        container += le(encryptedKey.count) + Data(encryptedKey)
        container += le(encryptedMeta.count) + Data(encryptedMeta)
        container += Data(repeating: 0, count: 5) + le(0) + le(0) + Data(audio)
        return (container, plain)
    }

    private func le(_ value: Int) -> Data { Data([0,8,16,24].map { UInt8(truncatingIfNeeded: value >> $0) }) }

    private func aesEncrypt(_ input: [UInt8], key: [UInt8]) throws -> [UInt8] {
        var output = [UInt8](repeating: 0, count: input.count + 16)
        let capacity = output.count
        var written = 0
        let status = output.withUnsafeMutableBytes { destination in
            input.withUnsafeBytes { source in
                key.withUnsafeBytes { keyBytes in
                    CCCrypt(CCOperation(kCCEncrypt), CCAlgorithm(kCCAlgorithmAES), CCOptions(kCCOptionECBMode | kCCOptionPKCS7Padding),
                            keyBytes.baseAddress, key.count, nil, source.baseAddress, input.count,
                            destination.baseAddress, capacity, &written)
                }
            }
        }
        XCTAssertEqual(status, CCCryptorStatus(kCCSuccess))
        return Array(output.prefix(written))
    }
}
