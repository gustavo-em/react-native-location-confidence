import Foundation

// When the tests run on a Mac (`swift test`), the C front door of the engine
// is a module of its own. Inside the app it is part of this same module, so
// there is nothing to import.
#if canImport(LCECore)
import LCECore
#endif

/// The C++ engine, seen from Swift.
///
/// Swift reads the structs of `lce.h` directly, so this file only copies
/// values: Swift types on one side, C structs on the other. No decision is
/// taken here.
///
///   EngineCoordinator
///     -> CoreEngine              (this file: Swift types <-> C structs)
///       -> lce_... functions     (cpp/include/location_confidence/lce.h)
///         -> the C++ engine      (every decision)
///
/// On Android the same job needs two files (CoreEngine.kt and
/// JniBindings.cpp), because Kotlin cannot read a C struct.
///
/// One engine must be used from one queue only. EngineCoordinator sees to that.
final class CoreEngine: CoreEnginePort {
  /// The version of the engine that was compiled into the app.
  static var abiVersion: Int { Int(lce_abi_version()) }

  /// The version this Swift code was written for (LCE_ABI_VERSION).
  static let versionThisCodeExpects = 2

  private var engine: OpaquePointer?

  // The public texts of the reasons, of the score limits and of the steps.
  // They live in the engine so that Android and iOS show the same ones.
  private let reasonTexts: [String]
  private let scoreLimitTexts: [String]
  private let stepTexts: [String]

  /// nil when the engine could not be created, or when it is of another
  /// version than the one this code was written for.
  static func createOrNil() -> CoreEngine? {
    guard abiVersion == versionThisCodeExpects else {
      return nil
    }
    guard let engine = lce_engine_create() else {
      return nil
    }
    return CoreEngine(engine: engine)
  }

  private init(engine: OpaquePointer) {
    self.engine = engine
    reasonTexts = (0..<Int32(lce_reason_count())).map { String(cString: lce_reason_name($0)) }
    scoreLimitTexts = (0..<Int32(lce_score_limit_count())).map {
      String(cString: lce_score_limit_name($0))
    }
    stepTexts = (0..<Int32(lce_step_count())).map { String(cString: lce_step_name($0)) }
  }

  deinit {
    destroy()
  }

  // MARK: Sessions

  func openSession(_ settings: SessionSettings, device: DeviceState, nowMs: Int64) -> SessionOpened {
    var cSettings = lce_session_settings()
    cSettings.keep_collecting_readings = yesOrNo(settings.keepCollectingReadings)
    cSettings.time_between_readings_ms = settings.timeBetweenReadingsMs
    cSettings.longest_collecting_time_ms = settings.longestCollectingTimeMs
    cSettings.device = cDeviceState(device)

    var opened = lce_session_opened()
    _ = lce_open_session(engine, &cSettings, nowMs, &opened)

    return SessionOpened(
      sessionId: Int(opened.session_id),
      collecting: CollectingState(rawValue: opened.collecting) ?? .notRequested,
      reasons: textsOfBits(opened.reasons, reasonTexts)
    )
  }

  @discardableResult
  func clearSessionHistory(_ sessionId: Int) -> Bool {
    guard let id = UInt32(exactly: sessionId) else {
      return false
    }
    return lce_clear_session_history(engine, id) == LCE_OK
  }

  func closeSession(_ sessionId: Int, nowMs: Int64) {
    guard let id = UInt32(exactly: sessionId) else {
      return
    }
    _ = lce_close_session(engine, id, nowMs)
  }

  // MARK: Requests

  func startRequest(
    sessionId: Int, settings: RequestSettings, device: DeviceState, nowMs: Int64
  ) -> RequestStart {
    var cSettings = lce_request_settings()
    cSettings.destination_latitude = settings.destinationLatitude
    cSettings.destination_longitude = settings.destinationLongitude
    cSettings.radius_meters = settings.radiusMeters
    cSettings.newest_age_that_counts_as_fresh_ms = settings.newestAgeThatCountsAsFreshMs
    cSettings.oldest_age_allowed_ms = settings.oldestAgeAllowedMs
    cSettings.worst_gps_error_accepted_meters = settings.worstGpsErrorAcceptedMeters
    cSettings.fastest_possible_speed_mps = settings.fastestPossibleSpeedMps
    cSettings.use_phone_activity = yesOrNo(settings.usePhoneActivity)
    cSettings.allow_answer_from_history = yesOrNo(settings.allowAnswerFromHistory)
    cSettings.mock_policy = settings.mockPolicy.rawValue
    cSettings.total_time_allowed_ms = settings.totalTimeAllowedMs
    cSettings.first_attempt_time_ms = settings.firstAttemptTimeMs
    cSettings.extra_time_ms = settings.extraTimeMs
    cSettings.device = cDeviceState(device)

    // A session number that cannot exist (negative, or huge) becomes one that
    // no session has: the engine then answers "unknown_session".
    let sessionNumber = UInt32(exactly: sessionId) ?? UInt32.max

    var requestId: UInt32 = 0
    let result = lce_start_request(engine, sessionNumber, &cSettings, nowMs, &requestId)

    if result == LCE_OK {
      return RequestStart(requestId: Int(requestId), refusal: nil)
    } else if result == LCE_ERROR_INVALID_REQUEST {
      return RequestStart(requestId: 0, refusal: .invalidRequest)
    } else {
      return RequestStart(requestId: 0, refusal: .engineIsFull)
    }
  }

  func cancelRequest(_ requestId: Int, nowMs: Int64) {
    guard let id = UInt32(exactly: requestId) else {
      return
    }
    _ = lce_cancel_request(engine, id, nowMs)
  }

  // MARK: Things that happen

  func readingsArrived(_ readings: [Reading], nowMs: Int64) {
    if readings.isEmpty {
      return
    }

    let cReadings: [lce_reading] = readings.map { reading in
      var cReading = lce_reading()
      cReading.latitude = reading.latitude
      cReading.longitude = reading.longitude
      cReading.has_accuracy = yesOrNo(reading.accuracyMeters != nil)
      cReading.accuracy_meters = reading.accuracyMeters ?? 0
      cReading.captured_at_ms = reading.capturedAtMs
      cReading.time_is_reliable = yesOrNo(reading.timeIsReliable)
      cReading.capture_time_was_estimated = yesOrNo(reading.captureTimeWasEstimated)
      cReading.only_approximate_permission = yesOrNo(reading.onlyApproximatePermission)
      cReading.has_speed = yesOrNo(reading.speedMps != nil)
      cReading.speed_mps = reading.speedMps ?? 0
      cReading.has_speed_accuracy = yesOrNo(reading.speedAccuracyMps != nil)
      cReading.speed_accuracy_mps = reading.speedAccuracyMps ?? 0
      cReading.has_heading = yesOrNo(reading.headingDegrees != nil)
      cReading.heading_degrees = reading.headingDegrees ?? 0
      cReading.provider = reading.provider.rawValue
      cReading.mock_signal = reading.mockSignal.rawValue
      return cReading
    }

    // All the readings of one delivery go in one call.
    cReadings.withUnsafeBufferPointer { buffer in
      _ = lce_readings_arrived(engine, buffer.baseAddress, UInt32(buffer.count), nowMs)
    }
  }

  func phoneActivityChanged(
    _ activity: PhoneActivity, confidence: ActivityConfidence, sinceMs: Int64, nowMs: Int64
  ) {
    _ = lce_phone_activity_changed(engine, activity.rawValue, confidence.rawValue, sinceMs, nowMs)
  }

  func phoneActivityUpdatesStopped(nowMs: Int64) {
    _ = lce_phone_activity_updates_stopped(engine, nowMs)
  }

  func timePassed(nowMs: Int64) {
    _ = lce_time_passed(engine, nowMs)
  }

  func appWentToBackground(nowMs: Int64) {
    _ = lce_app_went_to_background(engine, nowMs)
  }

  func locationPermissionWasLost(nowMs: Int64) {
    _ = lce_location_permission_was_lost(engine, nowMs)
  }

  func locationProviderFailed(nowMs: Int64) {
    _ = lce_location_provider_failed(engine, nowMs)
  }

  func setReadingsAreSimulated(_ simulated: Bool) {
    _ = lce_set_readings_are_simulated(engine, yesOrNo(simulated))
  }

  // MARK: What the engine wants, and what it has ready

  func whatThePlatformMustDoNow() -> PlatformWork {
    var work = lce_platform_work()
    _ = lce_what_the_platform_must_do_now(engine, &work)

    return PlatformWork(
      locationUpdatesAreWanted: work.location_updates_are_wanted != 0,
      timeBetweenLocationUpdatesMs: work.time_between_location_updates_ms,
      phoneActivityUpdatesAreWanted: work.phone_activity_updates_are_wanted != 0,
      wakeUpIsWanted: work.wake_up_is_wanted != 0,
      wakeUpAtMs: work.wake_up_at_ms
    )
  }

  func takeFinishedReport() -> Report? {
    var report = lce_report()
    let thereWasOne = lce_take_finished_report(engine, &report) == 1
    if !thereWasOne {
      return nil
    }

    // The report always carries 8 step numbers; only the first ones are real.
    // A C array of 8 arrives in Swift as a tuple of 8.
    let allSteps = withUnsafeBytes(of: report.steps) { bytes in
      Array(bytes.bindMemory(to: Int32.self))
    }
    let howManySteps = max(0, min(Int(report.how_many_steps), allSteps.count))
    let steps = allSteps.prefix(howManySteps).map { stepNumber -> String in
      let isKnown = stepNumber >= 0 && Int(stepNumber) < stepTexts.count
      return isKnown ? stepTexts[Int(stepNumber)] : "unknown"
    }

    // -1 means there was no cause.
    let causeNumber = Int(report.fallback_cause)
    let causeIsKnown = causeNumber >= 0 && causeNumber < reasonTexts.count
    let fallbackCause: String? = causeIsKnown ? reasonTexts[causeNumber] : nil

    return Report(
      requestId: Int(report.request_id),
      sessionId: Int(report.session_id),
      status: Int(report.status),
      source: Int(report.source),
      hasPosition: report.has_position != 0,
      latitude: report.latitude,
      longitude: report.longitude,
      distanceToDestinationMeters: report.distance_to_destination_meters,
      gpsErrorMeters: report.gps_error_meters,
      howFarPhoneCouldBeMeters: report.how_far_phone_could_be_meters,
      readingAgeMs: report.reading_age_ms,
      mockSignal: Int(report.mock_signal),
      radiusMeters: report.radius_meters,
      ageReliability: Int(report.age_reliability),
      permissionPrecision: Int(report.permission_precision),
      relation: Int(report.relation),
      qualityScore: Int(report.quality_score),
      hasScoreBreakdown: report.has_score_breakdown != 0,
      accuracyFactor: report.accuracy_factor,
      freshnessFactor: report.freshness_factor,
      movementFactor: report.movement_factor,
      consistencyFactor: report.consistency_factor,
      rawScore: Int(report.raw_score),
      limitsApplied: textsOfBits(UInt64(report.limits_applied), scoreLimitTexts),
      activityNow: Int(report.activity_now),
      activityConfidenceNow: Int(report.activity_confidence_now),
      standingStillSinceTheReading: report.standing_still_since_the_reading != 0,
      activityKnownUntilNow: report.activity_known_until_now != 0,
      activityKnownSinceAgeMs: report.activity_known_since_age_ms,
      activityHasGaps: report.activity_has_gaps != 0,
      reasons: textsOfBits(report.reasons, reasonTexts),
      steps: steps,
      firstAttemptTimedOut: report.first_attempt_timed_out != 0,
      extraTimeWasUsed: report.extra_time_was_used != 0,
      fallbackCause: fallbackCause,
      readingsReceived: Int(report.readings_received),
      readingsKept: Int(report.readings_kept),
      readingsExamined: Int(report.readings_examined),
      gpsJumpsFound: Int(report.gps_jumps_found),
      durationMs: report.duration_ms
    )
  }

  func destroy() {
    if engine != nil {
      lce_engine_destroy(engine)
      engine = nil
    }
  }

  // MARK: Helpers

  private func yesOrNo(_ value: Bool) -> Int32 {
    return value ? 1 : 0
  }

  private func cDeviceState(_ device: DeviceState) -> lce_device_state {
    var cDevice = lce_device_state()
    cDevice.location_permission = device.locationPermission.rawValue
    cDevice.permission_precision = device.permissionPrecision.rawValue
    cDevice.location_is_turned_on = yesOrNo(device.locationIsTurnedOn)
    cDevice.location_provider = device.locationProvider.rawValue
    cDevice.app_is_in_foreground = yesOrNo(device.appIsInForeground)
    cDevice.phone_activity_support = device.phoneActivity.rawValue
    return cDevice
  }

  /// The engine packs a list of yes/no answers in one number: bit 3 set means
  /// "the text number 3 applies". This unpacks them, in order.
  private func textsOfBits(_ bits: UInt64, _ texts: [String]) -> [String] {
    var result: [String] = []
    for number in 0..<min(texts.count, 64) {
      let bitIsSet = (bits >> UInt64(number)) & 1 == 1
      if bitIsSet {
        result.append(texts[number])
      }
    }
    return result
  }
}
