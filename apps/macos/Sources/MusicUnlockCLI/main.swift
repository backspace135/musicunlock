import Foundation
import MusicUnlockCore

let arguments = Array(CommandLine.arguments.dropFirst())
if arguments.count < 2 {
    print("用法: musicunlock <输出目录> <文件或目录> [更多文件…]")
    exit(2)
}
let directory = URL(fileURLWithPath: arguments[0], isDirectory: true)
let manager = FileManager.default
do {
    try manager.createDirectory(at: directory, withIntermediateDirectories: true)
    let bundledMask = Bundle.main.url(forResource: "kgm", withExtension: "mask")
    let maskPath = ProcessInfo.processInfo.environment["MUSICUNLOCK_KGM_MASK"].map { URL(fileURLWithPath: $0) } ?? bundledMask
    let mask = maskPath.flatMap { try? Data(contentsOf: $0) }
    var failures = 0
    var sources: [URL] = []
    for argument in arguments.dropFirst() {
        let source = URL(fileURLWithPath: argument)
        var isDirectory: ObjCBool = false
        if manager.fileExists(atPath: source.path, isDirectory: &isDirectory), isDirectory.boolValue {
            if let enumerator = manager.enumerator(at: source, includingPropertiesForKeys: [.isRegularFileKey], options: [.skipsHiddenFiles, .skipsPackageDescendants]) {
                for case let url as URL in enumerator where AudioDecoder.supportedExtensions.contains(url.pathExtension.lowercased()) {
                    if (try? url.resourceValues(forKeys: [.isRegularFileKey]).isRegularFile) == true { sources.append(url) }
                }
            }
        } else { sources.append(source) }
    }
    guard !sources.isEmpty else { throw DecodeError.invalid("未找到支持的文件") }
    for source in sources {
        do {
            let output = try Conversion.run(source: source, directory: directory, naming: .original, kgmMask: mask)
            print("完成: \(output.url.path)")
        } catch {
            failures += 1
            fputs("失败: \(source.lastPathComponent): \(error.localizedDescription)\n", stderr)
        }
    }
    exit(failures == 0 ? 0 : 1)
} catch {
    fputs("\(error.localizedDescription)\n", stderr)
    exit(1)
}
