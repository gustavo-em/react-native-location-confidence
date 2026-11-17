// =============================================================================
// THE ORDER OF THE CALLS IN THIS FILE
// =============================================================================
//
//   buildReport(history, activityHistory, nowMs, asked, request)
//     |                                                    <- everything starts here
//     |-- no location permission?  ->  report "unavailable" and stop
//     |
//     |-- chooseBestReading()                              STEP 1  (ReadingSelection.cpp)
//     |      no reading can be used?  ->  report "unavailable" and stop
//     |
//     |-- findWhereTheReadingCameFrom()                    STEP 2  fresh, recovered or cached
//     |-- distanceBetweenMeters()                          STEP 3  (GeoMath.cpp)
//     |   classifyRelation()                                       (ProximityRelation.cpp)
//     |-- calculateQualityScore()                          STEP 4  (QualityScore.cpp)
//     |-- decideTheStatus()                                STEP 5  available or degraded
//     |-- writeDownTheReasons()                            STEP 6
//     `-- describePhoneActivity()                                  (PhoneActivityTimeline.cpp)
//
// =============================================================================
// EXAMPLES, STEP BY STEP
// The address is always at the door and the delivery radius is 100 meters.
// =============================================================================
//
// EXAMPLE 1 - one good reading obtained right now
//
//   one reading: obtained by this request, 2 seconds old, GPS error 10,
//                30 meters from the address
//   the phone reported "standing still" during those 2 seconds
//
//   buildReport()
//     chooseBestReading()              -> that reading
//                                         could have moved 0.5 x 2 = 1 meter
//                                         how far the phone could be = 10 + 1 = 11 meters
//     findWhereTheReadingCameFrom()    -> Fresh  (this request obtained it; 2 s is not old)
//     classifyRelation(30, 11, 100)    -> SupportsInside  (at most 30 + 11 = 41 meters away)
//     calculateQualityScore()
//       accuracy 0.90, freshness 0.967, movement 0.99, consistency 0.60 (one reading)
//       blend = 89;  limit "single_fix" = 75                 -> 75
//     decideTheStatus()                -> Available
//
// EXAMPLE 2 - nothing new arrived, the answer comes from the history
//
//   best reading: it was in the history before this request started,
//                 10 seconds old, GPS error 10, 30 meters from the address
//   the phone reported "in vehicle" during those 10 seconds
//   two other readings of the history agree with it
//
//     chooseBestReading()              -> could have moved 45 x 10 = 450 meters
//                                         how far the phone could be = 10 + 450 = 460 meters
//     findWhereTheReadingCameFrom()    -> Cached  (this request did not obtain it)
//     classifyRelation(30, 460, 100)   -> Inconclusive  (anywhere from 0 to 490 meters away)
//     calculateQualityScore()
//       accuracy 0.90, freshness 0.833, movement 0, consistency 1.00 (three readings)
//       blend = 72;  limits "cached" 60, "uncertainty_at_least_100m" 35,
//       "uncertainty_at_least_300m" 15                        -> 15
//     decideTheStatus()                -> Degraded  (it came from the history)
//
// EXAMPLE 3 - nothing can be used
//
//   the only reading of the history is 90 seconds old; the app accepts 60
//
//     chooseBestReading()              -> found = false
//     the report: Unavailable, no position, score 0, Inconclusive,
//                 reasons "no_usable_fix" and "location_stale"
//
// EXAMPLE 4 - a far reading says "outside" just as clearly
//
//   one fresh reading, GPS error 10, 1 second old, 500 meters from the address
//
//     how far the phone could be = 10 + 45 = 55 meters
//     classifyRelation(500, 55, 100)   -> SupportsOutside  (at least 500 - 55 = 445 away)
//
// =============================================================================
// WHAT THE REPORT IS USED FOR
// =============================================================================
//
// ProximityRequest.cpp calls buildReport() every time new readings
// arrive, to see whether the answer is already good enough to stop, and one
// last time when the time is over. The engine hands the final report to the
// platform code, which turns it into the object the app receives in
// JavaScript.
// =============================================================================

#include "Report.hpp"

#include "GeoMath.hpp"

namespace lce {

// =============================================================================
// HELPERS - used only inside this file
// =============================================================================
namespace {

// Public text of each step, in exactly the same order as the RequestStep
// list in Report.hpp.
constexpr const char* STEP_NAMES[] = {
    "started",
    "first_attempt",
    "extra_time",
    "answer_from_history",
    "finished",
};

static_assert(sizeof(STEP_NAMES) / sizeof(STEP_NAMES[0]) == REQUEST_STEP_COUNT,
              "the list of step names does not match the RequestStep list");

// Which reason explains a missing permission?
ReasonCode reasonForMissingPermission(LocationPermission permission) noexcept {
  ReasonCode reason = ReasonCode::PermissionDenied;

  if (permission == LocationPermission::NotDetermined) {
    reason = ReasonCode::PermissionNotDetermined;
  } else if (permission == LocationPermission::Restricted) {
    reason = ReasonCode::PermissionRestricted;
  }

  return reason;
}

// Step 2: where did the chosen reading come from?
//
//   Fresh      this request obtained it, in the first attempt, and it is not old
//   Recovered  this request obtained it, during the extra time, and it is not old
//   Cached     anything else: it was already in the history, or it is older
//              than what counts as fresh (the system can hand us an old
//              reading it had stored)
//
// "Obtained by this request" is told by the arrival number: the request
// remembered which number was next when it started.
FixSource findWhereTheReadingCameFrom(const WhatWeKnowAboutReading& best,
                                      const WhatTheAppAskedFor& asked,
                                      const WhatTheRequestKnows& request) noexcept {
  const std::uint64_t arrivalNumber = best.reading.arrivalNumber;

  const bool thisRequestObtainedIt = (arrivalNumber >= request.firstArrivalNumberOfThisRequest);
  const bool extraTimeWasUsed = (request.firstArrivalNumberOfExtraTime != 0);
  const bool arrivedDuringExtraTime =
      extraTimeWasUsed && (arrivalNumber >= request.firstArrivalNumberOfExtraTime);
  const bool isNotOld = (best.ageMs <= asked.newestAgeThatCountsAsFreshMs);

  FixSource source = FixSource::Cached;

  if (thisRequestObtainedIt && isNotOld && arrivedDuringExtraTime) {
    source = FixSource::Recovered;
  } else if (thisRequestObtainedIt && isNotOld) {
    source = FixSource::Fresh;
  }

  return source;
}

// Step 5: is the answer "available" or only "degraded"?
// Available needs all of these: the reading was obtained now, it is precise
// enough, it is not a probable GPS jump, it is not blocked for being
// simulated, and the location was not blurred by an approximate permission.
ReportStatus decideTheStatus(const Report& report,
                                 const WhatTheAppAskedFor& asked,
                                 bool isProbablyAGpsJump,
                                 bool blockedForBeingSimulated,
                                 bool locationIsOnlyApproximate) noexcept {
  const bool wasObtainedNow =
      (report.source == FixSource::Fresh || report.source == FixSource::Recovered);
  const bool isPreciseEnough = (report.gpsErrorMeters <= asked.worstGpsErrorAcceptedMeters);

  const bool everythingIsFine = wasObtainedNow && isPreciseEnough && !isProbablyAGpsJump &&
                                !blockedForBeingSimulated && !locationIsOnlyApproximate;

  return everythingIsFine ? ReportStatus::Available : ReportStatus::Degraded;
}

// Describes the phone activity for the report.
// `best` is nullptr when no reading was chosen: then only "what the phone is
// doing right now" can be told.
PhoneActivitySummary describePhoneActivity(const PhoneActivityHistory& activityHistory,
                                           const WhatWeKnowAboutReading* best,
                                           std::int64_t nowMs,
                                           const WhatTheAppAskedFor& asked) noexcept {
  PhoneActivitySummary summary;

  if (!asked.usePhoneActivity) {
    return summary;  // the app asked us not to look at the phone activity
  }

  const PhoneActivityAtOneMoment rightNow = findPhoneActivityAtMoment(activityHistory, nowMs);
  summary.activityNow = rightNow.activity;
  summary.confidenceNow = rightNow.confidence;

  const bool thereIsAReading = (best != nullptr);
  if (thereIsAReading) {
    const TimeInEachPhoneActivity& table = best->timeInEachActivity;

    summary.standingStillSinceTheReading = table.standingStillTheWholeTime();

    const std::int64_t knownSinceMs =
        findSinceWhenPhoneActivityIsKnown(activityHistory, best->reading.capturedAtMs, nowMs);
    summary.knownUntilNow = (knownSinceMs < nowMs);
    if (summary.knownUntilNow) {
      summary.knownSinceAgeMs = nowMs - knownSinceMs;
    }

    const bool partOfTheTimeIsUnknown = (table.millisecondsUnknown > 0);
    const bool partOfTheTimeIsKnown = !table.weKnowNothing();
    summary.hasGaps = partOfTheTimeIsKnown && partOfTheTimeIsUnknown;
  }

  return summary;
}

// Step 6: the reasons that explain a report that has a reading.
void writeDownTheReasons(Report& report,
                         const ChosenReading& chosen,
                         const WhatTheAppAskedFor& asked,
                         bool locationIsOnlyApproximate) noexcept {
  const WhatWeKnowAboutReading& best = chosen.best;
  ReasonSet& reasons = report.reasons;

  // Every distance in the report comes from speeds that are guesses.
  reasons.add(ReasonCode::UncertaintyModelHeuristic);

  const bool isImprecise = (report.gpsErrorMeters > asked.worstGpsErrorAcceptedMeters);
  if (isImprecise) {
    reasons.add(ReasonCode::LocationLowAccuracy);
  }

  const bool isOld = (best.ageMs > asked.newestAgeThatCountsAsFreshMs);
  if (isOld) {
    reasons.add(ReasonCode::LocationStale);
  }

  if (chosen.someReadingsContradictEachOther) {
    reasons.add(ReasonCode::SuspectedGpsJump);
  }
  if (chosen.readingsFromDifferentProvidersContradict) {
    reasons.add(ReasonCode::ProviderConflict);
  }
  if (best.reading.mockSignal == MockSignal::FlaggedByOs) {
    reasons.add(ReasonCode::PossibleMockLocation);
  }
  if (locationIsOnlyApproximate) {
    reasons.add(ReasonCode::PrecisionReduced);
  }
  if (best.gpsSpeedDoesNotMatchTheMovement) {
    reasons.add(ReasonCode::LocationMetadataInconsistent);
  }
  if (report.phoneActivity.hasGaps) {
    reasons.add(ReasonCode::MotionInsufficientCoverage);
  }
  if (best.timeInEachActivity.twoReportsDisagreed) {
    reasons.add(ReasonCode::MotionConflictingEvidence);
  }

  const bool onlyTheHistoryAndItCannotTell =
      (report.source == FixSource::Cached &&
       report.relation == EvidenceRelation::Inconclusive);
  if (onlyTheHistoryAndItCannotTell) {
    reasons.add(ReasonCode::HistoryOnlyInconclusive);
  }
}

}  // namespace

// =============================================================================
// FUNCTIONS OTHER FILES CAN CALL
//
// They are announced in Report.hpp.
// =============================================================================

const char* requestStepName(RequestStep step) noexcept {
  const std::size_t position = static_cast<std::size_t>(step);
  const bool isARealStep = (position < REQUEST_STEP_COUNT);
  return isARealStep ? STEP_NAMES[position] : nullptr;
}

Report buildReport(const LocationHistory& history,
                           const PhoneActivityHistory& activityHistory,
                           std::int64_t nowMs,
                           const WhatTheAppAskedFor& asked,
                           const WhatTheRequestKnows& request) noexcept {
  // The report starts as "nothing can be used": unavailable, no position,
  // score 0, inconclusive. The steps below improve it when they can.
  Report report;
  report.radiusMeters = asked.radiusMeters;
  report.permissionPrecision = request.permissionPrecision;
  report.readingsExamined = static_cast<std::uint32_t>(history.size());
  report.score = calculateQualityScore(ScoreInput{});

  // Without the location permission no reading may be used, not even one
  // that is still in memory.
  const bool permissionIsGranted = (request.locationPermission == LocationPermission::Granted);
  if (!permissionIsGranted) {
    report.reasons.add(reasonForMissingPermission(request.locationPermission));
    return report;
  }

  // --- Step 1: choose the best reading ---------------------------------------
  RulesForChoosingReading rules;
  rules.oldestAgeAllowedMs = asked.oldestAgeAllowedMs;
  rules.fastestPossibleSpeedMetersPerSecond = asked.fastestPossibleSpeedMetersPerSecond;
  rules.usePhoneActivity = asked.usePhoneActivity;
  if (!asked.allowAnswerFromHistory) {
    rules.firstArrivalNumberAllowed = request.firstArrivalNumberOfThisRequest;
  }

  const ChosenReading chosen = chooseBestReading(history, activityHistory, nowMs, rules);
  report.gpsJumpsFound = static_cast<std::uint32_t>(chosen.howManyAreProbablyGpsJumps);

  if (!chosen.found) {
    report.reasons.add(ReasonCode::NoUsableFix);
    if (history.empty()) {
      report.reasons.add(ReasonCode::HistoryEmpty);
    }
    if (chosen.howManyWereTooOld > 0) {
      report.reasons.add(ReasonCode::LocationStale);
    }
    report.phoneActivity = describePhoneActivity(activityHistory, nullptr, nowMs, asked);
    return report;
  }

  const WhatWeKnowAboutReading& best = chosen.best;

  report.hasPosition = true;
  report.position = best.reading.position;
  report.gpsErrorMeters = best.reading.horizontalAccuracyMeters;
  report.howFarPhoneCouldBeMeters = best.howFarPhoneCouldBeMeters;
  report.readingAgeMs = best.ageMs;
  report.mockSignal = best.reading.mockSignal;
  report.ageReliability = best.reading.captureTimeWasEstimated ? AgeReliability::Estimated
                                                                  : AgeReliability::Monotonic;

  // Things about the reading that several steps below need to know.
  const bool isProbablyAGpsJump = (best.jumpCheck.result == JumpCheckResult::ProbablyGpsJump);
  const bool someReadingContradictsIt = (best.jumpCheck.readingsAgainstIt > 0);
  const bool blockedForBeingSimulated = (best.reading.mockSignal == MockSignal::FlaggedByOs &&
                                         asked.mockPolicy == MockPolicy::ZeroScore);
  const bool locationIsOnlyApproximate =
      (best.reading.onlyApproximatePermission ||
       request.permissionPrecision == PermissionPrecision::Approximate);

  // --- Step 2: where did the reading come from? ------------------------------
  report.source = findWhereTheReadingCameFrom(best, asked, request);

  // --- Step 3: compare it with the delivery radius ---------------------------
  report.distanceToDestinationMeters =
      distanceBetweenMeters(best.reading.position, asked.destination);

  RelationGates gates;
  gates.hasCandidate = true;
  gates.ageReliable = true;          // a reading with a broken time never enters the history
  gates.withinFallbackAge = true;    // chooseBestReading() already skipped the old ones
  gates.permissionAuthorized = true;  // checked at the top of this function
  gates.strongJumpSuspicion = isProbablyAGpsJump;
  gates.blockedByMockPolicy = blockedForBeingSimulated;

  report.relation = classifyRelation(report.distanceToDestinationMeters,
                                         report.howFarPhoneCouldBeMeters,
                                         asked.radiusMeters, gates);

  // --- Step 4: the quality score ---------------------------------------------
  ScoreInput scoreInput;
  scoreInput.hasCandidate = true;
  scoreInput.ageReliable = true;
  scoreInput.horizontalAccuracyMeters = best.reading.horizontalAccuracyMeters;
  scoreInput.ageMs = static_cast<double>(best.ageMs);
  scoreInput.possibleMovementMeters = best.distanceMoved.meters;
  scoreInput.movementIsUnknown = best.distanceMoved.weOnlyAssumedTheWorst();
  scoreInput.consistency = consistencyFromAgreeingReadings(best.jumpCheck.readingsOnItsSide,
                                                           someReadingContradictsIt);
  scoreInput.source = report.source;
  scoreInput.onlyApproximatePermission = locationIsOnlyApproximate;
  scoreInput.suspectedJump = isProbablyAGpsJump;
  scoreInput.mockSignal = best.reading.mockSignal;
  scoreInput.mockPolicy = asked.mockPolicy;

  report.score = calculateQualityScore(scoreInput);

  // --- Step 5: available or degraded? ----------------------------------------
  report.status = decideTheStatus(report, asked, isProbablyAGpsJump,
                                      blockedForBeingSimulated, locationIsOnlyApproximate);

  // --- Step 6: the phone activity and the reasons ----------------------------
  report.phoneActivity = describePhoneActivity(activityHistory, &best, nowMs, asked);
  writeDownTheReasons(report, chosen, asked, locationIsOnlyApproximate);

  return report;
}

}  // namespace lce
