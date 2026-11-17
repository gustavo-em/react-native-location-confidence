# Changelog

The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/) and the versions follow [Semantic Versioning](https://semver.org/).

## Unreleased

Nothing has been published yet. This is what version 0.1.0 will contain.

### Added

- `LocationConfidence.getProximityReport()`: reports how good the location evidence is for "this phone is inside the delivery radius of this address", with an index from 0 to 100 (not a percentage), what the evidence points to and the reasons.
- Sessions (`startSession`, `clearHistory`, `stopSession`), which remember the last 10 readings in memory, and optional collecting while the app is on the screen.
- `getCapabilities`, `getAuthorization`, `requestLocationPermission`, `requestMotionPermission`. Nothing else ever shows a permission dialog.
- Cancelling with an `AbortSignal`.
- `LocationConfidence.simulation`, to replace the GPS with readings of the app's own, for demos and tests.
- One C++ engine shared by Android (Kotlin, JNI) and iOS (Swift).
- An example app with a simulator of delivery situations and a screen for the real GPS, in English and in Portuguese.

### Known limits

- Foreground only. No background location.
- Android does not read the phone's activity yet; iOS does, through Core Motion.
- Tested on an Android emulator and on the iOS Simulator, not yet on physical devices.
- The numbers behind the index are design choices, not calibrated values.
