// swift-tools-version:5.9
import PackageDescription

// This file exists for ONE purpose: to run the Swift unit tests on a Mac,
// with no simulator and no Xcode project:
//
//     swift test
//
// The library is NOT distributed as a Swift package. Apps get it through
// CocoaPods (LocationConfidence.podspec), like any React Native library, and
// this file is not part of the npm package.
//
// What is built here:
//   LCECore                     the C++ engine, with its C front door (lce.h)
//   LocationConfidenceHost      the Swift code that needs no iPhone: the
//                               wrapper around the engine, the coordinator,
//                               the queue and the clock
//   LocationConfidenceHostTests the tests in ios/Tests
//
// The parts that talk to Core Location, Core Motion and React Native only
// exist on iOS and are built with the example app.

let studyNotes = [
  "src/Assessment.study.ts",
  "src/EvaluationRequest.study.ts",
  "src/JumpDetector.study.ts",
  "src/NodeRuntime.study.ts",
  "src/PhoneActivityTimeline.study.ts",
  "src/ReadingSelection.study.ts",
  "src/UncertaintyModel.study.ts",
]

let package = Package(
  name: "LocationConfidenceHost",
  platforms: [.macOS(.v12)],
  targets: [
    .target(
      name: "LCECore",
      path: "cpp",
      exclude: ["tests", "CMakeLists.txt"] + studyNotes,
      sources: ["src"],
      publicHeadersPath: "include",
      cxxSettings: [
        // Keeps the floating point results identical on every platform.
        .unsafeFlags(["-ffp-contract=off"])
      ]
    ),
    .target(
      name: "LocationConfidenceHost",
      dependencies: ["LCECore"],
      path: "ios",
      exclude: [
        "Tests",
        "Bridge",
        "LocationConfidence.h",
        "LocationConfidence.mm",
        "PrivacyInfo.xcprivacy",
        "Adapters/CoreLocationSource.swift",
        "Adapters/CoreMotionActivitySource.swift",
        "Adapters/IOSDeviceState.swift",
      ],
      sources: [
        "Core",
        "Models",
        "Ports",
        "Application",
        "Adapters/DispatchSerialQueue.swift",
        "Adapters/MonotonicClock.swift",
      ]
    ),
    .testTarget(
      name: "LocationConfidenceHostTests",
      dependencies: ["LocationConfidenceHost"],
      path: "ios/Tests"
    ),
  ],
  cxxLanguageStandard: .cxx17
)
