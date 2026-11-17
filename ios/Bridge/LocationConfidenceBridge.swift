import Foundation

/// What the TurboModule (LocationConfidence.mm) calls on iOS.
///
/// React Native's generated code is C++, which Swift cannot read. So the
/// work is split in two:
///
///   LocationConfidence.mm            receives the call from JavaScript and
///                                    turns the generated C++ types into
///                                    plain dictionaries
///   LocationConfidenceBridge (here)  builds the pieces once, turns the
///                                    dictionaries into Swift types, asks the
///                                    coordinator, and turns the answer back
///
/// No rule about locations lives here. It is the twin of
/// android/.../LocationConfidenceModule.kt.
///
///   JavaScript
///     -> LocationConfidence.mm
///       -> LocationConfidenceBridge     (this file: translate)
///         -> EngineCoordinator          (tell the engine, then obey it)
///           -> CoreEngine               (Swift types <-> C structs)
///             -> the C++ engine         (every decision)
@objc(LCEBridge)
public final class LocationConfidenceBridge: NSObject {
  public typealias Resolve = (Any?) -> Void
  public typealias Reject = (_ code: String, _ message: String) -> Void

  private let clock = MonotonicClock()
  private let deviceState: IOSDeviceState

  /// nil when the engine could not be created, or is of another version than
  /// this code. Every call then answers "platform_unsupported" instead of
  /// crashing.
  private let coordinator: EngineCoordinator?

  /// Must be created on the main thread (see IOSDeviceState).
  @objc public override init() {
    deviceState = IOSDeviceState()

    if let engine = CoreEngine.createOrNil() {
      coordinator = EngineCoordinator(
        engine: engine,
        queue: DispatchSerialQueue(),
        clock: clock,
        realLocationSource: CoreLocationSource(clock: clock),
        phoneActivitySource: CoreMotionActivitySource(clock: clock),
        realDeviceState: deviceState
      )
    } else {
      coordinator = nil
    }
    super.init()

    // The app going to the background ends what is running; nothing restarts
    // by itself when it comes back.
    deviceState.whenTheAppGoesToTheBackground = { [weak self] in
      self?.coordinator?.appWentToBackground()
    }
    // Coming back, or a change of permission while the app is open: the
    // coordinator checks whether the location permission was taken away.
    deviceState.whenTheAppComesBack = { [weak self] in
      self?.coordinator?.appCameToForeground()
    }
    deviceState.whenThePermissionChanges = { [weak self] in
      self?.coordinator?.appCameToForeground()
    }
  }

  /// React Native is tearing the module down (reload, or the app is closing).
  @objc public func invalidate() {
    coordinator?.shutDown()
  }

  // MARK: Capabilities and permissions

  @objc public func getCapabilities(_ resolve: @escaping Resolve) {
    func answer(_ device: DeviceState, simulationIsOn: Bool) {
      var reasons: [String] = []
      if coordinator == nil {
        reasons.append("platform_unsupported")
      }
      if !device.locationIsTurnedOn {
        reasons.append("location_services_disabled")
      }
      if device.phoneActivity == .notSupported {
        reasons.append("motion_unavailable")
      } else if device.phoneActivity == .permissionDenied {
        reasons.append("motion_permission_denied")
      }

      resolve([
        "locationServicesEnabled": device.locationIsTurnedOn,
        "locationProviderAvailable": true,
        "motionSupported": device.phoneActivity != .notSupported,
        // iOS tells when a location was made up by software.
        "mockDetectionSupported": true,
        "simulationEnabled": simulationIsOn,
        "coreAbiVersion": CoreEngine.abiVersion,
        "reasonCodes": reasons,
      ])
    }

    guard let coordinator = coordinator else {
      // Reading the location switch can be slow: not on the main thread.
      DispatchQueue.global(qos: .userInitiated).async {
        answer(self.deviceState.currentDeviceState(), simulationIsOn: false)
      }
      return
    }
    coordinator.readDeviceState { device in
      coordinator.isSimulationOn { simulationIsOn in
        answer(device, simulationIsOn: simulationIsOn)
      }
    }
  }

  @objc public func getAuthorization(_ resolve: @escaping Resolve) {
    DispatchQueue.global(qos: .userInitiated).async {
      resolve(self.authorization())
    }
  }

  /// Shows iOS's location dialog. This is the ONLY place where the library
  /// asks the user for the location, and it only runs when the app calls it.
  /// Opening a session or evaluating never asks.
  @objc public func requestLocationPermission(
    _ resolve: @escaping Resolve, reject: @escaping Reject
  ) {
    DispatchQueue.main.async {
      // Without this text in Info.plist iOS shows no dialog and never answers.
      guard IOSDeviceState.hasUsageDescription("NSLocationWhenInUseUsageDescription") else {
        reject(
          "missing_usage_description",
          "Add NSLocationWhenInUseUsageDescription to the app's Info.plist")
        return
      }
      self.deviceState.requestLocationPermission {
        self.getAuthorization(resolve)
      }
    }
  }

  /// Shows iOS's "Motion & Fitness" dialog, if the user was never asked.
  @objc public func requestMotionPermission(_ resolve: @escaping Resolve) {
    DispatchQueue.main.async {
      let neverAsked = (IOSDeviceState.phoneActivitySupport() == .permissionNotAsked)
      if !neverAsked {
        self.getAuthorization(resolve)
        return
      }
      CoreMotionActivitySource.requestPermission {
        self.getAuthorization(resolve)
      }
    }
  }

  private func authorization() -> [String: Any] {
    let device = deviceState.currentDeviceState()
    return [
      "location": device.locationPermission.publicText,
      "precision": device.permissionPrecision.publicText,
      "motion": device.phoneActivity.publicText,
    ]
  }

  // MARK: Sessions

  @objc public func startSession(
    keepCollectingReadings: Bool,
    timeBetweenReadingsMs: Double,
    longestCollectingTimeMs: Double,
    resolve: @escaping Resolve,
    reject: @escaping Reject
  ) {
    guard let coordinator = coordinatorOrReject(reject) else { return }

    let settings = SessionSettings(
      keepCollectingReadings: keepCollectingReadings,
      timeBetweenReadingsMs: Int64(timeBetweenReadingsMs),
      longestCollectingTimeMs: Int64(longestCollectingTimeMs)
    )

    coordinator.openSession(settings) { opened in
      let wasOpened = (opened.sessionId != 0)
      // "Stopped" cannot happen right after opening; the app only needs to
      // tell "active" from "blocked" from "not requested".
      let monitoring = (opened.collecting == .stopped ? CollectingState.blocked : opened.collecting)

      resolve([
        "id": wasOpened ? String(opened.sessionId) : "",
        "monitoring": monitoring.publicText,
        "reasonCodes": opened.reasons,
      ])
    }
  }

  @objc public func clearHistory(
    _ sessionId: String, resolve: @escaping Resolve, reject: @escaping Reject
  ) {
    guard let coordinator = coordinatorOrReject(reject) else { return }
    coordinator.clearHistory(sessionNumber(sessionId)) { resolve(nil) }
  }

  @objc public func stopSession(
    _ sessionId: String, resolve: @escaping Resolve, reject: @escaping Reject
  ) {
    guard let coordinator = coordinatorOrReject(reject) else { return }
    coordinator.closeSession(sessionNumber(sessionId)) { resolve(nil) }
  }

  // MARK: Evaluations

  /// `request` has the fields of NativeEvaluateRequest (src/NativeLocationConfidence.ts).
  @objc public func evaluate(
    _ requestId: String,
    request: [String: Any],
    resolve: @escaping Resolve,
    reject: @escaping Reject
  ) {
    guard let coordinator = coordinatorOrReject(reject) else { return }

    func number(_ key: String) -> Double {
      return (request[key] as? NSNumber)?.doubleValue ?? 0
    }
    func yesOrNo(_ key: String) -> Bool {
      return (request[key] as? NSNumber)?.boolValue ?? false
    }

    let sessionIdFromTheApp = request["sessionId"] as? String

    let settings = EvaluationSettings(
      destinationLatitude: number("latitude"),
      destinationLongitude: number("longitude"),
      radiusMeters: number("radiusMeters"),
      newestAgeThatCountsAsFreshMs: Int64(number("maxFreshAgeMs")),
      oldestAgeAllowedMs: Int64(number("maxFallbackAgeMs")),
      worstGpsErrorAcceptedMeters: number("maxHorizontalAccuracyMeters"),
      fastestPossibleSpeedMps: number("maxPlausibleSpeedMps"),
      usePhoneActivity: yesOrNo("enableMotionAssistance"),
      allowAnswerFromHistory: yesOrNo("allowHistoricalFallback"),
      mockPolicy: yesOrNo("zeroScoreForMockLocation") ? .zeroScore : .flagOnly,
      totalTimeAllowedMs: Int64(number("totalDeadlineMs")),
      firstAttemptTimeMs: Int64(number("initialFixBudgetMs")),
      extraTimeMs: Int64(number("recoveryWindowMs"))
    )

    // No session: the engine's number for that is 0.
    let session = sessionIdFromTheApp.map { sessionNumber($0) } ?? 0

    coordinator.evaluate(
      idGivenByTheApp: requestId,
      sessionId: session,
      settings: settings,
      onReport: { report in
        resolve(Self.dictionary(of: report, sessionIdFromTheApp: sessionIdFromTheApp))
      },
      onRefused: { refusal in
        switch refusal {
        case .invalidRequest:
          reject("invalid_argument", "The destination or the radius is not valid")
        case .engineIsFull:
          reject("native_internal_error", "The engine could not take one more evaluation")
        }
      }
    )
  }

  @objc public func cancelRequest(_ requestId: String) {
    coordinator?.cancel(requestId)
  }

  // MARK: Simulation

  @objc public func setSimulationEnabled(
    _ enabled: Bool, resolve: @escaping Resolve, reject: @escaping Reject
  ) {
    guard let coordinator = coordinatorOrReject(reject) else { return }
    coordinator.setSimulationEnabled(enabled) { resolve(nil) }
  }

  /// Each item has the fields of NativeSimulatedReading.
  @objc public func simulateReadings(
    _ readings: [[String: Any]], resolve: @escaping Resolve, reject: @escaping Reject
  ) {
    guard let coordinator = coordinatorOrReject(reject) else { return }

    let nowMs = clock.nowMs()
    let made = readings.map { item -> Reading in
      func number(_ key: String) -> Double? {
        return (item[key] as? NSNumber)?.doubleValue
      }
      func yesOrNo(_ key: String) -> Bool {
        return (item[key] as? NSNumber)?.boolValue ?? false
      }
      return Reading(
        latitude: number("latitude") ?? 0,
        longitude: number("longitude") ?? 0,
        accuracyMeters: number("accuracyMeters"),
        capturedAtMs: nowMs - Int64(number("ageMs") ?? 0),
        onlyApproximatePermission: yesOrNo("approximate"),
        // A speed that was not given arrives as null: it stays unknown.
        speedMps: number("speedMps"),
        speedAccuracyMps: number("speedAccuracyMps"),
        mockSignal: yesOrNo("flaggedAsMock") ? .flagged : .notFlagged
      )
    }

    coordinator.simulateReadings(made) { resolve(nil) }
  }

  @objc public func simulatePhoneActivity(
    _ activity: Double,
    confidence: Double,
    resolve: @escaping Resolve,
    reject: @escaping Reject
  ) {
    guard let coordinator = coordinatorOrReject(reject) else { return }

    // -1 (or any number that is not an activity) means "the phone stopped reporting".
    var chosenActivity = PhoneActivity(rawValue: Int32(activity))
    if chosenActivity == .unknown {
      chosenActivity = nil
    }
    let chosenConfidence = ActivityConfidence(rawValue: Int32(confidence)) ?? .unknown

    coordinator.simulatePhoneActivity(chosenActivity, confidence: chosenConfidence) {
      resolve(nil)
    }
  }

  // MARK: Helpers

  private func coordinatorOrReject(_ reject: Reject) -> EngineCoordinator? {
    if coordinator == nil {
      reject(
        "platform_unsupported",
        "The engine of react-native-location-confidence could not be started on this device")
    }
    return coordinator
  }

  /// Session ids travel to JavaScript as text ("1", "2"...). A text that is
  /// not a number becomes -1, which no session has, so the engine answers
  /// "unknown_session".
  private func sessionNumber(_ sessionId: String) -> Int {
    return Int(sessionId) ?? -1
  }

  /// The report as the flat object described by NativeAssessment in the
  /// TypeScript spec.
  private static func dictionary(of report: Report, sessionIdFromTheApp: String?) -> [String: Any] {
    return [
      "transportVersion": CoreEngine.abiVersion,
      "sessionId": sessionIdFromTheApp ?? NSNull(),
      "status": report.status,
      "source": report.source,
      "hasPosition": report.hasPosition,
      "latitude": report.latitude,
      "longitude": report.longitude,
      "distanceMeters": report.distanceToDestinationMeters,
      "horizontalAccuracyMeters": report.gpsErrorMeters,
      "effectiveUncertaintyMeters": report.howFarPhoneCouldBeMeters,
      "locationAgeMs": Double(report.readingAgeMs),
      "mockSignal": report.mockSignal,
      "radiusMeters": report.radiusMeters,
      "ageReliability": report.ageReliability,
      "permissionPrecision": report.permissionPrecision,
      "evidenceRelation": report.relation,
      "qualityScore": report.qualityScore,
      "hasScoreBreakdown": report.hasScoreBreakdown,
      "accuracyFactor": report.accuracyFactor,
      "freshnessFactor": report.freshnessFactor,
      "movementFactor": report.movementFactor,
      "consistencyFactor": report.consistencyFactor,
      "rawScore": report.rawScore,
      "appliedCaps": report.limitsApplied,
      "motionState": report.activityNow,
      "motionConfidence": report.activityConfidenceNow,
      "continuousStationaritySupported": report.standingStillSinceTheReading,
      "hasCoverageStart": report.activityKnownUntilNow,
      "coverageStartAgeMs": Double(report.activityKnownSinceAgeMs),
      "gapsDetected": report.activityHasGaps,
      "reasonCodes": report.reasons,
      "phaseTrace": report.steps,
      "initialFixTimedOut": report.firstAttemptTimedOut,
      "recoveryAttempted": report.extraTimeWasUsed,
      "fallbackCause": report.fallbackCause ?? NSNull(),
      "samplesReceived": report.readingsReceived,
      "uniqueSamplesRetained": report.readingsKept,
      "samplesAnalyzed": report.readingsExamined,
      "outliersFlagged": report.gpsJumpsFound,
      "durationMs": Double(report.durationMs),
    ]
  }
}
