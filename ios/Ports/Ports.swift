import Foundation

/*
 * The "ports": everything the coordinator needs from the outside world,
 * written as small protocols.
 *
 * The coordinator (EngineCoordinator.swift) only knows these protocols. It
 * does not know Core Location, permissions, dispatch queues or C. The real
 * pieces are in the Adapters folder; the tests plug in pretend ones (a GPS
 * that delivers what the test says, a clock the test moves by hand).
 */

/// The current time in milliseconds, on a clock that only moves forward.
protocol Clock {
  func nowMs() -> Int64
}

/// Something that was scheduled and can still be called off.
protocol Cancellable {
  func cancel()
}

/// Runs tasks one at a time, in the order they were given.
///
/// Everything that touches the engine goes through one of these. That is
/// what makes it safe: two things never use the engine at the same moment.
protocol SerialQueue {
  func post(_ task: @escaping () -> Void)

  /// Runs the task after the delay, unless it is cancelled before.
  func postDelayed(_ delayMs: Int64, _ task: @escaping () -> Void) -> Cancellable

  /// Runs one last task and then stops accepting tasks.
  func shutDownAfter(_ lastTask: @escaping () -> Void)
}

/// Who receives the GPS readings.
protocol LocationSourceListener: AnyObject {
  func onReadings(_ readings: [Reading])

  /// The source broke and will deliver nothing more.
  func onFailure()
}

/// Where the GPS readings come from.
protocol LocationSource: AnyObject {
  /// Starts delivering readings, about one every `timeBetweenReadingsMs`.
  /// Calling it while already started only changes the pace.
  func start(timeBetweenReadingsMs: Int64, listener: LocationSourceListener)

  /// Stops delivering. Safe to call when it is not started.
  func stop()
}

/// Who receives the reports about the phone activity.
protocol PhoneActivitySourceListener: AnyObject {
  /// "Since `sinceMs` the phone is doing this."
  func onActivityChanged(_ activity: PhoneActivity, confidence: ActivityConfidence, sinceMs: Int64)
}

/// Where reports about the phone activity come from.
protocol PhoneActivitySource: AnyObject {
  func start(listener: PhoneActivitySourceListener)
  func stop()
}

/// Permission, location switch, provider, foreground: read at one moment.
protocol DeviceStateSource: AnyObject {
  func currentDeviceState() -> DeviceState
}

/// What can go wrong when a request is started.
enum StartRefusal {
  /// The address or the radius makes no sense.
  case invalidRequest

  /// The engine could not take one more request.
  case engineIsFull
}

/// The answer to "start a request": an id, or why it was refused.
struct RequestStart {
  var requestId: Int
  var refusal: StartRefusal?
}

/// The engine, seen from Swift. One function for each function of lce.h.
/// The real one is CoreEngine.swift.
protocol CoreEnginePort: AnyObject {
  func openSession(_ settings: SessionSettings, device: DeviceState, nowMs: Int64) -> SessionOpened
  @discardableResult func clearSessionHistory(_ sessionId: Int) -> Bool
  func closeSession(_ sessionId: Int, nowMs: Int64)

  func startRequest(
    sessionId: Int, settings: RequestSettings, device: DeviceState, nowMs: Int64
  ) -> RequestStart
  func cancelRequest(_ requestId: Int, nowMs: Int64)

  func readingsArrived(_ readings: [Reading], nowMs: Int64)
  func phoneActivityChanged(
    _ activity: PhoneActivity, confidence: ActivityConfidence, sinceMs: Int64, nowMs: Int64)
  func phoneActivityUpdatesStopped(nowMs: Int64)
  func timePassed(nowMs: Int64)
  func appWentToBackground(nowMs: Int64)
  func locationPermissionWasLost(nowMs: Int64)
  func locationProviderFailed(nowMs: Int64)
  func setReadingsAreSimulated(_ simulated: Bool)

  func whatThePlatformMustDoNow() -> PlatformWork

  /// One finished report, or nil when none is waiting.
  func takeFinishedReport() -> Report?

  /// Destroys the engine. Nothing may be called afterwards.
  func destroy()
}
