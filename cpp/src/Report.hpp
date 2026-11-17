// The final report of one request.
//
// The app asks: "is the courier near this address?". The engine never answers
// just yes or no. It answers with a report, called a report, that says:
//
//   - which GPS reading was used, how old it is and where it came from;
//   - how far the phone could really be from that reading;
//   - what the evidence says about the delivery radius: inside, outside, or
//     we cannot tell;
//   - a quality score from 0 to 100 (an index, never a probability);
//   - the reasons behind all of that, as short fixed labels.
//
// This file builds that report from what is in memory at one moment: the GPS
// readings and what the phone reported about its activity. It does not wait
// for anything and it does not talk to the GPS. Asking the GPS for new
// readings, and deciding how long to wait for them, is the job of
// ProximityRequest.cpp.
//
// The steps:
//
//   Step 1 - Choose the best reading          (ReadingSelection.cpp)
//   Step 2 - Where did that reading come from: fresh, recovered or cached?
//   Step 3 - Compare it with the delivery radius   (ProximityRelation.cpp)
//   Step 4 - Calculate the quality score           (QualityScore.cpp)
//   Step 5 - Decide the status: available, degraded or unavailable
//   Step 6 - Write down the reasons and describe the phone activity
//
// The report is evidence for the app. It is never an approval of the
// delivery and never proof that a person was at the address.
//
// A picture of the order of the calls, with examples worked step by step, is
// at the top of Report.cpp.
#ifndef LCE_REPORT_HPP
#define LCE_REPORT_HPP

#include <array>
#include <cstddef>
#include <cstdint>

#include "LocationHistory.hpp"
#include "PhoneActivityTimeline.hpp"
#include "ProximityRelation.hpp"
#include "QualityScore.hpp"
#include "ReadingSelection.hpp"
#include "ReasonCodes.hpp"
#include "Types.hpp"
#include "UncertaintyModel.hpp"

namespace lce {

// How good the answer is as a whole.
enum class ReportStatus : std::uint8_t {
  // A reading obtained just now, precise enough, with nothing suspicious.
  Available,

  // There is a reading, but something about it is weak: it came from the
  // history, or it is imprecise, or it looks like a GPS jump...
  Degraded,

  // There is no reading we can use.
  Unavailable,

  // The request was stopped before it finished.
  Cancelled,
};

// How much we trust the age of the reading that was used.
enum class AgeReliability : std::uint8_t {
  NotApplicable,  // no reading was used
  Monotonic,      // measured with a clock the user cannot change
  Estimated,      // worked out from the wall clock (iOS)
  Unreliable,     // readings were thrown away because their time made no sense
};

// The location permission the user gave to the app.
enum class LocationPermission : std::uint8_t {
  NotDetermined,  // the user was never asked
  Denied,
  Restricted,     // blocked by the device (parental controls, company policy)
  Granted,
};

// Whether the user allowed the precise location or only an approximate one.
enum class PermissionPrecision : std::uint8_t { Precise, Approximate, Unknown };

// What the app asked for in this request.
struct WhatTheAppAskedFor {
  // The address, and how close to it counts as "there".
  Coordinates destination;
  double radiusMeters = 100.0;

  // A reading up to this old still counts as "obtained now". Normally 10 seconds.
  std::int64_t newestAgeThatCountsAsFreshMs = 10000;

  // A reading older than this is never used. Normally 60 seconds.
  std::int64_t oldestAgeAllowedMs = 60000;

  // A reading with a GPS error above this is not "good": the answer becomes
  // degraded. Normally 50 meters.
  double worstGpsErrorAcceptedMeters = 50.0;

  // Normally 45.
  double fastestPossibleSpeedMetersPerSecond = FASTEST_POSSIBLE_SPEED_METERS_PER_SECOND;

  // False: ignore what the phone reported about its activity.
  bool usePhoneActivity = true;

  // False: only use readings obtained by this request, never older ones from
  // the history.
  bool allowAnswerFromHistory = true;

  // What to do with a reading the system marked as simulated.
  MockPolicy mockPolicy = MockPolicy::ZeroScore;
};

// What the request knows when it asks for a report.
struct WhatTheRequestKnows {
  // The arrival number the history was going to give next when this request
  // started. Every reading with this number or a higher one was obtained by
  // this request. (See LocationReading::arrivalNumber.)
  std::uint64_t firstArrivalNumberOfThisRequest = 1;

  // The same, for the moment the extra time started. 0 means the extra time
  // was not used.
  std::uint64_t firstArrivalNumberOfExtraTime = 0;

  LocationPermission locationPermission = LocationPermission::Granted;
  PermissionPrecision permissionPrecision = PermissionPrecision::Precise;
};

// What we can say about the phone activity, for the final report.
struct PhoneActivitySummary {
  // What the phone is doing right now, and how sure it is.
  PhoneActivity activityNow = PhoneActivity::Unknown;
  PhoneActivityConfidence confidenceNow = PhoneActivityConfidence::Unknown;

  // True when the phone was standing still during the whole time since the
  // reading was taken, with no hole.
  bool standingStillSinceTheReading = false;

  // Going back from now, for how long do we know the activity with no break?
  // `knownSinceAgeMs` is only meaningful when `knownUntilNow` is true.
  bool knownUntilNow = false;
  std::int64_t knownSinceAgeMs = 0;

  // True when part of the time since the reading is known and part is not.
  bool hasGaps = false;
};

// The steps a request went through, in the order they happened.
enum class RequestStep : std::uint8_t {
  Started = 0,
  FirstAttempt,       // waiting for a good reading from the GPS
  ExtraTime,          // the first attempt was not enough: waiting some more
  AnswerFromHistory,  // nothing new and good arrived: using what we remember
  Finished,
  Count  // not a step: only tells how many steps exist
};

inline constexpr std::size_t REQUEST_STEP_COUNT =
    static_cast<std::size_t>(RequestStep::Count);

// The report can list at most this many steps.
inline constexpr std::size_t MAX_STEPS_IN_REPORT = 8;

// Public text of a step, for example "first_attempt".
const char* requestStepName(RequestStep step) noexcept;

// THE FINAL REPORT.
struct Report {
  // Which request and which session this report belongs to (0 = none).
  // Filled by the engine.
  std::uint32_t requestId = 0;
  std::uint32_t sessionId = 0;

  ReportStatus status = ReportStatus::Unavailable;
  FixSource source = FixSource::Unavailable;

  // The reading that was used. When `hasPosition` is false there is none and
  // the six values below mean nothing.
  bool hasPosition = false;
  Coordinates position;
  double distanceToDestinationMeters = 0.0;
  double gpsErrorMeters = 0.0;
  double howFarPhoneCouldBeMeters = 0.0;
  std::int64_t readingAgeMs = 0;
  MockSignal mockSignal = MockSignal::Unknown;

  double radiusMeters = 0.0;
  AgeReliability ageReliability = AgeReliability::NotApplicable;
  PermissionPrecision permissionPrecision = PermissionPrecision::Unknown;

  // Inside, outside, or we cannot tell.
  EvidenceRelation relation = EvidenceRelation::Inconclusive;

  // The quality score with its four grades and the limits that applied.
  // `score.qualityScore` is the final number, from 0 to 100.
  ScoreResult score;

  PhoneActivitySummary phoneActivity;

  // Why the report says what it says.
  ReasonSet reasons;

  // --- Filled by the request (ProximityRequest.cpp) ---------------

  // The steps the request went through.
  std::array<RequestStep, MAX_STEPS_IN_REPORT> steps{};
  std::size_t howManySteps = 0;

  bool firstAttemptTimedOut = false;
  bool extraTimeWasUsed = false;

  // Why the engine had to go beyond the first attempt. Only meaningful when
  // `hasFallbackCause` is true.
  bool hasFallbackCause = false;
  ReasonCode fallbackCause = ReasonCode::NoUsableFix;

  std::uint32_t readingsReceived = 0;  // delivered by the GPS during this request
  std::uint32_t readingsKept = 0;      // of those, how many were stored
  std::int64_t durationMs = 0;

  // --- Filled here -------------------------------------------------------------

  std::uint32_t readingsExamined = 0;  // readings in the history when the report was built
  std::uint32_t gpsJumpsFound = 0;     // readings labelled "probably a GPS jump"
};

// THE MAIN FUNCTION OF THIS FILE.
// Builds the report from what is in memory right now.
//
//   history            the GPS readings we remember
//   activityHistory    what the phone reported about its activity
//   nowMs              the current time, on the same clock as the readings
//   asked              what the app asked for (address, radius, limits)
//   request            what the request knows (which readings it
//                      obtained itself, the permission)
//
// The status is never Cancelled here: only the request cancels.
Report buildReport(const LocationHistory& history,
                           const PhoneActivityHistory& activityHistory,
                           std::int64_t nowMs,
                           const WhatTheAppAskedFor& asked,
                           const WhatTheRequestKnows& request) noexcept;

}  // namespace lce

#endif  // LCE_REPORT_HPP
