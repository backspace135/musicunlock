import Foundation
@testable import MusicUnlock
import XCTest

final class WorkflowTests: XCTestCase {
    @MainActor
    func testFolderImportBatchFailureRetryAndPreferencePersistence() async throws {
        let manager = FileManager.default
        let root = manager.temporaryDirectory.appendingPathComponent("musicunlock-workflow-\(ProcessInfo.processInfo.processIdentifier)")
        let source = root.appendingPathComponent("input")
        let nested = source.appendingPathComponent("album")
        let output = root.appendingPathComponent("output")
        try manager.createDirectory(at: nested, withIntermediateDirectories: true)
        let suite = "app.musicunlock.tests.\(ProcessInfo.processInfo.processIdentifier)"
        let defaults = UserDefaults(suiteName: suite)!
        defer {
            try? manager.removeItem(at: root)
            defaults.removePersistentDomain(forName: suite)
        }
        let audio = Data("RIFF".utf8) + Data([36, 0, 0, 0]) + Data("WAVEfmt ".utf8)
            + Data([16, 0, 0, 0, 1, 0, 1, 0, 0x40, 0x1f, 0, 0, 0x80, 0x3e, 0, 0, 2, 0, 16, 0])
            + Data("data".utf8) + Data([0, 0, 0, 0])
        let encrypted = Data(audio.map { $0 ^ 0xa3 })
        let valid = nested.appendingPathComponent("测试音乐.uc")
        let damaged = source.appendingPathComponent("损坏文件.ncm")
        try encrypted.write(to: valid)
        try Data([1, 2, 3]).write(to: damaged)
        try Data("ignore".utf8).write(to: source.appendingPathComponent("说明.txt"))

        let model = AppModel(defaults: defaults)
        model.outputDirectory = output
        model.importURLs([source, valid])
        // Concurrent imports must stay disabled until both scans finish; duplicates are removed.
        model.importURLs([valid])
        try await waitUntil { !model.importing }
        XCTAssertEqual(model.jobs.count, 2)
        model.start()
        XCTAssertTrue(model.running)
        try await waitUntil { !model.running }
        XCTAssertEqual(model.doneCount, 1)
        XCTAssertEqual(model.failedCount, 1)
        XCTAssertEqual(model.waitingCount, 0)
        XCTAssertEqual(try Data(contentsOf: output.appendingPathComponent("测试音乐.wav")), audio)
        XCTAssertEqual(try Data(contentsOf: valid), encrypted)

        let recreated = AppModel(defaults: defaults)
        XCTAssertEqual(recreated.outputDirectory.path, output.path)
        model.retryFailed()
        try await waitUntil { !model.running }
        XCTAssertEqual(model.failedCount, 1)
        model.clearCompleted()
        XCTAssertEqual(model.jobs.count, 1)
        model.clearQueue()
        XCTAssertTrue(model.jobs.isEmpty)
    }

    @MainActor
    private func waitUntil(_ condition: @MainActor () -> Bool) async throws {
        for _ in 0..<500 {
            if condition() { return }
            try await Task.sleep(nanoseconds: 20_000_000)
        }
        XCTFail("后台任务未在预期内完成")
    }
}
