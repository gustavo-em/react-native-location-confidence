import Foundation

/*
 * The data that travels between the Swift code and the engine.
 *
 * Each list of choices below carries the NUMBER the engine uses for it. The
 * numbers are defined in cpp/include/location_confidence/lce.h (the LCE_...
 * constants) and must be the same here. The Kotlin side has the same file:
 * android/.../models/Models.kt.
 */

/// The location permission the user gave to the app.
enum LocationPermission: Int32 {
  case notDetermined = 0  // the user was never asked
  case denied = 1
  case restricted = 2  // blocked by the device
  case granted = 3

  var publicText: String {
    switch self {
    case .notDetermined: return "not_determined"
    case .denied: return "denied"
    case .restricted: return "restricted"
    case .granted: return "granted"
    }
  }
}

/// Precise location, or only an approximate one.
enum PermissionPrecision: Int32 {
  case precise = 0
  case approximate = 1
  case unknown = 2

  var publicText: String {
    switch self {
    case .precise: return "precise"
    case .approximate: return "approximate"
    case .unknown: return "unknown"
    }
  }
}

/// Can the phone give locations at all?
enum LocationProviderState: Int32 {
  case available = 0
  case unavailable = 1
  case playServicesMissing = 2  // Android only
}

/// Can the phone report its activity (walking, standing still...)?
enum PhoneActivitySupport: Int32 {
  case available = 0
  case notSupported = 1
  case permissionDenied = 2
  case permissionNotAsked = 3

  var publicText: String {
    switch self {
    case .available: return "granted"
    case .notSupported: return "unsupported"
    case .permissionDenied: return "denied"
    case .permissionNotAsked: return "not_determined"
    }
  }
}

/// What the phone is doing.
enum PhoneActivity: Int32 {
  case standingStill = 0
  case walking = 1
  case running = 2
  case cycling = 3
  case inVehicle = 4
  case unknown = 5
}

/// How sure the phone is about its activity.
enum ActivityConfidence: Int32 {
  case high = 0
  case medium = 1
  case low = 2
  case unknown = 3
}

/// Which system service produced a reading.
enum ReadingProvider: Int32 {
  case fused = 0  // Android; never used on iOS
  case coreLocation = 1
  case systemCache = 2
}

/// Did the system mark a reading as simulated? "Not flagged" proves nothing.
enum MockSignal: Int32 {
  case unknown = 0
  case notFlagged = 1
  case flagged = 2
}

/// What the app wants for a reading marked as simulated.
enum MockPolicy: Int32 {
  case zeroScore = 0
  case flagOnly = 1
}

/// Is a session collecting readings?
enum CollectingState: Int32 {
  case notRequested = 0
  case active = 1
  case blocked = 2
  case stopped = 3

  var publicText: String {
    switch self {
    case .notRequested: return "not_requested"
    case .active: return "active"
    case .blocked: return "blocked"
    case .stopped: return "stopped"
    }
  }
}

/// What the platform knows about the phone at one moment.
struct DeviceState: Equatable {
  var locationPermission: LocationPermission
  var permissionPrecision: PermissionPrecision
  var locationIsTurnedOn: Bool
  var locationProvider: LocationProviderState
  var appIsInForeground: Bool
  var phoneActivity: PhoneActivitySupport
}

/// What the app chose when it opened a session.
struct SessionSettings {
  var keepCollectingReadings: Bool
  var timeBetweenReadingsMs: Int64
  var longestCollectingTimeMs: Int64
}

/// What the app asked for in one evaluation.
struct EvaluationSettings {
  var destinationLatitude: Double
  var destinationLongitude: Double
  var radiusMeters: Double
  var newestAgeThatCountsAsFreshMs: Int64
  var oldestAgeAllowedMs: Int64
  var worstGpsErrorAcceptedMeters: Double
  var fastestPossibleSpeedMps: Double
  var usePhoneActivity: Bool
  var allowAnswerFromHistory: Bool
  var mockPolicy: MockPolicy
  var totalTimeAllowedMs: Int64
  var firstAttemptTimeMs: Int64
  var extraTimeMs: Int64
}

/// One reading as the GPS delivered it. Nothing in it is trusted yet: the
/// engine checks every field.
///
/// A value that the phone did not report is `nil`. It is never replaced by
/// zero: an unknown speed is not "standing still".
struct Reading {
  var latitude: Double
  var longitude: Double
  var accuracyMeters: Double?
  /// When the reading was taken, on the same clock as `Clock`.
  var capturedAtMs: Int64
  var timeIsReliable: Bool = true
  var captureTimeWasEstimated: Bool = false
  var onlyApproximatePermission: Bool = false
  var speedMps: Double? = nil
  var speedAccuracyMps: Double? = nil
  var headingDegrees: Double? = nil
  var provider: ReadingProvider = .coreLocation
  var mockSignal: MockSignal = .unknown
}

/// The answer to "open a session".
struct SessionOpened {
  /// 0 means the session was NOT opened (too many are open).
  var sessionId: Int
  var collecting: CollectingState
  var reasons: [String]
}

/// What the engine wants from the platform right now.
struct PlatformWork: Equatable {
  var locationUpdatesAreWanted: Bool
  var timeBetweenLocationUpdatesMs: Int64
  var phoneActivityUpdatesAreWanted: Bool
  var wakeUpIsWanted: Bool
  var wakeUpAtMs: Int64
}

/// The final report of one evaluation.
///
/// The fields that are a choice from a list (status, source...) stay as the
/// engine's numbers here. The JavaScript side turns them into their public
/// texts, in one place for Android and iOS.
struct Report {
  var requestId: Int
  var sessionId: Int
  var status: Int
  var source: Int
  var hasPosition: Bool
  var latitude: Double
  var longitude: Double
  var distanceToDestinationMeters: Double
  var gpsErrorMeters: Double
  var howFarPhoneCouldBeMeters: Double
  var readingAgeMs: Int64
  var mockSignal: Int
  var radiusMeters: Double
  var ageReliability: Int
  var permissionPrecision: Int
  var relation: Int
  /// An index from 0 to 100. It is NOT a probability.
  var qualityScore: Int
  var hasScoreBreakdown: Bool
  var accuracyFactor: Double
  var freshnessFactor: Double
  var movementFactor: Double
  var consistencyFactor: Double
  var rawScore: Int
  var limitsApplied: [String]
  var activityNow: Int
  var activityConfidenceNow: Int
  var standingStillSinceTheReading: Bool
  var activityKnownUntilNow: Bool
  var activityKnownSinceAgeMs: Int64
  var activityHasGaps: Bool
  var reasons: [String]
  var steps: [String]
  var firstAttemptTimedOut: Bool
  var extraTimeWasUsed: Bool
  /// The public text of the reason, or nil when there was none.
  var fallbackCause: String?
  var readingsReceived: Int
  var readingsKept: Int
  var readingsExamined: Int
  var gpsJumpsFound: Int
  var durationMs: Int64
}
