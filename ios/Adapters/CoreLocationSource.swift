import CoreLocation
import Foundation

/// The real GPS on iOS: Core Location, which mixes GPS, Wi-Fi and cell towers.
///
/// It only translates. Each `CLLocation` becomes a `Reading` with exactly
/// what iOS said, and nothing is judged here: whether a reading is good, old
/// or suspicious is the engine's decision.
///
/// Two things differ from Android:
///
///   - iOS has no "one reading every N milliseconds" setting. It delivers at
///     its own pace (about once per second) and the engine keeps what it
///     needs.
///   - iOS stamps a location with the wall clock, the one the user can change
///     in the settings. The engine's clock is a different one, so the moment
///     of each reading is worked out here ("it is 300 ms old, so it was taken
///     300 ms ago on the engine's clock") and marked as estimated.
///
/// The caller must have checked the location permission before start().
final class CoreLocationSource: NSObject, LocationSource, CLLocationManagerDelegate {
  private let clock: Clock

  // Touched on the main thread only. iOS calls the delegate on the thread
  // where the manager was created, so it is created there too.
  private var manager: CLLocationManager?
  private var listener: LocationSourceListener?

  init(clock: Clock) {
    self.clock = clock
  }

  func start(timeBetweenReadingsMs: Int64, listener: LocationSourceListener) {
    DispatchQueue.main.async {
      self.listener = listener

      if self.manager == nil {
        let newManager = CLLocationManager()
        newManager.delegate = self
        newManager.desiredAccuracy = kCLLocationAccuracyBest
        newManager.distanceFilter = kCLDistanceFilterNone
        // The library only works while the app is on the screen, so iOS must
        // not pause the readings to save battery behind our back.
        newManager.pausesLocationUpdatesAutomatically = false
        self.manager = newManager
      }
      self.manager?.startUpdatingLocation()
    }
  }

  func stop() {
    DispatchQueue.main.async {
      self.manager?.stopUpdatingLocation()
      self.listener = nil
    }
  }

  // MARK: What iOS delivers (main thread)

  func locationManager(_ manager: CLLocationManager, didUpdateLocations locations: [CLLocation]) {
    // One call can bring several locations, old and new. Each one keeps its
    // own time; the engine sorts them out.
    let nowOnTheEnginesClockMs = clock.nowMs()
    let nowOnTheWallClock = Date()
    let onlyApproximate = (manager.accuracyAuthorization == .reducedAccuracy)

    let readings = locations.map { location -> Reading in
      let ageSeconds = max(0, nowOnTheWallClock.timeIntervalSince(location.timestamp))
      return Reading(
        latitude: location.coordinate.latitude,
        longitude: location.coordinate.longitude,
        // iOS says "I do not know" with a negative number.
        accuracyMeters: location.horizontalAccuracy >= 0 ? location.horizontalAccuracy : nil,
        capturedAtMs: nowOnTheEnginesClockMs - Int64((ageSeconds * 1000).rounded()),
        timeIsReliable: true,
        captureTimeWasEstimated: true,
        onlyApproximatePermission: onlyApproximate,
        speedMps: location.speed >= 0 ? location.speed : nil,
        speedAccuracyMps: location.speedAccuracy >= 0 ? location.speedAccuracy : nil,
        headingDegrees: location.course >= 0 ? location.course : nil,
        provider: .coreLocation,
        mockSignal: Self.mockSignal(of: location)
      )
    }

    listener?.onReadings(readings)
  }

  func locationManager(_ manager: CLLocationManager, didFailWithError error: Error) {
    // "Location unknown" only means "not yet": iOS keeps trying, and so do we.
    let isOnlyNotYet = (error as? CLError)?.code == .locationUnknown
    if isOnlyNotYet {
      return
    }
    listener?.onFailure()
  }

  /// Did iOS say the location was made up by software (a GPX file in Xcode,
  /// the Simulator, a tool)? "Not flagged" only repeats what iOS said. It
  /// proves nothing.
  private static func mockSignal(of location: CLLocation) -> MockSignal {
    guard let source = location.sourceInformation else {
      return .unknown
    }
    return source.isSimulatedBySoftware ? .flagged : .notFlagged
  }
}
