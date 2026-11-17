import CoreLocation
import CoreMotion
import Foundation
import UIKit

/// What the iPhone says about itself: the location permission, whether the
/// location is precise, the location switch, whether the app is on the
/// screen, and whether the phone can report its activity.
///
/// It only reads. Nothing here turns the GPS on, and the permission dialog
/// is shown only by requestLocationPermission().
///
/// Create it on the main thread: iOS delivers the answers about permissions
/// to the thread where the CLLocationManager was created.
final class IOSDeviceState: NSObject, DeviceStateSource, CLLocationManagerDelegate {
  private let locationManager = CLLocationManager()

  // A copy of what iOS said last, read from other threads under the lock.
  private let lock = NSLock()
  private var authorization: CLAuthorizationStatus
  private var onlyApproximateLocation: Bool
  private var appIsOnScreen: Bool

  // Touched on the main thread only.
  private var waitingForTheUsersAnswer: [() -> Void] = []

  /// Called on the main thread when the permission changed.
  var whenThePermissionChanges: (() -> Void)?

  /// Called on the main thread when the app is really not visible anymore.
  /// A permission dialog or the Control Center on top of the app does not count.
  var whenTheAppGoesToTheBackground: (() -> Void)?

  /// Called on the main thread when the app is visible again.
  var whenTheAppComesBack: (() -> Void)?

  override init() {
    authorization = locationManager.authorizationStatus
    onlyApproximateLocation = (locationManager.accuracyAuthorization == .reducedAccuracy)
    appIsOnScreen = (UIApplication.shared.applicationState != .background)
    super.init()

    locationManager.delegate = self

    let center = NotificationCenter.default
    center.addObserver(
      self, selector: #selector(appWentToTheBackground),
      name: UIApplication.didEnterBackgroundNotification, object: nil)
    center.addObserver(
      self, selector: #selector(appCameBack),
      name: UIApplication.willEnterForegroundNotification, object: nil)
  }

  deinit {
    NotificationCenter.default.removeObserver(self)
  }

  // MARK: Reading the state (any thread)

  func currentDeviceState() -> DeviceState {
    lock.lock()
    let status = authorization
    let approximate = onlyApproximateLocation
    let onScreen = appIsOnScreen
    lock.unlock()

    let permission = Self.permission(from: status)
    let precision: PermissionPrecision
    if permission != .granted {
      precision = .unknown
    } else if approximate {
      precision = .approximate
    } else {
      precision = .precise
    }

    return DeviceState(
      locationPermission: permission,
      permissionPrecision: precision,
      // Apple asks not to call this on the main thread: it can be slow.
      // currentDeviceState() runs on the engine's queue.
      locationIsTurnedOn: CLLocationManager.locationServicesEnabled(),
      // Core Location is always there on an iPhone.
      locationProvider: .available,
      appIsInForeground: onScreen,
      phoneActivity: Self.phoneActivitySupport()
    )
  }

  private static func permission(from status: CLAuthorizationStatus) -> LocationPermission {
    switch status {
    case .notDetermined:
      return .notDetermined
    case .restricted:
      return .restricted
    case .denied:
      return .denied
    case .authorizedAlways, .authorizedWhenInUse:
      return .granted
    @unknown default:
      return .denied
    }
  }

  /// Can the phone report its activity, and did the user allow it?
  ///
  /// The answer is "available" only when the permission ALREADY exists. That
  /// is what keeps an evaluation from showing a permission dialog: the engine
  /// only asks for activity reports when it is told they are available.
  static func phoneActivitySupport() -> PhoneActivitySupport {
    // Without this text in Info.plist, touching the sensor crashes the app.
    let theAppExplainsWhy = hasUsageDescription("NSMotionUsageDescription")
    if !theAppExplainsWhy || !CMMotionActivityManager.isActivityAvailable() {
      return .notSupported
    }

    switch CMMotionActivityManager.authorizationStatus() {
    case .authorized:
      return .available
    case .notDetermined:
      return .permissionNotAsked
    case .denied, .restricted:
      return .permissionDenied
    @unknown default:
      return .permissionDenied
    }
  }

  static func hasUsageDescription(_ key: String) -> Bool {
    let text = Bundle.main.object(forInfoDictionaryKey: key) as? String
    return text != nil && text?.isEmpty == false
  }

  // MARK: Asking the user (main thread)

  /// Shows the system's location dialog, if the user was never asked.
  /// `whenAnswered` runs on the main thread, right away when there is
  /// nothing to ask: iOS shows this dialog only once.
  func requestLocationPermission(whenAnswered: @escaping () -> Void) {
    let neverAsked = (locationManager.authorizationStatus == .notDetermined)
    if !neverAsked {
      whenAnswered()
      return
    }
    waitingForTheUsersAnswer.append(whenAnswered)
    locationManager.requestWhenInUseAuthorization()
  }

  // MARK: What iOS tells us (main thread)

  func locationManagerDidChangeAuthorization(_ manager: CLLocationManager) {
    lock.lock()
    authorization = manager.authorizationStatus
    onlyApproximateLocation = (manager.accuracyAuthorization == .reducedAccuracy)
    lock.unlock()

    let theUserAnswered = (manager.authorizationStatus != .notDetermined)
    if theUserAnswered {
      let waiting = waitingForTheUsersAnswer
      waitingForTheUsersAnswer = []
      waiting.forEach { $0() }
    }

    whenThePermissionChanges?()
  }

  @objc private func appWentToTheBackground() {
    lock.lock()
    appIsOnScreen = false
    lock.unlock()
    whenTheAppGoesToTheBackground?()
  }

  @objc private func appCameBack() {
    lock.lock()
    appIsOnScreen = true
    lock.unlock()
    whenTheAppComesBack?()
  }
}
