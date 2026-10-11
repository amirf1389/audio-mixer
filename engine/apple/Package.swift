// swift-tools-version:5.5
// Audio Mixer engine for macOS and iOS: the embedded C core (engine/embedded-c, linked in through the two symlinks of Sources/CAudioMixerCore), a Swift API on top of it
// (AudioMixerEngine) and an Objective-C one (AudioMixerObjC). The same numbers as the page, the C++ engine, the Rust crate and WebAssembly: engine/tests/vectors.txt.
// NOT compiled or run in this repository (there is no Xcode here): open this folder in Xcode or run `swift test` on a Mac and expect to fix small things.
import PackageDescription

let package = Package(
    name: "AudioMixerEngine",
    platforms: [.macOS(.v10_15), .iOS(.v13)],
    products: [
        .library(name: "AudioMixerEngine", targets: ["AudioMixerEngine"]),
        .library(name: "AudioMixerObjC", targets: ["AudioMixerObjC"]),
    ],
    targets: [
        .target(name: "CAudioMixerCore", path: "Sources/CAudioMixerCore", exclude: ["src/am_math.h"], sources: ["src/am_core.c"], publicHeadersPath: "include"),
        .target(name: "AudioMixerEngine", dependencies: ["CAudioMixerCore"]),
        .target(name: "AudioMixerObjC", dependencies: ["CAudioMixerCore"], publicHeadersPath: "include"),
        .testTarget(name: "AudioMixerEngineTests", dependencies: ["AudioMixerEngine", "AudioMixerObjC"]),
    ]
)
