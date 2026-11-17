require "json"

package = JSON.parse(File.read(File.join(__dir__, "package.json")))

Pod::Spec.new do |s|
  s.name         = "LocationConfidence"
  s.version      = package["version"]
  s.summary      = package["description"]
  s.homepage     = package["homepage"]
  s.license      = package["license"]
  s.authors      = package["author"]

  s.platforms    = { :ios => min_ios_version_supported }
  s.source       = { :git => "https://github.com/gustavo-em/react-native-location-confidence.git", :tag => "v#{s.version}" }

  s.swift_version = "5.9"

  # Platform sources plus the shared C++ core, compiled from source.
  s.source_files = [
    "ios/**/*.{h,m,mm,swift}",
    "cpp/include/**/*.h",
    "cpp/src/**/*.{h,hpp,cpp}",
  ]

  # The Swift unit tests run on a Mac with `swift test` (see Package.swift).
  # They are not part of the library.
  s.exclude_files = "ios/Tests/**/*"

  # Tells Apple what the library does with data: nothing leaves the device.
  s.resource_bundles = {
    "LocationConfidencePrivacy" => ["ios/PrivacyInfo.xcprivacy"],
  }

  s.frameworks = "CoreLocation", "CoreMotion"

  # lce.h is plain C and is the only header Swift needs to see, so it is the
  # only public header. Everything under ios/ imports C++ (Codegen spec) and
  # must stay private, otherwise the module would not be importable.
  s.public_header_files  = "cpp/include/location_confidence/lce.h"
  s.private_header_files = "ios/**/*.h"

  s.pod_target_xcconfig = {
    "DEFINES_MODULE" => "YES",
    "HEADER_SEARCH_PATHS" => "\"$(PODS_TARGET_SRCROOT)/cpp/include\"",
    # Keep floating point results identical to the Android build.
    "OTHER_CPLUSPLUSFLAGS" => "$(inherited) -ffp-contract=off",
  }

  install_modules_dependencies(s)
end
