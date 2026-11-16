package com.locationconfidence.models

/*
 * The data that travels between the Kotlin code and the engine.
 *
 * Each list of choices below carries the NUMBER the engine uses for it. The
 * numbers are defined in cpp/include/location_confidence/lce.h (the LCE_...
 * constants) and must be the same here.
 */

/** The location permission the user gave to the app. */
enum class LocationPermission(val number: Int, val publicText: String) {
  NOT_DETERMINED(0, "not_determined"), // the user was never asked
  DENIED(1, "denied"),
  RESTRICTED(2, "restricted"), // blocked by the device
  GRANTED(3, "granted"),
}

/** Precise location, or only an approximate one. */
enum class PermissionPrecision(val number: Int, val publicText: String) {
  PRECISE(0, "precise"),
  APPROXIMATE(1, "approximate"),
  UNKNOWN(2, "unknown"),
}

/** Can the phone give locations at all? */
enum class LocationProviderState(val number: Int) {
  AVAILABLE(0),
  UNAVAILABLE(1),
  PLAY_SERVICES_MISSING(2),
}

/** Can the phone report its activity (walking, standing still...)? */
enum class PhoneActivitySupport(val number: Int, val publicText: String) {
  AVAILABLE(0, "granted"),
  NOT_SUPPORTED(1, "unsupported"),
  PERMISSION_DENIED(2, "denied"),
  PERMISSION_NOT_ASKED(3, "not_determined"),
}

/** What the phone is doing. */
enum class PhoneActivity(val number: Int) {
  STANDING_STILL(0),
  WALKING(1),
  RUNNING(2),
  CYCLING(3),
  IN_VEHICLE(4),
  UNKNOWN(5),
}

/** How sure the phone is about its activity. */
enum class ActivityConfidence(val number: Int) {
  HIGH(0),
  MEDIUM(1),
  LOW(2),
  UNKNOWN(3),
}

/** Which system service produced a reading. */
enum class ReadingProvider(val number: Int) {
  FUSED(0), // Google Play services
  CORE_LOCATION(1), // iOS; never used on Android
  SYSTEM_CACHE(2),
}

/** Did the system mark a reading as simulated? "Not flagged" proves nothing. */
enum class MockSignal(val number: Int) {
  UNKNOWN(0),
  NOT_FLAGGED(1),
  FLAGGED(2),
}

/** What the app wants for a reading marked as simulated. */
enum class MockPolicy(val number: Int) {
  ZERO_SCORE(0),
  FLAG_ONLY(1),
}

/** Is a session collecting readings? */
enum class CollectingState(val number: Int, val publicText: String) {
  NOT_REQUESTED(0, "not_requested"),
  ACTIVE(1, "active"),
  BLOCKED(2, "blocked"),
  STOPPED(3, "stopped"),
}

/** What the platform knows about the phone at one moment. */
data class DeviceState(
  val locationPermission: LocationPermission,
  val permissionPrecision: PermissionPrecision,
  val locationIsTurnedOn: Boolean,
  val locationProvider: LocationProviderState,
  val appIsInForeground: Boolean,
  val phoneActivity: PhoneActivitySupport,
)

/** What the app chose when it opened a session. */
data class SessionSettings(
  val keepCollectingReadings: Boolean,
  val timeBetweenReadingsMs: Long,
  val longestCollectingTimeMs: Long,
)

/** What the app asked for in one evaluation. */
data class EvaluationSettings(
  val destinationLatitude: Double,
  val destinationLongitude: Double,
  val radiusMeters: Double,
  val newestAgeThatCountsAsFreshMs: Long,
  val oldestAgeAllowedMs: Long,
  val worstGpsErrorAcceptedMeters: Double,
  val fastestPossibleSpeedMps: Double,
  val usePhoneActivity: Boolean,
  val allowAnswerFromHistory: Boolean,
  val mockPolicy: MockPolicy,
  val totalTimeAllowedMs: Long,
  val firstAttemptTimeMs: Long,
  val extraTimeMs: Long,
)

/**
 * One reading as the GPS delivered it. Nothing in it is trusted yet: the
 * engine checks every field.
 *
 * A value that the phone did not report is `null`. It is never replaced by
 * zero: an unknown speed is not "standing still".
 */
data class Reading(
  val latitude: Double,
  val longitude: Double,
  val accuracyMeters: Double?,
  /** When the reading was taken, on the same clock as [com.locationconfidence.ports.Clock]. */
  val capturedAtMs: Long,
  val timeIsReliable: Boolean = true,
  val captureTimeWasEstimated: Boolean = false,
  val onlyApproximatePermission: Boolean = false,
  val speedMps: Double? = null,
  val speedAccuracyMps: Double? = null,
  val headingDegrees: Double? = null,
  val provider: ReadingProvider = ReadingProvider.FUSED,
  val mockSignal: MockSignal = MockSignal.UNKNOWN,
)

/** The answer to "open a session". */
data class SessionOpened(
  /** 0 means the session was NOT opened (too many are open). */
  val sessionId: Int,
  val collecting: CollectingState,
  val reasons: List<String>,
)

/** What the engine wants from the platform right now. */
data class PlatformWork(
  val locationUpdatesAreWanted: Boolean,
  val timeBetweenLocationUpdatesMs: Long,
  val phoneActivityUpdatesAreWanted: Boolean,
  val wakeUpIsWanted: Boolean,
  val wakeUpAtMs: Long,
)

/**
 * The final report of one evaluation.
 *
 * The fields that are a choice from a list (status, source...) stay as the
 * engine's numbers here. The JavaScript side turns them into their public
 * texts, in one place for Android and iOS.
 */
data class Report(
  val requestId: Int,
  val sessionId: Int,
  val status: Int,
  val source: Int,
  val hasPosition: Boolean,
  val latitude: Double,
  val longitude: Double,
  val distanceToDestinationMeters: Double,
  val gpsErrorMeters: Double,
  val howFarPhoneCouldBeMeters: Double,
  val readingAgeMs: Long,
  val mockSignal: Int,
  val radiusMeters: Double,
  val ageReliability: Int,
  val permissionPrecision: Int,
  val relation: Int,
  /** An index from 0 to 100. It is NOT a probability. */
  val qualityScore: Int,
  val hasScoreBreakdown: Boolean,
  val accuracyFactor: Double,
  val freshnessFactor: Double,
  val movementFactor: Double,
  val consistencyFactor: Double,
  val rawScore: Int,
  val limitsApplied: List<String>,
  val activityNow: Int,
  val activityConfidenceNow: Int,
  val standingStillSinceTheReading: Boolean,
  val activityKnownUntilNow: Boolean,
  val activityKnownSinceAgeMs: Long,
  val activityHasGaps: Boolean,
  val reasons: List<String>,
  val steps: List<String>,
  val firstAttemptTimedOut: Boolean,
  val extraTimeWasUsed: Boolean,
  /** The public text of the reason, or null when there was none. */
  val fallbackCause: String?,
  val readingsReceived: Int,
  val readingsKept: Int,
  val readingsExamined: Int,
  val gpsJumpsFound: Int,
  val durationMs: Long,
)
