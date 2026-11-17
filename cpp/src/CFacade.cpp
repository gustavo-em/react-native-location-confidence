// The code behind the front door declared in lce.h.
//
// Kotlin and Swift cannot use C++ classes, so they call the plain C functions
// of lce.h. Each function in this file is a thin translator:
//
//     plain C values  ->  the C++ types of the engine  ->  Engine.cpp
//     and the answer travels back the same way.
//
// No business rule lives here. The only decisions this file takes are about
// values that make no sense: a null pointer gives an error code, and a number
// that is not one of the listed choices becomes the cautious choice (for
// example, an unknown permission value counts as "the user was never asked").
//
// =============================================================================
// THE ORDER OF THE CALLS IN THIS FILE
// =============================================================================
//
//   lce_start_evaluation(engine, session_id, settings, now_ms, request_id)
//     |-- toRequestSettings()             C struct  ->  C++ struct
//     |      `-- toDeviceState()
//     `-- engine->engine.startRequest()                    (Engine.cpp)
//
//   lce_readings_arrived(engine, readings, how_many, now_ms)
//     |-- toUncheckedReading()            once for each reading
//     `-- engine->engine.whenReadingsArrive()              (Engine.cpp)
//
//   lce_take_finished_report(engine, report)
//     |-- engine->engine.takeFinishedReport()              (Engine.cpp)
//     `-- fillReport()                    C++ struct  ->  C struct
//
// Every other function follows the same pattern with fewer steps.
// =============================================================================
#include "location_confidence/lce.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <new>

#include "Report.hpp"
#include "Engine.hpp"
#include "ProximityRequest.hpp"
#include "QualityScore.hpp"
#include "ReadingChecker.hpp"
#include "ReasonCodes.hpp"

// What the platform holds a pointer to: one engine and nothing else.
struct lce_engine {
  lce::Engine engine;
};

// =============================================================================
// HELPERS - used only inside this file
// =============================================================================
namespace {

using namespace lce;

// --- The numbers of lce.h must mean the same as the C++ lists -----------------
// When a number is sent back to the platform (a status, a source...), this
// file simply turns the C++ value into its number. That is only correct while
// both lists are in the same order. These lines stop the build if someone
// changes one list and forgets the other.

constexpr bool sameNumber(int numberInLceH, int numberInCpp) noexcept {
  return numberInLceH == numberInCpp;
}

static_assert(sameNumber(LCE_STATUS_AVAILABLE, static_cast<int>(ReportStatus::Available)), "");
static_assert(sameNumber(LCE_STATUS_DEGRADED, static_cast<int>(ReportStatus::Degraded)), "");
static_assert(sameNumber(LCE_STATUS_UNAVAILABLE, static_cast<int>(ReportStatus::Unavailable)),
              "");
static_assert(sameNumber(LCE_STATUS_CANCELLED, static_cast<int>(ReportStatus::Cancelled)), "");

static_assert(sameNumber(LCE_SOURCE_FRESH, static_cast<int>(FixSource::Fresh)), "");
static_assert(sameNumber(LCE_SOURCE_RECOVERED, static_cast<int>(FixSource::Recovered)), "");
static_assert(sameNumber(LCE_SOURCE_CACHED, static_cast<int>(FixSource::Cached)), "");
static_assert(sameNumber(LCE_SOURCE_UNAVAILABLE, static_cast<int>(FixSource::Unavailable)), "");

static_assert(sameNumber(LCE_RELATION_SUPPORTS_INSIDE,
                         static_cast<int>(EvidenceRelation::SupportsInside)),
              "");
static_assert(sameNumber(LCE_RELATION_SUPPORTS_OUTSIDE,
                         static_cast<int>(EvidenceRelation::SupportsOutside)),
              "");
static_assert(sameNumber(LCE_RELATION_INCONCLUSIVE,
                         static_cast<int>(EvidenceRelation::Inconclusive)),
              "");

static_assert(sameNumber(LCE_AGE_NOT_APPLICABLE, static_cast<int>(AgeReliability::NotApplicable)),
              "");
static_assert(sameNumber(LCE_AGE_MONOTONIC, static_cast<int>(AgeReliability::Monotonic)), "");
static_assert(sameNumber(LCE_AGE_ESTIMATED, static_cast<int>(AgeReliability::Estimated)), "");
static_assert(sameNumber(LCE_AGE_UNRELIABLE, static_cast<int>(AgeReliability::Unreliable)), "");

static_assert(sameNumber(LCE_PRECISION_PRECISE, static_cast<int>(PermissionPrecision::Precise)),
              "");
static_assert(sameNumber(LCE_PRECISION_APPROXIMATE,
                         static_cast<int>(PermissionPrecision::Approximate)),
              "");
static_assert(sameNumber(LCE_PRECISION_UNKNOWN, static_cast<int>(PermissionPrecision::Unknown)),
              "");

static_assert(sameNumber(LCE_MOCK_UNKNOWN, static_cast<int>(MockSignal::Unknown)), "");
static_assert(sameNumber(LCE_MOCK_NOT_FLAGGED, static_cast<int>(MockSignal::NotFlaggedByOs)), "");
static_assert(sameNumber(LCE_MOCK_FLAGGED, static_cast<int>(MockSignal::FlaggedByOs)), "");

static_assert(sameNumber(LCE_ACTIVITY_STANDING_STILL,
                         static_cast<int>(PhoneActivity::StandingStill)),
              "");
static_assert(sameNumber(LCE_ACTIVITY_WALKING, static_cast<int>(PhoneActivity::Walking)), "");
static_assert(sameNumber(LCE_ACTIVITY_RUNNING, static_cast<int>(PhoneActivity::Running)), "");
static_assert(sameNumber(LCE_ACTIVITY_CYCLING, static_cast<int>(PhoneActivity::Cycling)), "");
static_assert(sameNumber(LCE_ACTIVITY_IN_VEHICLE, static_cast<int>(PhoneActivity::InVehicle)), "");
static_assert(sameNumber(LCE_ACTIVITY_UNKNOWN, static_cast<int>(PhoneActivity::Unknown)), "");

static_assert(sameNumber(LCE_CONFIDENCE_HIGH, static_cast<int>(PhoneActivityConfidence::High)),
              "");
static_assert(sameNumber(LCE_CONFIDENCE_MEDIUM,
                         static_cast<int>(PhoneActivityConfidence::Medium)),
              "");
static_assert(sameNumber(LCE_CONFIDENCE_LOW, static_cast<int>(PhoneActivityConfidence::Low)), "");
static_assert(sameNumber(LCE_CONFIDENCE_UNKNOWN,
                         static_cast<int>(PhoneActivityConfidence::Unknown)),
              "");

static_assert(sameNumber(LCE_COLLECTING_NOT_REQUESTED,
                         static_cast<int>(CollectingState::NotRequested)),
              "");
static_assert(sameNumber(LCE_COLLECTING_ACTIVE, static_cast<int>(CollectingState::Active)), "");
static_assert(sameNumber(LCE_COLLECTING_BLOCKED, static_cast<int>(CollectingState::Blocked)), "");
static_assert(sameNumber(LCE_COLLECTING_STOPPED, static_cast<int>(CollectingState::Stopped)), "");

static_assert(LCE_MAX_STEPS_IN_REPORT == MAX_STEPS_IN_REPORT,
              "lce.h and Report.hpp disagree about how many steps a report holds");

// The reasons travel as one bit each inside a 64-bit number, and the limits
// of the score inside a 32-bit number. There must be enough bits.
static_assert(REASON_CODE_COUNT <= 64, "too many reasons for a 64-bit number");
static_assert(SCORE_LIMIT_COUNT <= 32, "too many score limits for a 32-bit number");

// --- From the numbers of lce.h to the C++ lists ---------------------------------
// A number that is not one of the choices becomes the cautious choice.

bool isYes(int32_t value) noexcept {
  return value != 0;
}

LocationPermission toLocationPermission(int32_t value) noexcept {
  LocationPermission permission = LocationPermission::NotDetermined;
  if (value == LCE_PERMISSION_DENIED) {
    permission = LocationPermission::Denied;
  } else if (value == LCE_PERMISSION_RESTRICTED) {
    permission = LocationPermission::Restricted;
  } else if (value == LCE_PERMISSION_GRANTED) {
    permission = LocationPermission::Granted;
  }
  return permission;
}

PermissionPrecision toPermissionPrecision(int32_t value) noexcept {
  PermissionPrecision precision = PermissionPrecision::Unknown;
  if (value == LCE_PRECISION_PRECISE) {
    precision = PermissionPrecision::Precise;
  } else if (value == LCE_PRECISION_APPROXIMATE) {
    precision = PermissionPrecision::Approximate;
  }
  return precision;
}

LocationProviderState toLocationProviderState(int32_t value) noexcept {
  LocationProviderState state = LocationProviderState::Unavailable;
  if (value == LCE_PROVIDER_STATE_AVAILABLE) {
    state = LocationProviderState::Available;
  } else if (value == LCE_PROVIDER_STATE_PLAY_SERVICES_MISSING) {
    state = LocationProviderState::PlayServicesMissing;
  }
  return state;
}

PhoneActivitySupport toPhoneActivitySupport(int32_t value) noexcept {
  PhoneActivitySupport support = PhoneActivitySupport::NotSupported;
  if (value == LCE_ACTIVITY_SUPPORT_AVAILABLE) {
    support = PhoneActivitySupport::Available;
  } else if (value == LCE_ACTIVITY_SUPPORT_PERMISSION_DENIED) {
    support = PhoneActivitySupport::PermissionDenied;
  } else if (value == LCE_ACTIVITY_SUPPORT_PERMISSION_NOT_ASKED) {
    support = PhoneActivitySupport::PermissionNotAsked;
  }
  return support;
}

PhoneActivity toPhoneActivity(int32_t value) noexcept {
  const bool isOneOfTheChoices = (value >= LCE_ACTIVITY_STANDING_STILL &&
                                  value <= LCE_ACTIVITY_UNKNOWN);
  return isOneOfTheChoices ? static_cast<PhoneActivity>(value) : PhoneActivity::Unknown;
}

PhoneActivityConfidence toPhoneActivityConfidence(int32_t value) noexcept {
  const bool isOneOfTheChoices = (value >= LCE_CONFIDENCE_HIGH && value <= LCE_CONFIDENCE_UNKNOWN);
  return isOneOfTheChoices ? static_cast<PhoneActivityConfidence>(value)
                           : PhoneActivityConfidence::Unknown;
}

Provider toProvider(int32_t value) noexcept {
  Provider provider = Provider::Fused;
  if (value == LCE_READING_PROVIDER_CORE_LOCATION) {
    provider = Provider::CoreLocation;
  } else if (value == LCE_READING_PROVIDER_SYSTEM_CACHE) {
    provider = Provider::SystemCache;
  }
  return provider;
}

MockSignal toMockSignal(int32_t value) noexcept {
  MockSignal signal = MockSignal::Unknown;
  if (value == LCE_MOCK_NOT_FLAGGED) {
    signal = MockSignal::NotFlaggedByOs;
  } else if (value == LCE_MOCK_FLAGGED) {
    signal = MockSignal::FlaggedByOs;
  }
  return signal;
}

MockPolicy toMockPolicy(int32_t value) noexcept {
  // The cautious choice is the strict one: a simulated reading scores 0.
  return value == LCE_MOCK_POLICY_FLAG_ONLY ? MockPolicy::FlagOnly : MockPolicy::ZeroScore;
}

// --- From the C structs to the C++ structs ----------------------------------------

DeviceState toDeviceState(const lce_device_state& device) noexcept {
  DeviceState result;
  result.locationPermission = toLocationPermission(device.location_permission);
  result.permissionPrecision = toPermissionPrecision(device.permission_precision);
  result.locationIsTurnedOn = isYes(device.location_is_turned_on);
  result.locationProvider = toLocationProviderState(device.location_provider);
  result.appIsInForeground = isYes(device.app_is_in_foreground);
  result.phoneActivity = toPhoneActivitySupport(device.phone_activity_support);
  return result;
}

SessionSettings toSessionSettings(const lce_session_settings& settings) noexcept {
  SessionSettings result;
  result.keepCollectingReadings = isYes(settings.keep_collecting_readings);
  result.timeBetweenReadingsMs = settings.time_between_readings_ms;
  result.longestCollectingTimeMs = settings.longest_collecting_time_ms;
  result.device = toDeviceState(settings.device);
  return result;
}

RequestSettings toRequestSettings(const lce_evaluation_settings& settings) noexcept {
  RequestSettings result;

  result.asked.destination.latitudeDegrees = settings.destination_latitude;
  result.asked.destination.longitudeDegrees = settings.destination_longitude;
  result.asked.radiusMeters = settings.radius_meters;
  result.asked.newestAgeThatCountsAsFreshMs = settings.newest_age_that_counts_as_fresh_ms;
  result.asked.oldestAgeAllowedMs = settings.oldest_age_allowed_ms;
  result.asked.worstGpsErrorAcceptedMeters = settings.worst_gps_error_accepted_meters;
  result.asked.fastestPossibleSpeedMetersPerSecond = settings.fastest_possible_speed_mps;
  result.asked.usePhoneActivity = isYes(settings.use_phone_activity);
  result.asked.allowAnswerFromHistory = isYes(settings.allow_answer_from_history);
  result.asked.mockPolicy = toMockPolicy(settings.mock_policy);

  result.totalTimeAllowedMs = settings.total_time_allowed_ms;
  result.firstAttemptTimeMs = settings.first_attempt_time_ms;
  result.extraTimeMs = settings.extra_time_ms;

  result.device = toDeviceState(settings.device);
  return result;
}

UncheckedReading toUncheckedReading(const lce_reading& reading) noexcept {
  UncheckedReading result;
  result.latitudeDegrees = reading.latitude;
  result.longitudeDegrees = reading.longitude;
  result.hasAccuracy = isYes(reading.has_accuracy);
  result.horizontalAccuracyMeters = reading.accuracy_meters;
  result.capturedAtMs = reading.captured_at_ms;
  result.timeReliable = isYes(reading.time_is_reliable);
  result.captureTimeWasEstimated = isYes(reading.capture_time_was_estimated);
  result.onlyApproximatePermission = isYes(reading.only_approximate_permission);
  result.hasSpeed = isYes(reading.has_speed);
  result.speedMetersPerSecond = reading.speed_mps;
  result.hasSpeedAccuracy = isYes(reading.has_speed_accuracy);
  result.speedAccuracyMetersPerSecond = reading.speed_accuracy_mps;
  result.hasHeading = isYes(reading.has_heading);
  result.headingDegrees = reading.heading_degrees;
  result.provider = toProvider(reading.provider);
  result.mockSignal = toMockSignal(reading.mock_signal);
  return result;
}

// --- From the C++ answers to the C structs ------------------------------------------

int32_t yesOrNo(bool value) noexcept {
  return value ? 1 : 0;
}

// One bit for each reason that is in the set: bit N for reason number N.
uint64_t toReasonBits(const ReasonSet& reasons) noexcept {
  uint64_t bits = 0;
  for (std::size_t number = 0; number < REASON_CODE_COUNT; ++number) {
    if (reasons.contains(static_cast<ReasonCode>(number))) {
      bits |= (uint64_t{1} << number);
    }
  }
  return bits;
}

// One bit for each limit of the score that applied.
uint32_t toLimitBits(const ScoreResult& score) noexcept {
  uint32_t bits = 0;
  for (std::size_t number = 0; number < SCORE_LIMIT_COUNT; ++number) {
    if (score.limitWasApplied[number]) {
      bits |= (uint32_t{1} << number);
    }
  }
  return bits;
}

void fillReport(const Report& engineReport, lce_report& report) noexcept {
  report = lce_report{};  // every field starts at zero

  report.request_id = engineReport.requestId;
  report.session_id = engineReport.sessionId;
  report.status = static_cast<int32_t>(engineReport.status);
  report.source = static_cast<int32_t>(engineReport.source);

  report.has_position = yesOrNo(engineReport.hasPosition);
  if (engineReport.hasPosition) {
    report.latitude = engineReport.position.latitudeDegrees;
    report.longitude = engineReport.position.longitudeDegrees;
    report.distance_to_destination_meters = engineReport.distanceToDestinationMeters;
    report.gps_error_meters = engineReport.gpsErrorMeters;
    report.how_far_phone_could_be_meters = engineReport.howFarPhoneCouldBeMeters;
    report.reading_age_ms = engineReport.readingAgeMs;
    report.mock_signal = static_cast<int32_t>(engineReport.mockSignal);
  }

  report.radius_meters = engineReport.radiusMeters;
  report.age_reliability = static_cast<int32_t>(engineReport.ageReliability);
  report.permission_precision = static_cast<int32_t>(engineReport.permissionPrecision);
  report.relation = static_cast<int32_t>(engineReport.relation);

  report.quality_score = engineReport.score.qualityScore;
  report.has_score_breakdown = yesOrNo(engineReport.score.hasBreakdown);
  if (engineReport.score.hasBreakdown) {
    report.accuracy_factor = engineReport.score.accuracyFactor;
    report.freshness_factor = engineReport.score.freshnessFactor;
    report.movement_factor = engineReport.score.movementFactor;
    report.consistency_factor = engineReport.score.consistencyFactor;
    report.raw_score = engineReport.score.rawScore;
  }
  report.limits_applied = toLimitBits(engineReport.score);

  report.activity_now = static_cast<int32_t>(engineReport.phoneActivity.activityNow);
  report.activity_confidence_now = static_cast<int32_t>(engineReport.phoneActivity.confidenceNow);
  report.standing_still_since_the_reading =
      yesOrNo(engineReport.phoneActivity.standingStillSinceTheReading);
  report.activity_known_until_now = yesOrNo(engineReport.phoneActivity.knownUntilNow);
  report.activity_known_since_age_ms = engineReport.phoneActivity.knownSinceAgeMs;
  report.activity_has_gaps = yesOrNo(engineReport.phoneActivity.hasGaps);

  report.reasons = toReasonBits(engineReport.reasons);

  for (std::size_t position = 0; position < engineReport.howManySteps; ++position) {
    report.steps[position] = static_cast<int32_t>(engineReport.steps[position]);
  }
  report.how_many_steps = static_cast<int32_t>(engineReport.howManySteps);

  report.first_attempt_timed_out = yesOrNo(engineReport.firstAttemptTimedOut);
  report.extra_time_was_used = yesOrNo(engineReport.extraTimeWasUsed);
  report.fallback_cause =
      engineReport.hasFallbackCause ? static_cast<int32_t>(engineReport.fallbackCause) : -1;

  report.readings_received = engineReport.readingsReceived;
  report.readings_kept = engineReport.readingsKept;
  report.readings_examined = engineReport.readingsExamined;
  report.gps_jumps_found = engineReport.gpsJumpsFound;
  report.duration_ms = engineReport.durationMs;
}

// The engine takes the readings as one list. This is how many the facade
// converts and hands over at a time; a longer delivery is passed in pieces.
constexpr std::size_t READINGS_CONVERTED_AT_A_TIME = 16;

}  // namespace

// =============================================================================
// THE FUNCTIONS OF lce.h
//
// `extern "C"` must match the declarations in lce.h so that both sides agree
// on the names of the functions.
//
// No C++ error (exception) can escape from these functions into Kotlin or
// Swift, which would crash the app: everything they call is marked
// `noexcept`, and the only place that asks the system for memory
// (lce_engine_create) uses the form of `new` that returns NULL instead of
// raising an error.
// =============================================================================

extern "C" uint32_t lce_abi_version(void) {
  return LCE_ABI_VERSION;
}

extern "C" lce_engine* lce_engine_create(void) {
  // `std::nothrow` makes `new` return NULL when there is no memory, instead
  // of raising a C++ error.
  return new (std::nothrow) lce_engine();
}

extern "C" void lce_engine_destroy(lce_engine* engine) {
  if (engine == nullptr) {
    return;
  }
  // Overwrite everything it remembered before the memory is given back.
  engine->engine = lce::Engine{};
  delete engine;
}

// --- Sessions ----------------------------------------------------------------

extern "C" int32_t lce_open_session(lce_engine* engine,
                                    const lce_session_settings* settings,
                                    int64_t now_ms,
                                    lce_session_opened* opened) {
  if (engine == nullptr || settings == nullptr || opened == nullptr) {
    return LCE_ERROR_NULL_ARGUMENT;
  }

  const lce::SessionOpened result =
      engine->engine.openSession(toSessionSettings(*settings), now_ms);

  opened->session_id = result.sessionId;
  opened->collecting = static_cast<int32_t>(result.collecting);
  opened->reasons = toReasonBits(result.reasons);
  return LCE_OK;
}

extern "C" int32_t lce_clear_session_history(lce_engine* engine, uint32_t session_id) {
  if (engine == nullptr) {
    return LCE_ERROR_NULL_ARGUMENT;
  }
  const bool sessionExists = engine->engine.clearSessionHistory(session_id);
  return sessionExists ? LCE_OK : LCE_ERROR_NO_SUCH_SESSION;
}

extern "C" int32_t lce_close_session(lce_engine* engine, uint32_t session_id, int64_t now_ms) {
  if (engine == nullptr) {
    return LCE_ERROR_NULL_ARGUMENT;
  }
  engine->engine.closeSession(session_id, now_ms);
  return LCE_OK;
}

// --- Requests -------------------------------------------------------------

extern "C" int32_t lce_start_evaluation(lce_engine* engine,
                                        uint32_t session_id,
                                        const lce_evaluation_settings* settings,
                                        int64_t now_ms,
                                        uint32_t* request_id) {
  if (engine == nullptr || settings == nullptr || request_id == nullptr) {
    return LCE_ERROR_NULL_ARGUMENT;
  }

  const lce::RequestStarted started =
      engine->engine.startRequest(session_id, toRequestSettings(*settings), now_ms);

  int32_t answer = LCE_OK;
  if (started.result == lce::RequestStarted::Result::InvalidRequest) {
    answer = LCE_ERROR_INVALID_REQUEST;
  } else if (started.result == lce::RequestStarted::Result::TooManyReportsWaiting) {
    answer = LCE_ERROR_TOO_MANY_REPORTS_WAITING;
  }

  *request_id = started.requestId;
  return answer;
}

extern "C" int32_t lce_cancel_evaluation(lce_engine* engine,
                                         uint32_t request_id,
                                         int64_t now_ms) {
  if (engine == nullptr) {
    return LCE_ERROR_NULL_ARGUMENT;
  }
  engine->engine.cancelRequest(request_id, now_ms);
  return LCE_OK;
}

// --- Things that happen ------------------------------------------------------

extern "C" int32_t lce_readings_arrived(lce_engine* engine,
                                        const lce_reading* readings,
                                        uint32_t how_many,
                                        int64_t now_ms) {
  if (engine == nullptr) {
    return LCE_ERROR_NULL_ARGUMENT;
  }
  if (readings == nullptr && how_many > 0) {
    return LCE_ERROR_NULL_ARGUMENT;
  }

  // Convert and hand over a few readings at a time, so that no memory has to
  // be asked from the system however long the delivery is.
  std::array<UncheckedReading, READINGS_CONVERTED_AT_A_TIME> converted{};
  std::size_t nextToConvert = 0;

  while (nextToConvert < how_many) {
    std::size_t howManyInThisPiece = 0;
    while (howManyInThisPiece < READINGS_CONVERTED_AT_A_TIME && nextToConvert < how_many) {
      converted[howManyInThisPiece] = toUncheckedReading(readings[nextToConvert]);
      howManyInThisPiece += 1;
      nextToConvert += 1;
    }
    engine->engine.whenReadingsArrive(converted.data(), howManyInThisPiece, now_ms);
  }

  return LCE_OK;
}

extern "C" int32_t lce_phone_activity_changed(lce_engine* engine,
                                              int32_t activity,
                                              int32_t confidence,
                                              int64_t since_ms,
                                              int64_t now_ms) {
  if (engine == nullptr) {
    return LCE_ERROR_NULL_ARGUMENT;
  }
  engine->engine.whenPhoneActivityChanges(toPhoneActivity(activity),
                                          toPhoneActivityConfidence(confidence), since_ms,
                                          now_ms);
  return LCE_OK;
}

extern "C" int32_t lce_phone_activity_updates_stopped(lce_engine* engine, int64_t now_ms) {
  if (engine == nullptr) {
    return LCE_ERROR_NULL_ARGUMENT;
  }
  engine->engine.whenPhoneActivityUpdatesStop(now_ms);
  return LCE_OK;
}

extern "C" int32_t lce_time_passed(lce_engine* engine, int64_t now_ms) {
  if (engine == nullptr) {
    return LCE_ERROR_NULL_ARGUMENT;
  }
  engine->engine.whenTimePasses(now_ms);
  return LCE_OK;
}

extern "C" int32_t lce_app_went_to_background(lce_engine* engine, int64_t now_ms) {
  if (engine == nullptr) {
    return LCE_ERROR_NULL_ARGUMENT;
  }
  engine->engine.whenAppGoesToBackground(now_ms);
  return LCE_OK;
}

extern "C" int32_t lce_location_permission_was_lost(lce_engine* engine, int64_t now_ms) {
  if (engine == nullptr) {
    return LCE_ERROR_NULL_ARGUMENT;
  }
  engine->engine.whenLocationPermissionIsLost(now_ms);
  return LCE_OK;
}

extern "C" int32_t lce_location_provider_failed(lce_engine* engine, int64_t now_ms) {
  if (engine == nullptr) {
    return LCE_ERROR_NULL_ARGUMENT;
  }
  engine->engine.whenLocationProviderFails(now_ms);
  return LCE_OK;
}

extern "C" int32_t lce_set_readings_are_simulated(lce_engine* engine,
                                                  int32_t readings_are_simulated) {
  if (engine == nullptr) {
    return LCE_ERROR_NULL_ARGUMENT;
  }
  engine->engine.setReadingsAreSimulated(isYes(readings_are_simulated));
  return LCE_OK;
}

// --- What the platform asks after every call ---------------------------------

extern "C" int32_t lce_what_the_platform_must_do_now(const lce_engine* engine,
                                                     lce_platform_work* work) {
  if (engine == nullptr || work == nullptr) {
    return LCE_ERROR_NULL_ARGUMENT;
  }

  const lce::PlatformWork result = engine->engine.whatThePlatformMustDoNow();

  work->location_updates_are_wanted = yesOrNo(result.locationUpdatesAreWanted);
  work->time_between_location_updates_ms = result.timeBetweenLocationUpdatesMs;
  work->phone_activity_updates_are_wanted = yesOrNo(result.phoneActivityUpdatesAreWanted);
  work->wake_up_is_wanted = yesOrNo(result.wakeUpIsWanted);
  work->wake_up_at_ms = result.wakeUpAtMs;
  return LCE_OK;
}

extern "C" int32_t lce_take_finished_report(lce_engine* engine, lce_report* report) {
  if (engine == nullptr || report == nullptr) {
    return 0;
  }

  lce::Report engineReport;
  const bool oneWasWaiting = engine->engine.takeFinishedReport(engineReport);
  if (oneWasWaiting) {
    fillReport(engineReport, *report);
  }
  return yesOrNo(oneWasWaiting);
}

// --- Texts -------------------------------------------------------------------

extern "C" uint32_t lce_reason_count(void) {
  return static_cast<uint32_t>(lce::REASON_CODE_COUNT);
}

extern "C" const char* lce_reason_name(int32_t reason) {
  const bool isInTheList =
      (reason >= 0 && static_cast<std::size_t>(reason) < lce::REASON_CODE_COUNT);
  return isInTheList ? lce::reasonCodeName(static_cast<lce::ReasonCode>(reason)) : nullptr;
}

extern "C" uint32_t lce_score_limit_count(void) {
  return static_cast<uint32_t>(lce::SCORE_LIMIT_COUNT);
}

extern "C" const char* lce_score_limit_name(int32_t limit) {
  const bool isInTheList =
      (limit >= 0 && static_cast<std::size_t>(limit) < lce::SCORE_LIMIT_COUNT);
  return isInTheList ? lce::scoreLimitName(static_cast<lce::ScoreLimit>(limit)) : nullptr;
}

extern "C" uint32_t lce_step_count(void) {
  return static_cast<uint32_t>(lce::REQUEST_STEP_COUNT);
}

extern "C" const char* lce_step_name(int32_t step) {
  const bool isInTheList =
      (step >= 0 && static_cast<std::size_t>(step) < lce::REQUEST_STEP_COUNT);
  return isInTheList ? lce::requestStepName(static_cast<lce::RequestStep>(step)) : nullptr;
}
