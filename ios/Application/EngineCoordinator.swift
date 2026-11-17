import Foundation

/// The link between the phone and the engine.
///
/// The engine (C++) takes every decision, but it cannot touch the phone: it
/// has no GPS, no timer and no clock. This class is its hands. It does three
/// things and nothing else:
///
///   1. It TELLS the engine what happens:
///        the app asked for something      openSession(), startRequest(), cancel()...
///        the GPS delivered readings       (from the LocationSource)
///        the timer fired                  (from the SerialQueue)
///        the app went to the background   appWentToBackground()
///
///   2. Right after telling it, it OBEYS the engine: obeyTheEngine()
///        - takes the finished reports and hands each one to whoever asked;
///        - turns the GPS on or off, as the engine says;
///        - sets one timer for the moment the engine wants to be called again.
///
///   3. It keeps everything on ONE queue, so two things never use the engine
///      at the same moment.
///
/// It decides nothing about locations. There is no "if the reading is good"
/// anywhere in this file: that lives in the engine, and it is the same code
/// on Android and on iOS. This file itself is a line-by-line twin of
/// android/.../application/EngineCoordinator.kt.
///
/// THE ORDER OF THE CALLS, for one request:
///
///   startRequest()                               <- called from any thread
///     `-- queue.post                             now on the engine's queue
///           |-- engine.startRequest()
///           `-- obeyTheEngine()
///                 |-- engine.takeFinishedReport()        nothing yet
///                 |-- engine.whatThePlatformMustDoNow()  "GPS on, wake up in 4.5 s"
///                 |-- adjustLocationUpdates()            locationSource.start()
///                 `-- adjustTimer()                      queue.postDelayed()
///
///   the GPS delivers a reading                   <- called from the GPS's thread
///     `-- queue.post
///           |-- engine.readingsArrived()
///           `-- obeyTheEngine()
///                 |-- engine.takeFinishedReport()        the report! -> onReport()
///                 |-- engine.whatThePlatformMustDoNow()  "GPS off, no wake up"
///                 |-- adjustLocationUpdates()            locationSource.stop()
///                 `-- adjustTimer()                      the timer is cancelled
///
/// This class only knows the protocols of Ports.swift, so the tests run it
/// with a pretend GPS and a clock they move by hand.
final class EngineCoordinator {
  private let engine: CoreEnginePort
  private let queue: SerialQueue
  private let clock: Clock
  private let realLocationSource: LocationSource
  private let phoneActivitySource: PhoneActivitySource
  private let realDeviceState: DeviceStateSource

  /// Someone who is waiting for the report of a request.
  private struct Waiting {
    let idGivenByTheApp: String
    let onReport: (Report) -> Void
  }

  // Everything below is only touched from the queue.

  /// Who is waiting, by the request id the engine gave.
  private var waitingByRequestId: [Int: Waiting] = [:]

  /// The engine's request id, by the id the app gave (the app cancels by its own id).
  private var requestIdByAppId: [String: Int] = [:]

  // How the phone is right now, so we only change what needs to change.
  private var locationUpdatesAreOn = false
  private var timeBetweenReadingsMs: Int64 = 0
  private var activityUpdatesAreOn = false
  private var timer: Cancellable?
  private var timerIsSetForMs: Int64 = 0

  // Each time the GPS is (re)started this number grows. A delivery carries
  // the number it was started with; one that arrives with an old number comes
  // from a GPS we already turned off, and is ignored.
  private var locationUpdatesNumber = 0
  private var activityUpdatesNumber = 0

  // Simulation: the app feeds readings of its own instead of the GPS.
  private var simulationIsOn = false
  private let simulatedLocationSource = SimulatedLocationSource()

  private var locationWasGrantedLastTime = false
  private var isShutDown = false

  init(
    engine: CoreEnginePort,
    queue: SerialQueue,
    clock: Clock,
    realLocationSource: LocationSource,
    phoneActivitySource: PhoneActivitySource,
    realDeviceState: DeviceStateSource
  ) {
    self.engine = engine
    self.queue = queue
    self.clock = clock
    self.realLocationSource = realLocationSource
    self.phoneActivitySource = phoneActivitySource
    self.realDeviceState = realDeviceState
  }

  // MARK: What the app asks for

  func openSession(_ settings: SessionSettings, onOpened: @escaping (SessionOpened) -> Void) {
    queue.post {
      if self.isShutDown { return }
      let opened = self.engine.openSession(
        settings, device: self.currentDeviceState(), nowMs: self.clock.nowMs())
      self.obeyTheEngine()
      onOpened(opened)
    }
  }

  func clearHistory(_ sessionId: Int, onDone: @escaping () -> Void) {
    queue.post {
      if self.isShutDown { return }
      self.engine.clearSessionHistory(sessionId)
      self.obeyTheEngine()
      onDone()
    }
  }

  func closeSession(_ sessionId: Int, onDone: @escaping () -> Void) {
    queue.post {
      if self.isShutDown { return }
      self.engine.closeSession(sessionId, nowMs: self.clock.nowMs())
      self.obeyTheEngine()
      onDone()
    }
  }

  /// Starts a request.
  ///
  /// Exactly one of the two callbacks is called, exactly once:
  ///   `onReport`  with the final report, sooner or later;
  ///   `onRefused` at once, when the engine did not accept the request.
  /// Neither is called after shutDown().
  func startRequest(
    idGivenByTheApp: String,
    sessionId: Int,
    settings: RequestSettings,
    onReport: @escaping (Report) -> Void,
    onRefused: @escaping (StartRefusal) -> Void
  ) {
    queue.post {
      if self.isShutDown { return }

      let started = self.engine.startRequest(
        sessionId: sessionId, settings: settings, device: self.currentDeviceState(),
        nowMs: self.clock.nowMs())

      if let refusal = started.refusal {
        onRefused(refusal)
        return
      }

      // Remember who is waiting BEFORE obeying: the report may already be there.
      self.waitingByRequestId[started.requestId] = Waiting(
        idGivenByTheApp: idGivenByTheApp, onReport: onReport)
      self.requestIdByAppId[idGivenByTheApp] = started.requestId
      self.obeyTheEngine()
    }
  }

  /// Cancels a request by the id the app gave. Unknown ids are ignored.
  func cancel(_ idGivenByTheApp: String) {
    queue.post {
      if self.isShutDown { return }
      guard let requestId = self.requestIdByAppId[idGivenByTheApp] else { return }
      self.engine.cancelRequest(requestId, nowMs: self.clock.nowMs())
      self.obeyTheEngine()
    }
  }

  // MARK: What happens to the app

  func appWentToBackground() {
    queue.post {
      if self.isShutDown { return }
      self.engine.appWentToBackground(nowMs: self.clock.nowMs())
      self.obeyTheEngine()
    }
  }

  /// The app is visible again. Nothing restarts by itself. We only check
  /// whether the user took the location permission away while the app was
  /// not visible.
  func appCameToForeground() {
    queue.post {
      if self.isShutDown { return }

      // Looking at the phone also updates `locationWasGrantedLastTime`, so
      // the value from before is kept aside first.
      let wasGrantedBefore = self.locationWasGrantedLastTime
      let real = self.realDeviceState.currentDeviceState()
      self.locationWasGrantedLastTime = (real.locationPermission == .granted)
      let isGrantedNow = self.locationWasGrantedLastTime

      if wasGrantedBefore && !isGrantedNow {
        self.engine.locationPermissionWasLost(nowMs: self.clock.nowMs())
        self.obeyTheEngine()
      }
    }
  }

  // MARK: Simulation (demos and tests)

  /// Turns the simulation on or off. While it is on, the real GPS is never
  /// used and no permission is needed: the readings are the ones the app
  /// gives to simulateReadings(). Every report then carries the reason
  /// "simulated_location_source".
  func setSimulationEnabled(_ enabled: Bool, onDone: @escaping () -> Void) {
    queue.post {
      if self.isShutDown { return }
      if enabled != self.simulationIsOn {
        // Stop whichever source is running; obeyTheEngine() starts the other
        // one if the engine still wants readings.
        self.stopLocationUpdates()
        self.simulationIsOn = enabled
        self.engine.setReadingsAreSimulated(enabled)
        if !enabled {
          // What the phone "was doing" in the simulation is not true anymore.
          self.engine.phoneActivityUpdatesStopped(nowMs: self.clock.nowMs())
        }
        self.obeyTheEngine()
      }
      onDone()
    }
  }

  /// Hands made-up readings to the engine, as if the GPS had delivered them.
  func simulateReadings(_ readings: [Reading], onDone: @escaping () -> Void) {
    queue.post {
      if self.isShutDown { return }
      if self.simulationIsOn {
        self.simulatedLocationSource.deliver(readings)
      }
      onDone()
    }
  }

  /// Says what the phone "is doing" in the simulation. `nil` means the phone
  /// stopped reporting its activity.
  func simulatePhoneActivity(
    _ activity: PhoneActivity?, confidence: ActivityConfidence, onDone: @escaping () -> Void
  ) {
    queue.post {
      if self.isShutDown { return }
      if self.simulationIsOn {
        let nowMs = self.clock.nowMs()
        if let activity = activity {
          self.engine.phoneActivityChanged(
            activity, confidence: confidence, sinceMs: nowMs, nowMs: nowMs)
        } else {
          self.engine.phoneActivityUpdatesStopped(nowMs: nowMs)
        }
        self.obeyTheEngine()
      }
      onDone()
    }
  }

  func isSimulationOn(onAnswer: @escaping (Bool) -> Void) {
    queue.post { onAnswer(self.simulationIsOn) }
  }

  /// A snapshot of the phone, as the engine would be told right now. Used by
  /// getCapabilities(); it turns nothing on.
  func readDeviceState(onAnswer: @escaping (DeviceState) -> Void) {
    queue.post { onAnswer(self.realDeviceState.currentDeviceState()) }
  }

  // MARK: The end

  /// Stops everything and destroys the engine. Called when React Native tears
  /// the module down. Requests that were still running get NO answer: the
  /// JavaScript side that was waiting for them no longer exists.
  func shutDown() {
    queue.shutDownAfter {
      self.isShutDown = true
      self.stopLocationUpdates()
      self.stopActivityUpdates()
      self.timer?.cancel()
      self.timer = nil
      self.waitingByRequestId.removeAll()
      self.requestIdByAppId.removeAll()
      self.engine.destroy()
    }
  }

  // MARK: Obeying the engine

  /// Called after EVERY call into the engine. It takes the finished reports
  /// and makes the phone match what the engine wants right now.
  private func obeyTheEngine() {
    // 1. Finished reports go to whoever is waiting for them.
    while let report = engine.takeFinishedReport() {
      guard let waiting = waitingByRequestId.removeValue(forKey: report.requestId) else {
        continue
      }
      requestIdByAppId.removeValue(forKey: waiting.idGivenByTheApp)
      waiting.onReport(report)
    }

    // 2. The GPS, the activity updates and the timer.
    let work = engine.whatThePlatformMustDoNow()
    adjustLocationUpdates(work)
    adjustActivityUpdates(work)
    adjustTimer(work)
  }

  private func adjustLocationUpdates(_ work: PlatformWork) {
    let mustStart = work.locationUpdatesAreWanted && !locationUpdatesAreOn
    let mustChangeThePace =
      work.locationUpdatesAreWanted && locationUpdatesAreOn
      && work.timeBetweenLocationUpdatesMs != timeBetweenReadingsMs
    let mustStop = !work.locationUpdatesAreWanted && locationUpdatesAreOn

    if mustStart || mustChangeThePace {
      startLocationUpdates(work.timeBetweenLocationUpdatesMs)
    } else if mustStop {
      stopLocationUpdates()
    }
  }

  private func startLocationUpdates(_ timeBetweenMs: Int64) {
    locationUpdatesNumber += 1
    let startedWithNumber = locationUpdatesNumber

    // The GPS calls these from its own thread: go to the queue first.
    let listener = ClosureLocationListener(
      onReadings: { [weak self] readings in
        guard let self = self else { return }
        self.queue.post {
          let comesFromTheCurrentStart = (startedWithNumber == self.locationUpdatesNumber)
          if self.isShutDown || !self.locationUpdatesAreOn || !comesFromTheCurrentStart {
            return  // a late delivery from a GPS we already turned off
          }
          self.engine.readingsArrived(readings, nowMs: self.clock.nowMs())
          self.obeyTheEngine()
        }
      },
      onFailure: { [weak self] in
        guard let self = self else { return }
        self.queue.post {
          let comesFromTheCurrentStart = (startedWithNumber == self.locationUpdatesNumber)
          if self.isShutDown || !self.locationUpdatesAreOn || !comesFromTheCurrentStart {
            return
          }
          self.engine.locationProviderFailed(nowMs: self.clock.nowMs())
          self.obeyTheEngine()
        }
      })

    currentLocationSource().start(timeBetweenReadingsMs: timeBetweenMs, listener: listener)
    currentLocationListener = listener
    locationUpdatesAreOn = true
    timeBetweenReadingsMs = timeBetweenMs
  }

  // The sources hold their listener weakly or strongly, as they prefer. The
  // coordinator keeps the current ones alive itself.
  private var currentLocationListener: ClosureLocationListener?
  private var currentActivityListener: ClosureActivityListener?

  private func stopLocationUpdates() {
    if locationUpdatesAreOn {
      currentLocationSource().stop()
      currentLocationListener = nil
      locationUpdatesAreOn = false
      locationUpdatesNumber += 1  // anything still on its way is now "old"
    }
  }

  private func adjustActivityUpdates(_ work: PlatformWork) {
    // In the simulation the app says what the phone is doing; the real
    // sensor is left alone.
    let wanted = work.phoneActivityUpdatesAreWanted && !simulationIsOn

    if wanted && !activityUpdatesAreOn {
      activityUpdatesNumber += 1
      let startedWithNumber = activityUpdatesNumber

      let listener = ClosureActivityListener { [weak self] activity, confidence, sinceMs in
        guard let self = self else { return }
        self.queue.post {
          let comesFromTheCurrentStart = (startedWithNumber == self.activityUpdatesNumber)
          if self.isShutDown || !self.activityUpdatesAreOn || !comesFromTheCurrentStart {
            return
          }
          self.engine.phoneActivityChanged(
            activity, confidence: confidence, sinceMs: sinceMs, nowMs: self.clock.nowMs())
          self.obeyTheEngine()
        }
      }
      phoneActivitySource.start(listener: listener)
      currentActivityListener = listener
      activityUpdatesAreOn = true
    } else if !wanted && activityUpdatesAreOn {
      stopActivityUpdates()
      // From now on nobody is watching: the activity is unknown again.
      engine.phoneActivityUpdatesStopped(nowMs: clock.nowMs())
    }
  }

  private func stopActivityUpdates() {
    if activityUpdatesAreOn {
      phoneActivitySource.stop()
      currentActivityListener = nil
      activityUpdatesAreOn = false
      activityUpdatesNumber += 1
    }
  }

  /// There is one timer for the whole engine: "call me again at this moment".
  private func adjustTimer(_ work: PlatformWork) {
    let alreadySetForThatMoment = (timer != nil && timerIsSetForMs == work.wakeUpAtMs)

    if !work.wakeUpIsWanted {
      timer?.cancel()
      timer = nil
    } else if !alreadySetForThatMoment {
      timer?.cancel()

      let delayMs = max(0, work.wakeUpAtMs - clock.nowMs())
      timerIsSetForMs = work.wakeUpAtMs
      timer = queue.postDelayed(delayMs) { [weak self] in
        guard let self = self else { return }
        if self.isShutDown { return }
        self.timer = nil
        self.engine.timePassed(nowMs: self.clock.nowMs())
        self.obeyTheEngine()
      }
    }
  }

  // MARK: Helpers

  private func currentLocationSource() -> LocationSource {
    return simulationIsOn ? simulatedLocationSource : realLocationSource
  }

  /// What the engine is told about the phone. In the simulation everything
  /// "works": the readings do not come from the phone, so its permission and
  /// its GPS do not matter. Only the foreground state stays real.
  private func currentDeviceState() -> DeviceState {
    let real = realDeviceState.currentDeviceState()
    locationWasGrantedLastTime = (real.locationPermission == .granted)

    if !simulationIsOn {
      return real
    }
    return DeviceState(
      locationPermission: .granted,
      permissionPrecision: .precise,
      locationIsTurnedOn: true,
      locationProvider: .available,
      appIsInForeground: real.appIsInForeground,
      phoneActivity: .available
    )
  }
}

/// A listener made of two closures.
private final class ClosureLocationListener: LocationSourceListener {
  private let whenReadings: ([Reading]) -> Void
  private let whenFailure: () -> Void

  init(onReadings: @escaping ([Reading]) -> Void, onFailure: @escaping () -> Void) {
    whenReadings = onReadings
    whenFailure = onFailure
  }

  func onReadings(_ readings: [Reading]) {
    whenReadings(readings)
  }

  func onFailure() {
    whenFailure()
  }
}

/// A listener made of one closure.
private final class ClosureActivityListener: PhoneActivitySourceListener {
  private let whenChanged: (PhoneActivity, ActivityConfidence, Int64) -> Void

  init(_ whenChanged: @escaping (PhoneActivity, ActivityConfidence, Int64) -> Void) {
    self.whenChanged = whenChanged
  }

  func onActivityChanged(_ activity: PhoneActivity, confidence: ActivityConfidence, sinceMs: Int64) {
    whenChanged(activity, confidence, sinceMs)
  }
}

/// The pretend GPS of the simulation. It delivers what deliver() is given,
/// and only while it is started, like a real GPS.
private final class SimulatedLocationSource: LocationSource {
  private var listener: LocationSourceListener?

  func start(timeBetweenReadingsMs: Int64, listener: LocationSourceListener) {
    self.listener = listener
  }

  func stop() {
    listener = nil
  }

  func deliver(_ readings: [Reading]) {
    listener?.onReadings(readings)
  }
}
