import XCTest

@testable import LocationConfidenceHost

/// The coordinator with the REAL engine and pretend pieces around it: a GPS
/// that delivers what the test says, a clock the test moves by hand, a queue
/// with no thread.
///
/// What is checked here is the part Swift is responsible for:
///   - the GPS is turned on and off exactly when the engine says;
///   - nothing is left running after a request ends, however it ends;
///   - each request gets exactly one answer;
///   - a delivery that arrives too late changes nothing.
///
/// These are the same tests as android/src/sharedTest/.../EngineCoordinatorTest.kt.
final class EngineCoordinatorTests: XCTestCase {
  private var clock: FakeClock!
  private var queue: ManualQueue!
  private var gps: FakeLocationSource!
  private var activitySensor: FakePhoneActivitySource!
  private var device: FakeDeviceState!
  private var coordinator: EngineCoordinator!

  /// Every report that came back, with the id the app gave.
  private var reports: [(appId: String, report: Report)] = []
  private var refusals: [StartRefusal] = []

  override func setUp() {
    clock = FakeClock()
    queue = ManualQueue(clock: clock)
    gps = FakeLocationSource()
    activitySensor = FakePhoneActivitySource()
    device = FakeDeviceState()
    reports = []
    refusals = []

    guard let engine = CoreEngine.createOrNil() else {
      return XCTFail("the engine could not be created")
    }
    coordinator = EngineCoordinator(
      engine: engine, queue: queue, clock: clock, realLocationSource: gps,
      phoneActivitySource: activitySensor, realDeviceState: device)
  }

  override func tearDown() {
    coordinator.shutDown()
  }

  private func startRequest(
    _ appId: String, sessionId: Int = 0, settings: RequestSettings = usualRequest()
  ) {
    coordinator.startRequest(
      idGivenByTheApp: appId, sessionId: sessionId, settings: settings,
      onReport: { self.reports.append((appId, $0)) },
      onRefused: { self.refusals.append($0) })
  }

  private func reportOf(_ appId: String, file: StaticString = #filePath, line: UInt = #line)
    -> Report
  {
    guard let found = reports.first(where: { $0.appId == appId }) else {
      XCTFail("no report for \(appId)", file: file, line: line)
      // A report that matches nothing, so the test goes on and fails clearly.
      return CoreEngineTestsSupport.emptyReport
    }
    return found.report
  }

  private func howManyReportsOf(_ appId: String) -> Int {
    return reports.filter { $0.appId == appId }.count
  }

  private func openSession(keepCollecting: Bool, timeBetweenMs: Int64 = 5000) -> SessionOpened {
    var opened: SessionOpened?
    coordinator.openSession(
      SessionSettings(
        keepCollectingReadings: keepCollecting, timeBetweenReadingsMs: timeBetweenMs,
        longestCollectingTimeMs: 900000)
    ) { opened = $0 }
    return opened ?? SessionOpened(sessionId: 0, collecting: .notRequested, reasons: [])
  }

  /// Nothing is left running: no GPS, no activity updates, no timer.
  private func everythingIsQuiet() -> Bool {
    return !gps.isOn && !activitySensor.isOn && queue.howManyTimersAreSet == 0
  }

  // MARK: One request

  func test_REC_001_the_GPS_goes_on_a_good_reading_arrives_everything_goes_off() {
    XCTAssertTrue(everythingIsQuiet())

    startRequest("a")

    XCTAssertTrue(gps.isOn)
    XCTAssertEqual(gps.timeBetweenReadingsMs, 1000)
    XCTAssertTrue(activitySensor.isOn)
    XCTAssertEqual(queue.howManyTimersAreSet, 1)
    XCTAssertTrue(reports.isEmpty)

    clock.now = 1200
    XCTAssertTrue(gps.deliver(readingNorth(1200, 20.0)))

    XCTAssertEqual(reports.count, 1)
    let report = reportOf("a")
    XCTAssertEqual(report.status, 0)  // available
    XCTAssertEqual(report.source, 0)  // fresh
    XCTAssertEqual(report.durationMs, 1200)

    XCTAssertTrue(everythingIsQuiet())
    XCTAssertEqual(gps.timesStarted, 1)
    XCTAssertEqual(gps.timesStopped, 1)
  }

  func test_REC_005_REC_011_with_no_reading_the_timers_alone_end_the_request() {
    startRequest("a")

    queue.advanceTo(4500)  // the first attempt is over
    XCTAssertTrue(reports.isEmpty)
    XCTAssertTrue(gps.isOn)
    XCTAssertEqual(queue.howManyTimersAreSet, 1)

    queue.advanceTo(11500)  // the extra time is over
    XCTAssertEqual(reports.count, 1)
    XCTAssertEqual(reportOf("a").status, 2)  // unavailable
    XCTAssertEqual(reportOf("a").durationMs, 11500)
    XCTAssertTrue(reportOf("a").reasons.contains("initial_fix_timeout"))
    XCTAssertTrue(reportOf("a").reasons.contains("recovery_window_timeout"))

    XCTAssertTrue(everythingIsQuiet())
  }

  func test_CON_002_CON_006_cancelling_ends_it_once_and_cancelling_again_does_nothing() {
    startRequest("a")
    clock.now = 2000

    coordinator.cancel("a")

    XCTAssertEqual(reports.count, 1)
    XCTAssertEqual(reportOf("a").status, 3)  // cancelled
    XCTAssertTrue(reportOf("a").reasons.contains("request_cancelled"))
    XCTAssertTrue(everythingIsQuiet())

    coordinator.cancel("a")
    coordinator.cancel("an id nobody gave")
    XCTAssertEqual(reports.count, 1)
  }

  func test_CON_018_a_delivery_that_arrives_after_the_GPS_was_turned_off_is_ignored() {
    startRequest("a")
    guard let listenerOfTheStoppedGps = gps.lastListener else {
      return XCTFail("the GPS was not started")
    }
    clock.now = 1000
    gps.deliver(readingNorth(1000, 20.0))
    XCTAssertEqual(reports.count, 1)
    XCTAssertFalse(gps.isOn)

    // The system still had something on its way.
    listenerOfTheStoppedGps.onReadings([readingNorth(1500, 20.0)])
    listenerOfTheStoppedGps.onReadings([readingNorth(1500, 20.0)])
    listenerOfTheStoppedGps.onFailure()

    XCTAssertEqual(reports.count, 1)
    XCTAssertTrue(everythingIsQuiet())

    // And it does not leak into the next request either.
    startRequest("b")
    listenerOfTheStoppedGps.onReadings([readingNorth(1600, 20.0)])
    XCTAssertEqual(howManyReportsOf("b"), 0)
    XCTAssertTrue(gps.isOn)
  }

  func test_CON_019_CON_020_a_timer_and_a_reading_that_race_give_one_answer() {
    startRequest("a")
    guard let listener = gps.lastListener else {
      return XCTFail("the GPS was not started")
    }
    queue.advanceTo(4500)
    queue.advanceTo(11500)
    XCTAssertEqual(reports.count, 1)

    listener.onReadings([readingNorth(11600, 20.0)])  // too late
    queue.advanceTo(60000)

    XCTAssertEqual(reports.count, 1)
    XCTAssertEqual(reportOf("a").status, 2)
  }

  func test_GEO_012_a_request_that_makes_no_sense_is_refused_and_turns_nothing_on() {
    var broken = usualRequest()
    broken.destinationLatitude = 91.0
    startRequest("a", settings: broken)

    XCTAssertEqual(refusals, [.invalidRequest])
    XCTAssertTrue(reports.isEmpty)
    XCTAssertEqual(gps.timesStarted, 0)
    XCTAssertTrue(everythingIsQuiet())
  }

  // MARK: Several requests

  func test_CON_013_two_requests_share_one_GPS_and_cancelling_one_keeps_it_on() {
    startRequest("a")
    startRequest("b")
    XCTAssertEqual(gps.timesStarted, 1)

    coordinator.cancel("a")
    XCTAssertEqual(reportOf("a").status, 3)
    XCTAssertTrue(gps.isOn)
    XCTAssertEqual(gps.timesStopped, 0)

    clock.now = 1000
    gps.deliver(readingNorth(1000, 20.0))
    XCTAssertEqual(reportOf("b").status, 0)
    XCTAssertEqual(howManyReportsOf("a"), 1)
    XCTAssertEqual(howManyReportsOf("b"), 1)
    XCTAssertTrue(everythingIsQuiet())
    XCTAssertEqual(gps.timesStopped, 1)
  }

  func test_CON_012_two_requests_with_different_addresses_get_different_answers() {
    startRequest("near")
    var farAway = usualRequest()
    farAway.destinationLongitude = 0.01
    farAway.radiusMeters = 50.0
    startRequest("far", settings: farAway)

    clock.now = 1000
    gps.deliver(readingNorth(1000, 20.0))

    XCTAssertEqual(reportOf("near").relation, 0)  // supports inside
    XCTAssertEqual(reportOf("near").radiusMeters, 100.0)
    XCTAssertEqual(reportOf("far").relation, 1)  // supports outside
    XCTAssertEqual(reportOf("far").radiusMeters, 50.0)
  }

  func test_CON_016_a_hundred_requests_cancelled_one_answer_each_and_nothing_left_on() {
    for round in 0..<100 {
      let id = "request-\(round)"
      startRequest(id)
      clock.now += 300
      coordinator.cancel(id)
      gps.lastListener?.onReadings([readingNorth(clock.now, 20.0)])  // late
      XCTAssertEqual(howManyReportsOf(id), 1)
      XCTAssertTrue(everythingIsQuiet())
    }
    XCTAssertEqual(reports.count, 100)
    XCTAssertEqual(gps.timesStarted, 100)
    XCTAssertEqual(gps.timesStopped, 100)
  }

  // MARK: The state of the phone

  func test_PLT_018_PLT_019_when_the_GPS_cannot_be_used_it_is_never_turned_on() {
    func phone(_ change: (inout DeviceState) -> Void) -> DeviceState {
      var state = phoneWhereEverythingWorks()
      change(&state)
      return state
    }
    let broken: [(DeviceState, String)] = [
      (phone { $0.locationPermission = .denied }, "permission_denied"),
      (phone { $0.locationPermission = .notDetermined }, "permission_not_determined"),
      (phone { $0.locationPermission = .restricted }, "permission_restricted"),
      (phone { $0.locationIsTurnedOn = false }, "location_services_disabled"),
      (phone { $0.locationProvider = .unavailable }, "native_provider_unavailable"),
      (phone { $0.appIsInForeground = false }, "foreground_required"),
    ]

    for (index, entry) in broken.enumerated() {
      device.state = entry.0
      startRequest("case-\(index)")

      XCTAssertEqual(reportOf("case-\(index)").status, 2)  // unavailable
      XCTAssertTrue(reportOf("case-\(index)").reasons.contains(entry.1), entry.1)
    }
    XCTAssertEqual(gps.timesStarted, 0)
    XCTAssertTrue(everythingIsQuiet())
  }

  func test_CON_009_CON_010_CON_011_the_background_ends_the_request_and_nothing_restarts() {
    let session = openSession(keepCollecting: true)
    startRequest("a", sessionId: session.sessionId)
    XCTAssertTrue(gps.isOn)

    coordinator.appWentToBackground()

    XCTAssertEqual(reportOf("a").status, 3)
    XCTAssertTrue(reportOf("a").reasons.contains("app_backgrounded"))
    XCTAssertTrue(everythingIsQuiet())
    let timesStartedSoFar = gps.timesStarted

    coordinator.appCameToForeground()
    queue.advanceTo(600000)
    XCTAssertTrue(everythingIsQuiet())
    XCTAssertEqual(gps.timesStarted, timesStartedSoFar)  // collecting did not start again
  }

  func test_a_permission_taken_away_while_the_app_was_not_visible_wipes_what_was_remembered() {
    let session = openSession(keepCollecting: true)
    gps.deliver(readingNorth(0, 20.0))

    // The user goes to the settings and takes the permission away.
    coordinator.appWentToBackground()
    var denied = phoneWhereEverythingWorks()
    denied.locationPermission = .denied
    device.state = denied
    coordinator.appCameToForeground()

    // The user gives it back. The reading from before must not be there.
    device.state = phoneWhereEverythingWorks()
    clock.now = 2000
    startRequest("a", sessionId: session.sessionId)
    queue.advanceTo(30000)

    XCTAssertEqual(reportOf("a").status, 2)  // unavailable: nothing remembered
    XCTAssertEqual(reportOf("a").readingsExamined, 0)
  }

  func test_REC_023_when_the_GPS_fails_the_request_ends_and_the_GPS_is_turned_off() {
    startRequest("a")

    gps.fail()

    XCTAssertEqual(reports.count, 1)
    XCTAssertTrue(reportOf("a").reasons.contains("native_provider_unavailable"))
    XCTAssertTrue(everythingIsQuiet())
  }

  // MARK: Sessions

  func test_SES_001_a_usual_session_turns_nothing_on() {
    let session = openSession(keepCollecting: false)

    XCTAssertEqual(session.sessionId, 1)
    XCTAssertTrue(everythingIsQuiet())
    XCTAssertEqual(gps.timesStarted, 0)
  }

  func test_SES_002_SES_008_SES_011_a_collecting_session_keeps_the_GPS_on_at_its_own_pace() {
    let session = openSession(keepCollecting: true, timeBetweenMs: 5000)
    XCTAssertTrue(gps.isOn)
    XCTAssertEqual(gps.timeBetweenReadingsMs, 5000)
    XCTAssertFalse(activitySensor.isOn)  // collecting alone does not need the activity

    // A request asks for readings faster; the same GPS is used.
    startRequest("a", sessionId: session.sessionId)
    XCTAssertEqual(gps.timeBetweenReadingsMs, 1000)
    XCTAssertTrue(activitySensor.isOn)

    clock.now = 1000
    gps.deliver(readingNorth(1000, 20.0))
    XCTAssertEqual(reportOf("a").status, 0)

    // Back to the pace of the session.
    XCTAssertTrue(gps.isOn)
    XCTAssertEqual(gps.timeBetweenReadingsMs, 5000)
    XCTAssertFalse(activitySensor.isOn)

    var closed = false
    coordinator.closeSession(session.sessionId) { closed = true }
    XCTAssertTrue(closed)
    XCTAssertTrue(everythingIsQuiet())
  }

  func test_CON_007_closing_a_session_ends_its_requests() {
    let session = openSession(keepCollecting: false)
    startRequest("a", sessionId: session.sessionId)

    coordinator.closeSession(session.sessionId) {}

    XCTAssertEqual(reportOf("a").status, 3)
    XCTAssertTrue(reportOf("a").reasons.contains("session_stopped"))
    XCTAssertTrue(everythingIsQuiet())
  }

  func test_CON_015_a_hundred_sessions_opened_and_closed_leave_nothing_on() {
    for round in 0..<100 {
      let session = openSession(keepCollecting: true)
      gps.deliver(readingNorth(clock.now, 20.0))
      startRequest("request-\(round)", sessionId: session.sessionId)
      clock.now += 500
      coordinator.closeSession(session.sessionId) {}
      XCTAssertTrue(everythingIsQuiet())
    }
    XCTAssertEqual(reports.count, 100)
  }

  // MARK: The phone activity

  func test_what_the_activity_sensor_reports_reaches_the_report() {
    startRequest("a")
    XCTAssertTrue(activitySensor.isOn)

    activitySensor.report(.walking, sinceMs: 0)
    clock.now = 1000
    gps.deliver(readingNorth(1000, 20.0))

    XCTAssertEqual(reportOf("a").activityNow, 1)  // walking
    XCTAssertEqual(reportOf("a").activityConfidenceNow, 0)  // high
    XCTAssertFalse(activitySensor.isOn)
  }

  func test_PLT_024_without_the_activity_permission_the_sensor_is_never_touched() {
    // Touching the sensor before the user was asked would show the system's
    // dialog in the middle of a request.
    var notAsked = phoneWhereEverythingWorks()
    notAsked.phoneActivity = .permissionNotAsked
    device.state = notAsked

    startRequest("a")
    XCTAssertEqual(activitySensor.timesStarted, 0)

    clock.now = 1000
    gps.deliver(readingNorth(1000, 20.0))
    XCTAssertEqual(reportOf("a").status, 0)  // the GPS part works as usual
    XCTAssertEqual(reportOf("a").activityNow, 5)  // unknown
    XCTAssertEqual(activitySensor.timesStarted, 0)
  }

  // MARK: Simulation

  func test_in_the_simulation_the_real_GPS_is_never_used_and_no_permission_is_needed() {
    var denied = phoneWhereEverythingWorks()
    denied.locationPermission = .denied
    device.state = denied
    coordinator.setSimulationEnabled(true) {}

    startRequest("simulated")
    XCTAssertEqual(gps.timesStarted, 0)
    XCTAssertEqual(queue.howManyTimersAreSet, 1)

    clock.now = 1000
    coordinator.simulateReadings([readingNorth(1000, 20.0)]) {}

    let report = reportOf("simulated")
    XCTAssertEqual(report.status, 0)  // available
    XCTAssertTrue(report.reasons.contains("simulated_location_source"))
    XCTAssertEqual(queue.howManyTimersAreSet, 0)

    // Readings given when nothing is listening are dropped, like a GPS that is off.
    coordinator.simulateReadings([readingNorth(1500, 20.0)]) {}

    // Out of the simulation the real state of the phone counts again.
    coordinator.setSimulationEnabled(false) {}
    startRequest("real")
    XCTAssertEqual(reportOf("real").status, 2)
    XCTAssertTrue(reportOf("real").reasons.contains("permission_denied"))
    XCTAssertFalse(reportOf("real").reasons.contains("simulated_location_source"))
    XCTAssertEqual(gps.timesStarted, 0)
  }

  func test_in_the_simulation_the_app_also_says_what_the_phone_is_doing() {
    coordinator.setSimulationEnabled(true) {}
    let session = openSession(keepCollecting: true)

    coordinator.simulateReadings([readingNorth(0, 20.0, 5.0)]) {}
    coordinator.simulatePhoneActivity(.standingStill, confidence: .high) {}

    // 10 seconds later a request gets no new reading.
    clock.now = 10000
    startRequest("a", sessionId: session.sessionId)
    XCTAssertFalse(activitySensor.isOn)  // the real sensor is left alone
    queue.advanceTo(14500)

    let report = reportOf("a")
    XCTAssertEqual(report.source, 2)  // cached
    XCTAssertTrue(report.standingStillSinceTheReading)
    XCTAssertEqual(report.howFarPhoneCouldBeMeters, 5.0 + 0.5 * 14.5, accuracy: 1e-9)

    // "The phone stopped reporting": from now on its activity is unknown.
    coordinator.simulatePhoneActivity(nil, confidence: .high) {}
    clock.now = 20000
    startRequest("b", sessionId: session.sessionId)
    queue.advanceTo(40000)
    XCTAssertFalse(reportOf("b").standingStillSinceTheReading)
  }

  func test_turning_the_simulation_on_stops_a_real_GPS_that_was_running() {
    _ = openSession(keepCollecting: true)
    XCTAssertTrue(gps.isOn)

    coordinator.setSimulationEnabled(true) {}

    XCTAssertFalse(gps.isOn)
    var isOn = false
    coordinator.isSimulationOn { isOn = $0 }
    XCTAssertTrue(isOn)
  }

  // MARK: The end

  func test_CON_017_CON_028_after_shut_down_nobody_is_answered_and_nothing_runs() {
    startRequest("a")
    guard let listener = gps.lastListener else {
      return XCTFail("the GPS was not started")
    }

    coordinator.shutDown()

    XCTAssertTrue(reports.isEmpty)  // JavaScript is gone: there is nobody to answer
    XCTAssertFalse(gps.isOn)
    XCTAssertFalse(activitySensor.isOn)
    XCTAssertTrue(queue.isShutDown)

    // Whatever arrives afterwards is ignored and nothing crashes.
    listener.onReadings([readingNorth(1000, 20.0)])
    startRequest("b")
    coordinator.cancel("a")
    coordinator.appWentToBackground()
    coordinator.shutDown()
    XCTAssertTrue(reports.isEmpty)
  }

  // MARK: Many threads

  func test_CON_023_calls_from_many_threads_are_put_in_line_one_answer_each() {
    // The real queue (off the main thread) and the real clock this time.
    let realQueue = DispatchSerialQueue()
    let realClock = MonotonicClock()
    guard let engine = CoreEngine.createOrNil() else {
      return XCTFail("the engine could not be created")
    }
    let busyCoordinator = EngineCoordinator(
      engine: engine, queue: realQueue, clock: realClock,
      realLocationSource: FakeLocationSource(), phoneActivitySource: FakePhoneActivitySource(),
      realDeviceState: device)
    busyCoordinator.setSimulationEnabled(true) {}

    let threads = 4
    let perThread = 250
    let lock = NSLock()
    var answers: [String: Int] = [:]
    let allAnswered = expectation(description: "every request is answered")
    allAnswered.expectedFulfillmentCount = threads * perThread

    for thread in 0..<threads {
      Thread.detachNewThread {
        for count in 0..<perThread {
          let id = "t\(thread)-\(count)"
          let answered = {
            lock.lock()
            answers[id, default: 0] += 1
            lock.unlock()
            allAnswered.fulfill()
          }
          busyCoordinator.startRequest(
            idGivenByTheApp: id, sessionId: 0, settings: usualRequest(),
            onReport: { _ in answered() }, onRefused: { _ in answered() })
          if count % 2 == 0 {
            busyCoordinator.simulateReadings([readingNorth(realClock.nowMs(), 20.0)]) {}
          }
          busyCoordinator.cancel(id)
        }
      }
    }

    wait(for: [allAnswered], timeout: 30)
    lock.lock()
    XCTAssertEqual(answers.count, threads * perThread)
    XCTAssertTrue(answers.values.allSatisfy { $0 == 1 })
    lock.unlock()

    busyCoordinator.shutDown()
  }
}

enum CoreEngineTestsSupport {
  /// A report with nothing in it, for a test that already failed.
  static let emptyReport = Report(
    requestId: -1, sessionId: -1, status: -1, source: -1, hasPosition: false, latitude: 0,
    longitude: 0, distanceToDestinationMeters: 0, gpsErrorMeters: 0, howFarPhoneCouldBeMeters: 0,
    readingAgeMs: 0, mockSignal: -1, radiusMeters: 0, ageReliability: -1, permissionPrecision: -1,
    relation: -1, qualityScore: -1, hasScoreBreakdown: false, accuracyFactor: 0,
    freshnessFactor: 0, movementFactor: 0, consistencyFactor: 0, rawScore: -1, limitsApplied: [],
    activityNow: -1, activityConfidenceNow: -1, standingStillSinceTheReading: false,
    activityKnownUntilNow: false, activityKnownSinceAgeMs: 0, activityHasGaps: false, reasons: [],
    steps: [], firstAttemptTimedOut: false, extraTimeWasUsed: false, fallbackCause: nil,
    readingsReceived: 0, readingsKept: 0, readingsExamined: 0, gpsJumpsFound: 0, durationMs: 0)
}
