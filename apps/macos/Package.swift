// swift-tools-version: 5.9
import PackageDescription

let package = Package(
    name: "MusicUnlock",
    platforms: [.macOS("26.0")],
    products: [
        .executable(name: "MusicUnlock", targets: ["MusicUnlock"]),
        .executable(name: "MusicUnlockCLI", targets: ["MusicUnlockCLI"]),
    ],
    targets: [
        .target(name: "MusicUnlockCore"),
        .executableTarget(name: "MusicUnlock", dependencies: ["MusicUnlockCore"]),
        .executableTarget(name: "MusicUnlockCLI", dependencies: ["MusicUnlockCore"]),
        .testTarget(name: "MusicUnlockCoreTests", dependencies: ["MusicUnlockCore"]),
        .testTarget(name: "MusicUnlockAppTests", dependencies: ["MusicUnlock"]),
    ]
)
