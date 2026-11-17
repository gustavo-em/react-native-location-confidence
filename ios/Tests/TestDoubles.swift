import Foundation

@testable import LocationConfidenceHost

/*
 * Pretend pieces for the tests. They stand in for the GPS, the clock, the
 * queue and the state of the phone, so a test decides exactly what happens
 * and when. No test waits for real time.
 *
 * The Kotlin tests have the same file: android/src/sharedTest/.../TestDoubles.kt.
 */

/// A clock the test moves by hand.
final class FakeClock: Clock {
  var now: Int64 = 0
  func nowMs() -> Int64 { return now }
}

/// A queue with no thread: a task runs as soon as it is posted, and a timer
/// fires when the test moves the clock past its moment with advanceTo().
final class ManualQueue: SerialQueue {
  private final class Timer: Cancellable {
    let firesAtMs: Int64
    let task: () -> Void
    var isCancelled = false

    init(firesAtMs: Int64, task: @escaping () -> Void) {
      self.firesAtMs = firesAtMs
      self.task = task
    }

    func cancel() {
      isCancelled = true
    }
  }

  private let clock: FakeClock
  private var waitingTasks: [() -> Void] = []
  private var timers: [Timer] = []
  private var isRunningATask = false
  private(set) var isShutDown = false

  init(clock: FakeClock) {
    self.clock = clock
  }

  /// Timers that are set and were not cancelled.
  var howManyTimersAreSet: Int {
    return timers.filter { !$0.isCancelled }.count
  }

  func post(_ task: @escaping () -> Void) {
    if isShutDown { return }
    waitingTasks.append(task)

    // A task posted from inside another task waits for that one to end, as
    // it would on a real queue.
    if isRunningATask { return }
    isRunningATask = true
    while !waitingTasks.isEmpty {
      waitingTasks.removeFirst()()
    }
    isRunningATask = false
  }

  func postDelayed(_ delayMs: Int64, _ task: @escaping () -> Void) -> Cancellable {
    let timer = Timer(firesAtMs: clock.now + delayMs, task: task)
    timers.append(timer)
    return timer
  }

  func shutDownAfter(_ lastTask: @escaping () -> Void) {
    post(lastTask)
    isShutDown = true
    timers.removeAll()
  }

  /// Moves the clock to `targetMs`, firing every timer that is due on the way, in order.
  func advanceTo(_ targetMs: Int64) {
    while true {
      let due = timers.filter { !$0.isCancelled && $0.firesAtMs <= targetMs }
      guard let next = due.min(by: { $0.firesAtMs < $1.firesAtMs }) else { break }
      timers.removeAll { $0 === next }
      if next.firesAtMs > clock.now {
        clock.now = next.firesAtMs
      }
      post(next.task)
    }
    timers.removeAll { $0.isCancelled }
    if targetMs > clock.now {
      clock.now = targetMs
    }
  }
}

/// A GPS that delivers what the test gives it, and only while it is on.
final class FakeLocationSource: LocationSource {
  private var listener: LocationSourceListener?

  /// Kept after stop(), so a test can make a delivery arrive too late.
  private(set) var lastListener: LocationSourceListener?

  private(set) var timesStarted = 0
  private(set) var timesStopped = 0
  private(set) var timeBetweenReadingsMs: Int64 = 0

  var isOn: Bool { return listener != nil }

  func start(timeBetweenReadingsMs: Int64, listener: LocationSourceListener) {
    timesStarted += 1
    self.timeBetweenReadingsMs = timeBetweenReadingsMs
    self.listener = listener
    lastListener = listener
  }

  func stop() {
    timesStopped += 1
    listener = nil
  }

  /// Returns false when the GPS is off and nothing was delivered.
  @discardableResult
  func deliver(_ readings: Reading...) -> Bool {
    guard let current = listener else { return false }
    current.onReadings(readings)
    return true
  }

  func fail() {
    listener?.onFailure()
  }
}

final class FakePhoneActivitySource: PhoneActivitySource {
  private var listener: PhoneActivitySourceListener?
  private(set) var timesStarted = 0

  var isOn: Bool { return listener != nil }

  func start(listener: PhoneActivitySourceListener) {
    timesStarted += 1
    self.listener = listener
  }

  func stop() {
    listener = nil
  }

  func report(_ activity: PhoneActivity, sinceMs: Int64) {
    listener?.onActivityChanged(activity, confidence: .high, sinceMs: sinceMs)
  }
}

/// The state of the phone, set by the test.
final class FakeDeviceState: DeviceStateSource {
  var state = phoneWhereEverythingWorks()
  func currentDeviceState() -> DeviceState { return state }
}

func phoneWhereEverythingWorks() -> DeviceState {
  return DeviceState(
    locationPermission: .granted,
    permissionPrecision: .precise,
    locationIsTurnedOn: true,
    locationProvider: .available,
    appIsInForeground: true,
    phoneActivity: .available
  )
}

/// The usual request: the address at latitude 0, longitude 0, radius 100 meters.
func usualRequest() -> RequestSettings {
  return RequestSettings(
    destinationLatitude: 0,
    destinationLongitude: 0,
    radiusMeters: 100,
    newestAgeThatCountsAsFreshMs: 10000,
    oldestAgeAllowedMs: 60000,
    worstGpsErrorAcceptedMeters: 50,
    fastestPossibleSpeedMps: 45,
    usePhoneActivity: true,
    allowAnswerFromHistory: true,
    mockPolicy: .zeroScore,
    totalTimeAllowedMs: 12000,
    firstAttemptTimeMs: 4500,
    extraTimeMs: 7000
  )
}

/// A reading some meters north of the address, taken at `capturedAtMs`.
/// The provider is the Android one on purpose: the expected numbers are the
/// same on every platform, and the engine does not treat providers apart.
func readingNorth(_ capturedAtMs: Int64, _ metersNorth: Double, _ gpsErrorMeters: Double = 8.0)
  -> Reading
{
  let degreesPerMeter = 180.0 / (3.14159265358979323846 * 6371008.8)
  return Reading(
    latitude: metersNorth * degreesPerMeter,
    longitude: 0,
    accuracyMeters: gpsErrorMeters,
    capturedAtMs: capturedAtMs,
    provider: .fused
  )
}
