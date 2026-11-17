import XCTest

@testable import LocationConfidenceHost

/// The engine through its C front door, called from Swift.
///
/// "Golden scenario" is the same script, with the same expected numbers, as
/// in cpp/tests/TestFacade.cpp and in the Kotlin CoreEngineTest. If the three
/// agree, the values cross each bridge without being changed.
final class CoreEngineTests: XCTestCase {
  private var engine: CoreEngine!

  override func setUp() {
    engine = CoreEngine.createOrNil()
    XCTAssertNotNil(engine)
  }

  override func tearDown() {
    engine.destroy()
  }

  func test_the_engine_is_the_version_this_code_expects() {
    XCTAssertEqual(CoreEngine.abiVersion, 2)
    XCTAssertEqual(CoreEngine.versionThisCodeExpects, 2)
  }

  func test_golden_scenario_one_evaluation_one_reading_every_field_of_the_report() {
    let device = phoneWhereEverythingWorks()

    // second 0: the app asks.
    let started = engine.startEvaluation(
      sessionId: 0, settings: usualEvaluation(), device: device, nowMs: 0)
    XCTAssertNil(started.refusal)
    XCTAssertEqual(started.requestId, 1)

    let work = engine.whatThePlatformMustDoNow()
    XCTAssertTrue(work.locationUpdatesAreWanted)
    XCTAssertEqual(work.timeBetweenLocationUpdatesMs, 1000)
    XCTAssertTrue(work.phoneActivityUpdatesAreWanted)
    XCTAssertTrue(work.wakeUpIsWanted)
    XCTAssertEqual(work.wakeUpAtMs, 4500)
    XCTAssertNil(engine.takeFinishedReport())

    // second 2: a reading taken at second 1, 20 meters from the address,
    // with a GPS error of 8 meters.
    var reading = readingNorth(1000, 20.0, 8.0)
    reading.mockSignal = .notFlagged
    engine.readingsArrived([reading], nowMs: 2000)

    guard let report = engine.takeFinishedReport() else {
      return XCTFail("no report")
    }
    XCTAssertNil(engine.takeFinishedReport())

    XCTAssertEqual(report.requestId, 1)
    XCTAssertEqual(report.sessionId, 0)
    XCTAssertEqual(report.status, 0)  // available
    XCTAssertEqual(report.source, 0)  // fresh

    XCTAssertTrue(report.hasPosition)
    XCTAssertEqual(report.latitude, reading.latitude)
    XCTAssertEqual(report.longitude, 0)
    XCTAssertEqual(report.distanceToDestinationMeters, 20.0, accuracy: 1e-7)
    XCTAssertEqual(report.gpsErrorMeters, 8.0)
    XCTAssertEqual(report.howFarPhoneCouldBeMeters, 53.0)  // 8 + 45 x 1 second
    XCTAssertEqual(report.readingAgeMs, 1000)
    XCTAssertEqual(report.mockSignal, 1)  // not flagged

    XCTAssertEqual(report.radiusMeters, 100.0)
    XCTAssertEqual(report.ageReliability, 1)  // monotonic
    XCTAssertEqual(report.permissionPrecision, 0)  // precise
    XCTAssertEqual(report.relation, 0)  // supports inside

    XCTAssertEqual(report.qualityScore, 75)
    XCTAssertTrue(report.hasScoreBreakdown)
    XCTAssertEqual(report.accuracyFactor, 0.92, accuracy: 1e-12)
    XCTAssertEqual(report.freshnessFactor, 59.0 / 60.0, accuracy: 1e-12)
    XCTAssertEqual(report.movementFactor, 0.55, accuracy: 1e-12)
    XCTAssertEqual(report.consistencyFactor, 0.60, accuracy: 1e-12)
    XCTAssertEqual(report.rawScore, 81)
    XCTAssertEqual(report.limitsApplied, ["single_fix"])

    XCTAssertEqual(report.activityNow, 5)  // unknown
    XCTAssertEqual(report.activityConfidenceNow, 3)  // unknown
    XCTAssertFalse(report.standingStillSinceTheReading)
    XCTAssertFalse(report.activityKnownUntilNow)
    XCTAssertEqual(report.activityKnownSinceAgeMs, 0)
    XCTAssertFalse(report.activityHasGaps)

    XCTAssertEqual(report.reasons, ["uncertainty_model_heuristic"])
    XCTAssertEqual(report.steps, ["started", "first_attempt", "finished"])
    XCTAssertFalse(report.firstAttemptTimedOut)
    XCTAssertFalse(report.extraTimeWasUsed)
    XCTAssertNil(report.fallbackCause)

    XCTAssertEqual(report.readingsReceived, 1)
    XCTAssertEqual(report.readingsKept, 1)
    XCTAssertEqual(report.readingsExamined, 1)
    XCTAssertEqual(report.gpsJumpsFound, 0)
    XCTAssertEqual(report.durationMs, 2000)

    let after = engine.whatThePlatformMustDoNow()
    XCTAssertFalse(after.locationUpdatesAreWanted)
    XCTAssertFalse(after.phoneActivityUpdatesAreWanted)
    XCTAssertFalse(after.wakeUpIsWanted)
  }

  func test_an_evaluation_that_times_out_carries_its_flags_its_steps_and_its_fallback_cause() {
    _ = engine.startEvaluation(
      sessionId: 0, settings: usualEvaluation(), device: phoneWhereEverythingWorks(), nowMs: 1000)
    engine.timePassed(nowMs: 5500)
    engine.timePassed(nowMs: 12500)

    guard let report = engine.takeFinishedReport() else {
      return XCTFail("no report")
    }
    XCTAssertEqual(report.status, 2)  // unavailable
    XCTAssertEqual(report.source, 3)  // unavailable
    XCTAssertFalse(report.hasPosition)
    XCTAssertEqual(report.qualityScore, 0)
    XCTAssertFalse(report.hasScoreBreakdown)
    XCTAssertEqual(report.ageReliability, 0)  // not applicable
    XCTAssertTrue(report.firstAttemptTimedOut)
    XCTAssertTrue(report.extraTimeWasUsed)
    XCTAssertEqual(report.fallbackCause, "initial_fix_timeout")
    XCTAssertEqual(report.durationMs, 11500)
    XCTAssertEqual(report.limitsApplied, ["no_candidate"])
    XCTAssertEqual(
      report.steps,
      ["started", "first_attempt", "extra_time", "answer_from_history", "finished"])
    for reason in [
      "initial_fix_timeout", "recovery_window_timeout", "no_usable_fix", "history_empty",
    ] {
      XCTAssertTrue(report.reasons.contains(reason), reason)
    }
  }

  func test_every_optional_value_of_a_reading_crosses_and_a_missing_one_stays_missing() {
    let device = phoneWhereEverythingWorks()

    // A speed with its error makes the distance small: (0.3 + 2 x 0.5) x 1 + 1.25 = 2.55.
    _ = engine.startEvaluation(sessionId: 0, settings: usualEvaluation(), device: device, nowMs: 0)
    var withSpeed = readingNorth(0, 20.0)
    withSpeed.speedMps = 0.3
    withSpeed.speedAccuracyMps = 0.5
    withSpeed.headingDegrees = 90.0
    engine.readingsArrived([withSpeed], nowMs: 1000)
    XCTAssertEqual(
      engine.takeFinishedReport()?.howFarPhoneCouldBeMeters ?? -1, 8.0 + 2.55, accuracy: 1e-9)

    // No speed: the fastest speed is assumed, 8 + 45 = 53.
    _ = engine.startEvaluation(sessionId: 0, settings: usualEvaluation(), device: device, nowMs: 0)
    engine.readingsArrived([readingNorth(0, 20.0)], nowMs: 1000)
    XCTAssertEqual(engine.takeFinishedReport()?.howFarPhoneCouldBeMeters, 53.0)

    // No GPS error: the reading is thrown away.
    _ = engine.startEvaluation(sessionId: 0, settings: usualEvaluation(), device: device, nowMs: 0)
    var withoutAccuracy = readingNorth(0, 20.0)
    withoutAccuracy.accuracyMeters = nil
    engine.readingsArrived([withoutAccuracy], nowMs: 1000)
    engine.timePassed(nowMs: 30000)
    guard let thrownAway = engine.takeFinishedReport() else {
      return XCTFail("no report")
    }
    XCTAssertFalse(thrownAway.hasPosition)
    XCTAssertTrue(thrownAway.reasons.contains("location_accuracy_unavailable"))

    // The marks: simulated, approximate, estimated time.
    _ = engine.startEvaluation(sessionId: 0, settings: usualEvaluation(), device: device, nowMs: 0)
    var marked = readingNorth(0, 20.0)
    marked.mockSignal = .flagged
    marked.onlyApproximatePermission = true
    marked.captureTimeWasEstimated = true
    engine.readingsArrived([marked], nowMs: 1000)
    engine.timePassed(nowMs: 30000)
    guard let markedReport = engine.takeFinishedReport() else {
      return XCTFail("no report")
    }
    XCTAssertEqual(markedReport.mockSignal, 2)  // flagged
    XCTAssertEqual(markedReport.ageReliability, 2)  // estimated
    XCTAssertEqual(markedReport.qualityScore, 0)
    XCTAssertTrue(markedReport.reasons.contains("possible_mock_location"))
    XCTAssertTrue(markedReport.reasons.contains("precision_reduced"))
  }

  func test_a_long_delivery_crosses_in_one_call_and_every_reading_is_counted() {
    var strict = usualEvaluation()
    strict.worstGpsErrorAcceptedMeters = 1.0  // keeps it running
    _ = engine.startEvaluation(
      sessionId: 0, settings: strict, device: phoneWhereEverythingWorks(), nowMs: 0)

    let readings = (0..<40).map { readingNorth(Int64($0) * 100, 20.0) }
    engine.readingsArrived(readings, nowMs: 4000)
    engine.timePassed(nowMs: 12000)

    guard let report = engine.takeFinishedReport() else {
      return XCTFail("no report")
    }
    XCTAssertEqual(report.readingsReceived, 40)
    XCTAssertEqual(report.readingsKept, 40)
    XCTAssertEqual(report.readingsExamined, 10)
  }

  func test_the_state_of_the_phone_crosses() {
    var denied = phoneWhereEverythingWorks()
    denied.locationPermission = .denied
    _ = engine.startEvaluation(sessionId: 0, settings: usualEvaluation(), device: denied, nowMs: 0)

    guard let report = engine.takeFinishedReport() else {
      return XCTFail("no report")
    }
    XCTAssertEqual(report.status, 2)
    XCTAssertTrue(report.reasons.contains("permission_denied"))
    XCTAssertFalse(engine.whatThePlatformMustDoNow().locationUpdatesAreWanted)
  }

  func test_a_session_is_opened_collects_and_is_closed() {
    let device = phoneWhereEverythingWorks()
    let settings = SessionSettings(
      keepCollectingReadings: true, timeBetweenReadingsMs: 5000, longestCollectingTimeMs: 900000)

    let opened = engine.openSession(settings, device: device, nowMs: 0)
    XCTAssertEqual(opened.sessionId, 1)
    XCTAssertEqual(opened.collecting, .active)
    XCTAssertTrue(opened.reasons.isEmpty)

    let work = engine.whatThePlatformMustDoNow()
    XCTAssertTrue(work.locationUpdatesAreWanted)
    XCTAssertEqual(work.timeBetweenLocationUpdatesMs, 5000)
    XCTAssertEqual(work.wakeUpAtMs, 900000)

    XCTAssertTrue(engine.clearSessionHistory(opened.sessionId))
    XCTAssertFalse(engine.clearSessionHistory(999))

    engine.closeSession(opened.sessionId, nowMs: 1000)
    XCTAssertFalse(engine.whatThePlatformMustDoNow().locationUpdatesAreWanted)

    // Blocked collecting says why.
    var locationOff = device
    locationOff.locationIsTurnedOn = false
    let blocked = engine.openSession(settings, device: locationOff, nowMs: 2000)
    XCTAssertEqual(blocked.collecting, .blocked)
    XCTAssertEqual(blocked.reasons, ["location_services_disabled"])
  }

  func test_the_phone_activity_crosses() {
    let device = phoneWhereEverythingWorks()
    let session = engine.openSession(
      SessionSettings(
        keepCollectingReadings: true, timeBetweenReadingsMs: 5000, longestCollectingTimeMs: 900000),
      device: device, nowMs: 0)
    engine.readingsArrived([readingNorth(0, 20.0, 5.0)], nowMs: 0)
    engine.phoneActivityChanged(.standingStill, confidence: .high, sinceMs: 0, nowMs: 500)

    _ = engine.startEvaluation(
      sessionId: session.sessionId, settings: usualEvaluation(), device: device, nowMs: 10000)
    engine.timePassed(nowMs: 14500)

    guard let report = engine.takeFinishedReport() else {
      return XCTFail("no report")
    }
    XCTAssertEqual(report.activityNow, 0)  // standing still
    XCTAssertEqual(report.activityConfidenceNow, 0)  // high
    XCTAssertTrue(report.standingStillSinceTheReading)
    XCTAssertTrue(report.activityKnownUntilNow)
    XCTAssertEqual(report.activityKnownSinceAgeMs, 14500)
    XCTAssertEqual(report.howFarPhoneCouldBeMeters, 5.0 + 0.5 * 14.5, accuracy: 1e-9)
    XCTAssertEqual(report.source, 2)  // cached
  }

  func test_a_request_that_makes_no_sense_is_refused() {
    var broken = usualEvaluation()
    broken.destinationLatitude = 91.0
    let started = engine.startEvaluation(
      sessionId: 0, settings: broken, device: phoneWhereEverythingWorks(), nowMs: 0)

    XCTAssertEqual(started.refusal, .invalidRequest)
    XCTAssertNil(engine.takeFinishedReport())
    XCTAssertFalse(engine.whatThePlatformMustDoNow().locationUpdatesAreWanted)
  }

  func test_a_session_number_that_cannot_exist_is_answered_as_an_unknown_session() {
    for sessionId in [99, -1] {
      let started = engine.startEvaluation(
        sessionId: sessionId, settings: usualEvaluation(), device: phoneWhereEverythingWorks(),
        nowMs: 0)
      XCTAssertNil(started.refusal)

      guard let report = engine.takeFinishedReport() else {
        return XCTFail("no report")
      }
      XCTAssertEqual(report.status, 2)  // unavailable
      XCTAssertTrue(report.reasons.contains("unknown_session"))
    }
    XCTAssertFalse(engine.whatThePlatformMustDoNow().locationUpdatesAreWanted)
  }

  func test_the_reasons_above_number_31_cross_too() {
    // "simulated_location_source" is reason number 43: it sits in the upper
    // half of the 64 bits.
    engine.setReadingsAreSimulated(true)
    _ = engine.startEvaluation(
      sessionId: 0, settings: usualEvaluation(), device: phoneWhereEverythingWorks(), nowMs: 0)
    engine.readingsArrived([readingNorth(0, 20.0)], nowMs: 1000)

    guard let report = engine.takeFinishedReport() else {
      return XCTFail("no report")
    }
    XCTAssertTrue(report.reasons.contains("simulated_location_source"))
    XCTAssertTrue(report.reasons.contains("uncertainty_model_heuristic"))
  }

  func test_CON_025_a_thousand_evaluations_leave_nothing_behind() {
    let device = phoneWhereEverythingWorks()
    for round in 0..<1000 {
      let now = Int64(round) * 20000
      _ = engine.startEvaluation(
        sessionId: 0, settings: usualEvaluation(), device: device, nowMs: now)
      engine.readingsArrived([readingNorth(now, 20.0)], nowMs: now + 1000)
      XCTAssertNotNil(engine.takeFinishedReport())
      XCTAssertNil(engine.takeFinishedReport())
    }
    XCTAssertFalse(engine.whatThePlatformMustDoNow().locationUpdatesAreWanted)
  }

  func test_after_destroy_calls_are_answered_and_nothing_crashes() {
    engine.destroy()
    engine.destroy()  // twice

    engine.timePassed(nowMs: 1000)
    XCTAssertNil(engine.takeFinishedReport())
    XCTAssertFalse(engine.whatThePlatformMustDoNow().locationUpdatesAreWanted)
    let started = engine.startEvaluation(
      sessionId: 0, settings: usualEvaluation(), device: phoneWhereEverythingWorks(), nowMs: 0)
    XCTAssertEqual(started.refusal, .engineIsFull)
  }
}
