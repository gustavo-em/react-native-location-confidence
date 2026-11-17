// Tests of the final report (Report.cpp).
//
// In every test the address is the starting point (latitude 0, longitude 0)
// and the delivery radius is 100 meters, unless the test says otherwise.
// Readings are placed some meters north of the address.
#include "third_party/doctest.h"

#include <cstdint>
#include <string>

#include "Report.hpp"
#include "LocationHistory.hpp"
#include "PhoneActivityTimeline.hpp"
#include "TestSupport.hpp"

using namespace lce;
using lce_test::activityHistoryWith;
using lce_test::activityReport;
using lce_test::DeterministicRng;
using lce_test::historyWith;
using lce_test::phoneReported;
using lce_test::phoneReportedNothing;
using lce_test::readingNorth;
using lce_test::secondsToMs;

namespace {

// What the app asks for in most tests: the address at the starting point,
// 100 meters of radius, and the usual limits.
WhatTheAppAskedFor usualRequestFromTheApp() {
  WhatTheAppAskedFor asked;
  asked.destination = Coordinates{0.0, 0.0};
  asked.radiusMeters = 100.0;
  return asked;
}

// The request obtained every reading itself, and the permission is fine.
WhatTheRequestKnows requestThatObtainedEverything() {
  return WhatTheRequestKnows{};
}

// Every reading was already in the history when the request started.
WhatTheRequestKnows requestThatObtainedNothing(const LocationHistory& history) {
  WhatTheRequestKnows request;
  request.firstArrivalNumberOfThisRequest = history.arrivalNumberOfNextReading();
  return request;
}

Report reportAt(double secondsNow,
                    const LocationHistory& history,
                    const PhoneActivityHistory& phoneActivity = PhoneActivityHistory{},
                    const WhatTheAppAskedFor& asked = usualRequestFromTheApp(),
                    const WhatTheRequestKnows& request = requestThatObtainedEverything()) {
  return buildReport(history, phoneActivity, secondsToMs(secondsNow), asked, request);
}

bool hasReason(const Report& report, ReasonCode reason) {
  return report.reasons.contains(reason);
}

int howManyLimitsApplied(const Report& report) {
  int howMany = 0;
  for (const bool applied : report.score.limitWasApplied) {
    if (applied) {
      howMany += 1;
    }
  }
  return howMany;
}

}  // namespace

// --- The reference fixtures of the spec (section 12), with exact scores -----------

TEST_CASE("fixture 12.1 score_perfect_three_fixes: three perfect readings score 100") {
  const LocationHistory history = historyWith({
      readingNorth(0, 0, 0),
      readingNorth(1, 0, 0),
      readingNorth(2, 0, 0),
  });

  const Report report = reportAt(2, history);

  CHECK(report.status == ReportStatus::Available);
  CHECK(report.source == FixSource::Fresh);
  CHECK(report.score.accuracyFactor == 1.0);
  CHECK(report.score.freshnessFactor == 1.0);
  CHECK(report.score.movementFactor == 1.0);
  CHECK(report.score.consistencyFactor == 1.0);
  CHECK(report.score.rawScore == 100);
  CHECK(report.score.qualityScore == 100);
  CHECK(howManyLimitsApplied(report) == 0);
  CHECK(report.relation == EvidenceRelation::SupportsInside);
}

TEST_CASE("fixture 12.2 score_single_fix_default: one good reading scores 75") {
  // GPS error 10, 2 seconds old, the phone stood still: 0.5 x 2 = 1 meter.
  const LocationHistory history = historyWith({readingNorth(0, 30, 10)});

  const Report report =
      reportAt(2, history, phoneReported(PhoneActivity::StandingStill, 0, 2));

  REQUIRE(report.hasPosition);
  CHECK(report.readingAgeMs == 2000);
  CHECK(report.gpsErrorMeters == 10.0);
  CHECK(report.howFarPhoneCouldBeMeters == doctest::Approx(11.0));
  CHECK(report.distanceToDestinationMeters == doctest::Approx(30.0));

  CHECK(report.score.accuracyFactor == doctest::Approx(0.90));
  CHECK(report.score.freshnessFactor == doctest::Approx(0.96666667));
  CHECK(report.score.movementFactor == doctest::Approx(0.99));
  CHECK(report.score.consistencyFactor == doctest::Approx(0.60));
  CHECK(report.score.rawScore == 89);
  CHECK(report.score.qualityScore == 75);
  CHECK(report.score.wasLimitedBy(ScoreLimit::SingleFix));
  CHECK(howManyLimitsApplied(report) == 1);

  CHECK(report.status == ReportStatus::Available);
  CHECK(report.source == FixSource::Fresh);
  CHECK(report.relation == EvidenceRelation::SupportsInside);      // 30 + 11 = 41
  CHECK(report.ageReliability == AgeReliability::Monotonic);

  CHECK(report.phoneActivity.activityNow == PhoneActivity::StandingStill);
  CHECK(report.phoneActivity.confidenceNow == PhoneActivityConfidence::High);
  CHECK(report.phoneActivity.standingStillSinceTheReading);
  CHECK(report.phoneActivity.knownUntilNow);
  CHECK(report.phoneActivity.knownSinceAgeMs == 2000);
  CHECK_FALSE(report.phoneActivity.hasGaps);

  // The only reason: the distances come from speeds that are guesses.
  CHECK(hasReason(report, ReasonCode::UncertaintyModelHeuristic));
  CHECK_FALSE(hasReason(report, ReasonCode::LocationStale));
  CHECK_FALSE(hasReason(report, ReasonCode::SuspectedGpsJump));
  CHECK_FALSE(hasReason(report, ReasonCode::MotionInsufficientCoverage));
}

TEST_CASE("fixture 12.3 score_cached_vehicle: an old reading in a vehicle scores 15") {
  // Three readings that agree; the newest is 10 seconds old at second 20.
  const LocationHistory history = historyWith({
      readingNorth(0, 30, 10),
      readingNorth(5, 30, 10),
      readingNorth(10, 30, 10),
  });

  const Report report =
      reportAt(20, history, phoneReported(PhoneActivity::InVehicle, 0, 20),
               usualRequestFromTheApp(), requestThatObtainedNothing(history));

  REQUIRE(report.hasPosition);
  CHECK(report.readingAgeMs == 10000);
  CHECK(report.howFarPhoneCouldBeMeters == doctest::Approx(460.0));      // 10 + 45 x 10

  CHECK(report.score.accuracyFactor == doctest::Approx(0.90));
  CHECK(report.score.freshnessFactor == doctest::Approx(0.83333333));
  CHECK(report.score.movementFactor == 0.0);
  CHECK(report.score.consistencyFactor == 1.0);
  CHECK(report.score.rawScore == 72);
  CHECK(report.score.qualityScore == 15);
  CHECK(report.score.wasLimitedBy(ScoreLimit::Cached));
  CHECK(report.score.wasLimitedBy(ScoreLimit::UncertaintyAtLeast100));
  CHECK(report.score.wasLimitedBy(ScoreLimit::UncertaintyAtLeast300));
  CHECK(howManyLimitsApplied(report) == 3);

  CHECK(report.source == FixSource::Cached);
  CHECK(report.status == ReportStatus::Degraded);
  CHECK(report.relation == EvidenceRelation::Inconclusive);
  CHECK(hasReason(report, ReasonCode::HistoryOnlyInconclusive));
}

TEST_CASE("fixture 12.4 far_outside: a far reading supports 'outside'") {
  // 500 meters away, and the phone could be 20 meters from the reading.
  const Report report = reportAt(0, historyWith({readingNorth(0, 500, 20)}));

  CHECK(report.distanceToDestinationMeters == doctest::Approx(500.0));
  CHECK(report.howFarPhoneCouldBeMeters == 20.0);
  CHECK(report.relation == EvidenceRelation::SupportsOutside);      // 500 - 20 > 100
}

TEST_CASE("fixture 12.5 aged_stationary_gap: 2 seconds of standing still do not cover 20") {
  const LocationHistory history = historyWith({readingNorth(0, 30, 10)});

  const Report report =
      reportAt(20, history, phoneReported(PhoneActivity::StandingStill, 18, 20));

  // 18 unknown seconds and 2 standing still: 45 x 18 + 0.5 x 2 = 811 meters.
  CHECK(report.howFarPhoneCouldBeMeters == doctest::Approx(821.0));
  CHECK_FALSE(report.phoneActivity.standingStillSinceTheReading);
  CHECK(report.phoneActivity.hasGaps);
  CHECK(report.phoneActivity.knownUntilNow);
  CHECK(report.phoneActivity.knownSinceAgeMs == 2000);
  CHECK(report.phoneActivity.activityNow == PhoneActivity::StandingStill);
  CHECK(hasReason(report, ReasonCode::MotionInsufficientCoverage));
}

TEST_CASE("fixture 12.6 recovery_aba: the reading that jumped is not the answer") {
  const LocationHistory history = historyWith({
      readingNorth(0, 0, 5),    // A
      readingNorth(2, 500, 5),  // B: 500 meters away
      readingNorth(4, 0, 5),    // C: back at A
  });

  const Report report = reportAt(4, history);

  REQUIRE(report.hasPosition);
  CHECK(report.position.latitudeDegrees == 0.0);      // C, not B
  CHECK(report.readingAgeMs == 0);
  CHECK(report.gpsJumpsFound == 1);
  CHECK(report.readingsExamined == 3);
  CHECK(hasReason(report, ReasonCode::SuspectedGpsJump));

  // One reading contradicts C, so "the readings disagree": at most 75.
  CHECK(report.score.consistencyFactor == doctest::Approx(0.30));
  CHECK(report.score.rawScore == 88);
  CHECK(report.score.qualityScore == 75);
  CHECK(report.score.wasLimitedBy(ScoreLimit::ConflictingEvidence));
  CHECK_FALSE(report.score.wasLimitedBy(ScoreLimit::SuspectedJump));
  CHECK(report.status == ReportStatus::Available);
  CHECK(report.relation == EvidenceRelation::SupportsInside);
}

// --- Where the reading came from (source) -------------------------------------------

TEST_CASE("VAL-025 the source says where the reading really came from") {
  const LocationHistory history = historyWith({readingNorth(0, 0, 5)});
  const WhatTheAppAskedFor asked = usualRequestFromTheApp();
  const PhoneActivityHistory nothingReported;

  SUBCASE("this request obtained it in the first attempt: fresh") {
    const Report report =
        buildReport(history, nothingReported, 1000, asked, requestThatObtainedEverything());
    CHECK(report.source == FixSource::Fresh);
  }

  SUBCASE("this request obtained it during the extra time: recovered") {
    WhatTheRequestKnows request;
    request.firstArrivalNumberOfThisRequest = 1;
    request.firstArrivalNumberOfExtraTime = 1;
    const Report report = buildReport(history, nothingReported, 1000, asked, request);
    CHECK(report.source == FixSource::Recovered);
    CHECK(report.status == ReportStatus::Available);
  }

  SUBCASE("it was already in the history: cached") {
    const Report report = buildReport(history, nothingReported, 1000, asked,
                                                  requestThatObtainedNothing(history));
    CHECK(report.source == FixSource::Cached);
    CHECK(report.status == ReportStatus::Degraded);
    CHECK(report.score.wasLimitedBy(ScoreLimit::Cached));
  }

  SUBCASE("this request obtained it, but it is exactly 10 seconds old: still fresh") {
    const Report report =
        buildReport(history, nothingReported, 10000, asked, requestThatObtainedEverything());
    CHECK(report.source == FixSource::Fresh);
    CHECK_FALSE(hasReason(report, ReasonCode::LocationStale));
  }

  SUBCASE("this request obtained it, but the system handed us an old one: cached") {
    const Report report =
        buildReport(history, nothingReported, 10001, asked, requestThatObtainedEverything());
    CHECK(report.source == FixSource::Cached);
    CHECK(report.readingAgeMs == 10001);
    CHECK(hasReason(report, ReasonCode::LocationStale));
  }
}

TEST_CASE("a reading from the first attempt stays 'fresh' after the extra time starts") {
  const LocationHistory history = historyWith({
      readingNorth(0, 0, 5),   // arrival number 1: first attempt
      readingNorth(1, 0, 90),  // arrival number 2: extra time, and worse
  });

  WhatTheRequestKnows request;
  request.firstArrivalNumberOfThisRequest = 1;
  request.firstArrivalNumberOfExtraTime = 2;

  // At second 1 the first reading is the better one: 5 + 45 = 50 against 90.
  const Report report =
      reportAt(1, history, phoneReportedNothing(), usualRequestFromTheApp(), request);

  CHECK(report.gpsErrorMeters == 5.0);
  CHECK(report.source == FixSource::Fresh);
}

// --- The status ---------------------------------------------------------------------------

TEST_CASE("VAL-018 an imprecise reading is only 'degraded', whatever the permission") {
  const Report report = reportAt(1, historyWith({readingNorth(0, 0, 500)}));

  CHECK(report.status == ReportStatus::Degraded);
  CHECK(report.source == FixSource::Fresh);
  CHECK(hasReason(report, ReasonCode::LocationLowAccuracy));
  CHECK(report.permissionPrecision == PermissionPrecision::Precise);
}

TEST_CASE("a reading exactly as imprecise as the app accepts is still 'available'") {
  CHECK(reportAt(0, historyWith({readingNorth(0, 0, 50)})).status == ReportStatus::Available);
  CHECK(reportAt(0, historyWith({readingNorth(0, 0, 50.5)})).status ==
        ReportStatus::Degraded);
}

TEST_CASE("VAL-019 SCR-011 an approximate location is degraded and scores at most 30") {
  SUBCASE("the reading itself was taken with approximate permission") {
    LocationReading blurred = readingNorth(0, 0, 3000);
    blurred.onlyApproximatePermission = true;

    const Report report = reportAt(0, historyWith({blurred}));

    CHECK(report.status == ReportStatus::Degraded);
    CHECK(report.relation == EvidenceRelation::Inconclusive);
    CHECK(report.score.wasLimitedBy(ScoreLimit::ApproximatePermission));
    CHECK(report.score.qualityScore <= 30);
    CHECK(hasReason(report, ReasonCode::PrecisionReduced));
    CHECK(hasReason(report, ReasonCode::LocationLowAccuracy));
  }

  SUBCASE("the permission is approximate now, even if the number looks precise") {
    WhatTheRequestKnows request;
    request.permissionPrecision = PermissionPrecision::Approximate;

    const Report report = reportAt(0, historyWith({readingNorth(0, 0, 5)}),
                                           phoneReportedNothing(), usualRequestFromTheApp(),
                                           request);

    CHECK(report.status == ReportStatus::Degraded);
    CHECK(report.score.rawScore == 92);      // 38 + 25 + 20 + 9
    CHECK(report.score.qualityScore == 30);
    CHECK(report.permissionPrecision == PermissionPrecision::Approximate);
    CHECK(hasReason(report, ReasonCode::PrecisionReduced));
  }
}

TEST_CASE("VAL-023 SCR-016 a reading marked as simulated") {
  LocationReading simulated = readingNorth(0, 0, 5);
  simulated.mockSignal = MockSignal::FlaggedByOs;
  const LocationHistory history = historyWith({simulated});

  SUBCASE("the app wants score 0 for simulated readings") {
    const Report report = reportAt(0, history);

    CHECK(report.hasPosition);
    CHECK(report.mockSignal == MockSignal::FlaggedByOs);
    CHECK(report.score.rawScore == 92);
    CHECK(report.score.qualityScore == 0);
    CHECK(report.score.wasLimitedBy(ScoreLimit::MockLocation));
    CHECK(report.relation == EvidenceRelation::Inconclusive);
    CHECK(report.status == ReportStatus::Degraded);
    CHECK(hasReason(report, ReasonCode::PossibleMockLocation));
  }

  SUBCASE("the app only wants to be told") {
    WhatTheAppAskedFor asked = usualRequestFromTheApp();
    asked.mockPolicy = MockPolicy::FlagOnly;

    const Report report = reportAt(0, history, phoneReportedNothing(), asked);

    CHECK(report.score.qualityScore == 75);      // only the single reading limit
    CHECK_FALSE(report.score.wasLimitedBy(ScoreLimit::MockLocation));
    CHECK(report.relation == EvidenceRelation::SupportsInside);
    CHECK(report.status == ReportStatus::Available);
    CHECK(hasReason(report, ReasonCode::PossibleMockLocation));
  }
}

TEST_CASE("VAL-024 SCR-030 no simulation flag is not a guarantee of anything") {
  LocationReading notFlagged = readingNorth(0, 0, 5);
  notFlagged.mockSignal = MockSignal::NotFlaggedByOs;
  LocationReading unknown = readingNorth(0, 0, 5);
  unknown.mockSignal = MockSignal::Unknown;

  const Report fromNotFlagged = reportAt(0, historyWith({notFlagged}));
  const Report fromUnknown = reportAt(0, historyWith({unknown}));

  // The report repeats what the system said and nothing more.
  CHECK(fromNotFlagged.mockSignal == MockSignal::NotFlaggedByOs);
  CHECK(fromUnknown.mockSignal == MockSignal::Unknown);
  CHECK_FALSE(hasReason(fromNotFlagged, ReasonCode::PossibleMockLocation));
  CHECK_FALSE(hasReason(fromUnknown, ReasonCode::PossibleMockLocation));

  // And the score is the same: "not flagged" earns nothing.
  CHECK(fromNotFlagged.score.qualityScore == fromUnknown.score.qualityScore);
}

TEST_CASE("when only a probable GPS jump can be used, the answer is degraded") {
  const LocationHistory history = historyWith({
      readingNorth(0, 0),
      readingNorth(1, 0),
      readingNorth(70, 5000),
  });

  // At second 75 the two good readings are too old.
  const Report report = reportAt(75, history);

  REQUIRE(report.hasPosition);
  CHECK(report.status == ReportStatus::Degraded);
  CHECK(report.relation == EvidenceRelation::Inconclusive);      // a jump never takes a side
  CHECK(report.score.wasLimitedBy(ScoreLimit::SuspectedJump));
  CHECK(report.score.qualityScore <= 20);
  CHECK(hasReason(report, ReasonCode::SuspectedGpsJump));
}

// --- Nothing can be used ---------------------------------------------------------------------

TEST_CASE("SCR-001 an empty history gives 'unavailable' and score 0") {
  const Report report = reportAt(10, LocationHistory{});

  CHECK(report.status == ReportStatus::Unavailable);
  CHECK(report.source == FixSource::Unavailable);
  CHECK_FALSE(report.hasPosition);
  CHECK(report.score.qualityScore == 0);
  CHECK_FALSE(report.score.hasBreakdown);
  CHECK(report.score.wasLimitedBy(ScoreLimit::NoCandidate));
  CHECK(report.relation == EvidenceRelation::Inconclusive);
  CHECK(report.ageReliability == AgeReliability::NotApplicable);
  CHECK(report.radiusMeters == 100.0);
  CHECK(hasReason(report, ReasonCode::NoUsableFix));
  CHECK(hasReason(report, ReasonCode::HistoryEmpty));
  CHECK_FALSE(hasReason(report, ReasonCode::LocationStale));
}

TEST_CASE("HIS-020 SCR-025 readings that are too old give 'unavailable'") {
  // Exactly at the address: the numbers alone would say "inside".
  const LocationHistory history = historyWith({readingNorth(0, 0, 1)});

  const Report report =
      reportAt(61, history, phoneReported(PhoneActivity::StandingStill, 0, 61));

  CHECK(report.status == ReportStatus::Unavailable);
  CHECK_FALSE(report.hasPosition);
  CHECK(report.relation == EvidenceRelation::Inconclusive);
  CHECK(report.score.qualityScore == 0);
  CHECK(report.readingsExamined == 1);
  CHECK(hasReason(report, ReasonCode::NoUsableFix));
  CHECK(hasReason(report, ReasonCode::LocationStale));
  CHECK_FALSE(hasReason(report, ReasonCode::HistoryEmpty));

  // What the phone is doing right now can still be told.
  CHECK(report.phoneActivity.activityNow == PhoneActivity::StandingStill);
  CHECK_FALSE(report.phoneActivity.standingStillSinceTheReading);
}

TEST_CASE("without the location permission no reading is used") {
  const LocationHistory history = historyWith({readingNorth(0, 0, 5)});

  WhatTheRequestKnows request;

  request.locationPermission = LocationPermission::Denied;
  const Report denied =
      reportAt(1, history, phoneReportedNothing(), usualRequestFromTheApp(), request);
  CHECK(denied.status == ReportStatus::Unavailable);
  CHECK_FALSE(denied.hasPosition);
  CHECK(denied.score.qualityScore == 0);
  CHECK(denied.relation == EvidenceRelation::Inconclusive);
  CHECK(hasReason(denied, ReasonCode::PermissionDenied));
  CHECK_FALSE(hasReason(denied, ReasonCode::NoUsableFix));

  request.locationPermission = LocationPermission::NotDetermined;
  const Report neverAsked =
      reportAt(1, history, phoneReportedNothing(), usualRequestFromTheApp(), request);
  CHECK_FALSE(neverAsked.hasPosition);
  CHECK(hasReason(neverAsked, ReasonCode::PermissionNotDetermined));

  request.locationPermission = LocationPermission::Restricted;
  const Report restricted =
      reportAt(1, history, phoneReportedNothing(), usualRequestFromTheApp(), request);
  CHECK_FALSE(restricted.hasPosition);
  CHECK(hasReason(restricted, ReasonCode::PermissionRestricted));
}

TEST_CASE("when the app refuses the history, only readings of this request are used") {
  const LocationHistory history = historyWith({readingNorth(0, 0, 5)});

  WhatTheAppAskedFor asked = usualRequestFromTheApp();
  asked.allowAnswerFromHistory = false;

  // The reading was in the history before the request: it is not used.
  const Report refused = reportAt(1, history, phoneReportedNothing(), asked,
                                      requestThatObtainedNothing(history));
  CHECK(refused.status == ReportStatus::Unavailable);
  CHECK(hasReason(refused, ReasonCode::NoUsableFix));

  // The request obtained it: it is used.
  const Report accepted = reportAt(1, history, phoneReportedNothing(), asked,
                                       requestThatObtainedEverything());
  CHECK(accepted.status == ReportStatus::Available);
}

TEST_CASE("the limits chosen by the app are the ones used") {
  const LocationHistory history = historyWith({readingNorth(0, 0, 5)});

  SUBCASE("a slower fastest speed") {
    WhatTheAppAskedFor onFoot = usualRequestFromTheApp();
    onFoot.fastestPossibleSpeedMetersPerSecond = 5.0;

    const Report report = reportAt(10, history, phoneReportedNothing(), onFoot);
    CHECK(report.howFarPhoneCouldBeMeters == doctest::Approx(55.0));      // 5 + 5 x 10
  }

  SUBCASE("a shorter oldest age") {
    WhatTheAppAskedFor strict = usualRequestFromTheApp();
    strict.oldestAgeAllowedMs = 20000;

    CHECK(reportAt(20, history, phoneReportedNothing(), strict).hasPosition);
    CHECK_FALSE(reportAt(21, history, phoneReportedNothing(), strict).hasPosition);
  }

  SUBCASE("a longer oldest age") {
    WhatTheAppAskedFor patient = usualRequestFromTheApp();
    patient.oldestAgeAllowedMs = 90000;

    CHECK(reportAt(70, history, phoneReportedNothing(), patient).hasPosition);
    CHECK_FALSE(reportAt(70, history).hasPosition);  // the usual limit is 60 seconds
  }
}

// --- The reasons ------------------------------------------------------------------------------

TEST_CASE("JMP-019 a contradiction between providers is reported") {
  LocationReading fromTheSystemCache = readingNorth(0, 0);
  fromTheSystemCache.provider = Provider::SystemCache;
  const LocationReading fromTheGps = readingNorth(2, 450);

  const Report report = reportAt(3, historyWith({fromTheSystemCache, fromTheGps}));

  CHECK(hasReason(report, ReasonCode::ProviderConflict));
  CHECK(hasReason(report, ReasonCode::SuspectedGpsJump));
  CHECK(report.score.wasLimitedBy(ScoreLimit::ConflictingEvidence));
}

TEST_CASE("JMP-018 a speed that does not fit the movement is reported") {
  LocationReading fastButNotMoving = readingNorth(2, 0);
  fastButNotMoving.speedMetersPerSecond = 30.0;
  fastButNotMoving.speedAccuracyMetersPerSecond = 1.0;

  const Report report = reportAt(3, historyWith({readingNorth(0, 0), fastButNotMoving}));

  CHECK(hasReason(report, ReasonCode::LocationMetadataInconsistent));
  CHECK(report.howFarPhoneCouldBeMeters == doctest::Approx(50.0));      // 5 + 45 x 1
}

TEST_CASE("MOT-014 two reports that disagree are reported, and nobody is believed") {
  const LocationHistory history = historyWith({readingNorth(0, 0, 5)});
  const PhoneActivityHistory phoneActivity = activityHistoryWith({
      activityReport(PhoneActivity::StandingStill, 0, 10),
      activityReport(PhoneActivity::InVehicle, 0, 10),
  });

  const Report report = reportAt(10, history, phoneActivity);

  CHECK(report.howFarPhoneCouldBeMeters == doctest::Approx(455.0));      // 5 + 45 x 10
  CHECK(hasReason(report, ReasonCode::MotionConflictingEvidence));
  CHECK(report.phoneActivity.activityNow == PhoneActivity::Unknown);
  CHECK(report.phoneActivity.confidenceNow == PhoneActivityConfidence::Unknown);
  CHECK_FALSE(report.phoneActivity.standingStillSinceTheReading);
  CHECK_FALSE(report.phoneActivity.knownUntilNow);
}

// --- The phone activity in the report --------------------------------------------------------

TEST_CASE("the report describes the phone activity without changing the distance") {
  const LocationHistory history = historyWith({readingNorth(0, 0, 5)});

  SUBCASE("nothing reported") {
    const Report report = reportAt(10, history);
    CHECK(report.phoneActivity.activityNow == PhoneActivity::Unknown);
    CHECK(report.phoneActivity.confidenceNow == PhoneActivityConfidence::Unknown);
    CHECK_FALSE(report.phoneActivity.knownUntilNow);
    CHECK_FALSE(report.phoneActivity.hasGaps);
    CHECK_FALSE(hasReason(report, ReasonCode::MotionInsufficientCoverage));
  }

  SUBCASE("MOT-022 a report the phone is not very sure about is shown, not used") {
    const PhoneActivityHistory notVerySure = activityHistoryWith({
        activityReport(PhoneActivity::StandingStill, 0, 10, PhoneActivityConfidence::Medium),
    });

    const Report report = reportAt(10, history, notVerySure);

    CHECK(report.phoneActivity.activityNow == PhoneActivity::StandingStill);
    CHECK(report.phoneActivity.confidenceNow == PhoneActivityConfidence::Medium);
    CHECK_FALSE(report.phoneActivity.standingStillSinceTheReading);
    CHECK_FALSE(report.phoneActivity.knownUntilNow);
    CHECK(report.howFarPhoneCouldBeMeters == doctest::Approx(455.0));      // still 45 x 10
  }

  SUBCASE("walking since before the reading") {
    const Report report =
        reportAt(10, history, phoneReported(PhoneActivity::Walking, 0, 10));

    CHECK(report.phoneActivity.activityNow == PhoneActivity::Walking);
    CHECK(report.phoneActivity.confidenceNow == PhoneActivityConfidence::High);
    CHECK_FALSE(report.phoneActivity.standingStillSinceTheReading);
    CHECK(report.phoneActivity.knownUntilNow);
    CHECK(report.phoneActivity.knownSinceAgeMs == 10000);
    CHECK(report.howFarPhoneCouldBeMeters == doctest::Approx(35.0));      // 5 + 3 x 10
  }

  SUBCASE("a hole in the middle: known only since the last report started") {
    const PhoneActivityHistory withAHole = activityHistoryWith({
        activityReport(PhoneActivity::StandingStill, 0, 4),
        activityReport(PhoneActivity::Walking, 6, 10),
    });

    const Report report = reportAt(10, history, withAHole);

    CHECK(report.phoneActivity.knownUntilNow);
    CHECK(report.phoneActivity.knownSinceAgeMs == 4000);
    CHECK(report.phoneActivity.hasGaps);
    CHECK(hasReason(report, ReasonCode::MotionInsufficientCoverage));
  }

  SUBCASE("two reports with no hole between them count as one stretch") {
    const PhoneActivityHistory oneAfterTheOther = activityHistoryWith({
        activityReport(PhoneActivity::StandingStill, 0, 4),
        activityReport(PhoneActivity::Walking, 4, 10),
    });

    const Report report = reportAt(10, history, oneAfterTheOther);

    CHECK(report.phoneActivity.knownUntilNow);
    CHECK(report.phoneActivity.knownSinceAgeMs == 10000);
    CHECK_FALSE(report.phoneActivity.hasGaps);
    CHECK_FALSE(report.phoneActivity.standingStillSinceTheReading);
    CHECK(report.phoneActivity.activityNow == PhoneActivity::Walking);
  }

  SUBCASE("the report the phone is most sure about is the one shown") {
    const PhoneActivityReport notSure =
        activityReport(PhoneActivity::StandingStill, 0, 10, PhoneActivityConfidence::Low);
    const PhoneActivityReport moreSure =
        activityReport(PhoneActivity::Walking, 0, 10, PhoneActivityConfidence::Medium);

    const Report oneOrder = reportAt(10, history, activityHistoryWith({notSure, moreSure}));
    CHECK(oneOrder.phoneActivity.activityNow == PhoneActivity::Walking);
    CHECK(oneOrder.phoneActivity.confidenceNow == PhoneActivityConfidence::Medium);

    const Report otherOrder = reportAt(10, history, activityHistoryWith({moreSure, notSure}));
    CHECK(otherOrder.phoneActivity.activityNow == PhoneActivity::Walking);
    CHECK(otherOrder.phoneActivity.confidenceNow == PhoneActivityConfidence::Medium);
  }

  SUBCASE("a report that came with no confidence is shown as it is") {
    const PhoneActivityHistory noConfidence = activityHistoryWith({
        activityReport(PhoneActivity::Cycling, 0, 10, PhoneActivityConfidence::Unknown),
    });

    const Report report = reportAt(10, history, noConfidence);

    CHECK(report.phoneActivity.activityNow == PhoneActivity::Cycling);
    CHECK(report.phoneActivity.confidenceNow == PhoneActivityConfidence::Unknown);
  }

  SUBCASE("a report that ended before now says nothing about now") {
    const Report report =
        reportAt(10, history, phoneReported(PhoneActivity::Walking, 0, 9));

    CHECK(report.phoneActivity.activityNow == PhoneActivity::Unknown);
    CHECK_FALSE(report.phoneActivity.knownUntilNow);
    CHECK(report.phoneActivity.hasGaps);
  }

  SUBCASE("a report that has not started yet says nothing about now") {
    const Report report =
        reportAt(10, history, phoneReported(PhoneActivity::Walking, 20, 30));

    CHECK(report.phoneActivity.activityNow == PhoneActivity::Unknown);
    CHECK_FALSE(report.phoneActivity.knownUntilNow);
  }

  SUBCASE("the app does not want the phone activity") {
    WhatTheAppAskedFor asked = usualRequestFromTheApp();
    asked.usePhoneActivity = false;

    const Report report =
        reportAt(10, history, phoneReported(PhoneActivity::StandingStill, 0, 10), asked);

    CHECK(report.phoneActivity.activityNow == PhoneActivity::Unknown);
    CHECK_FALSE(report.phoneActivity.standingStillSinceTheReading);
    CHECK_FALSE(report.phoneActivity.knownUntilNow);
    CHECK(report.howFarPhoneCouldBeMeters == doctest::Approx(455.0));
  }
}

TEST_CASE("the report says when the age of the reading is only an estimate") {
  LocationReading fromWallClock = readingNorth(0, 0, 5);
  fromWallClock.captureTimeWasEstimated = true;

  CHECK(reportAt(1, historyWith({fromWallClock})).ageReliability == AgeReliability::Estimated);
  CHECK(reportAt(1, historyWith({readingNorth(0, 0, 5)})).ageReliability ==
        AgeReliability::Monotonic);
}

// --- Inside, outside, or we cannot tell --------------------------------------------------------

TEST_CASE("SCR-024 a high score does not force an answer about the radius") {
  // 99 meters away and the phone could be 5 meters from the reading:
  // anywhere from 94 to 104 meters.
  const Report report = reportAt(0, historyWith({readingNorth(0, 99, 5)}));

  CHECK(report.score.qualityScore == 75);
  CHECK(report.status == ReportStatus::Available);
  CHECK(report.relation == EvidenceRelation::Inconclusive);
}

TEST_CASE("SCR-026 a tiny radius is not 'inside' just because the reading is on the address") {
  WhatTheAppAskedFor asked = usualRequestFromTheApp();
  asked.radiusMeters = 1.0;

  const Report report =
      reportAt(0, historyWith({readingNorth(0, 0, 10)}), phoneReportedNothing(), asked);

  CHECK(report.distanceToDestinationMeters == 0.0);
  CHECK(report.relation == EvidenceRelation::Inconclusive);      // 0 + 10 is more than 1
}

TEST_CASE("SCR-027 a wide radius is judged by the same rule") {
  WhatTheAppAskedFor asked = usualRequestFromTheApp();
  asked.radiusMeters = 100000.0;

  const Report report =
      reportAt(1, historyWith({readingNorth(0, 5000, 5)}), phoneReportedNothing(), asked);

  CHECK(report.relation == EvidenceRelation::SupportsInside);      // 5000 + 50 <= 100000
  CHECK(report.radiusMeters == 100000.0);
}

TEST_CASE("SCR-020 the score does not depend on where the address is") {
  const LocationHistory history = historyWith({
      readingNorth(0, 0, 8),
      readingNorth(1, 0, 8),
  });

  WhatTheAppAskedFor nearAddress = usualRequestFromTheApp();
  WhatTheAppAskedFor farAddress = usualRequestFromTheApp();
  farAddress.destination = Coordinates{0.0, 0.1};  // about 11 km east

  const Report near = reportAt(2, history, phoneReportedNothing(), nearAddress);
  const Report far = reportAt(2, history, phoneReportedNothing(), farAddress);

  CHECK(near.score.qualityScore == far.score.qualityScore);
  CHECK(near.score.rawScore == far.score.rawScore);
  CHECK(near.howFarPhoneCouldBeMeters == far.howFarPhoneCouldBeMeters);
  CHECK(near.status == far.status);

  // Only the comparison with the radius changes.
  CHECK(near.relation == EvidenceRelation::SupportsInside);
  CHECK(far.relation == EvidenceRelation::SupportsOutside);
}

// --- More evidence, less evidence ----------------------------------------------------------------

TEST_CASE("SCR-009 HIS-024 the same reading delivered again does not raise the score") {
  const LocationReading reading = readingNorth(0, 0, 5);

  LocationHistory deliveredOnce;
  deliveredOnce.store(reading, 0);

  LocationHistory deliveredThreeTimes;
  deliveredThreeTimes.store(reading, 0);
  deliveredThreeTimes.store(reading, 0);
  deliveredThreeTimes.store(reading, 0);

  const Report once = reportAt(1, deliveredOnce);
  const Report threeTimes = reportAt(1, deliveredThreeTimes);

  CHECK(threeTimes.score.qualityScore == once.score.qualityScore);
  CHECK(threeTimes.score.consistencyFactor == doctest::Approx(0.60));
  CHECK(threeTimes.score.wasLimitedBy(ScoreLimit::SingleFix));
  CHECK(threeTimes.readingsExamined == 1);
}

TEST_CASE("HIS-022 HIS-023 readings that agree raise the consistency") {
  const Report one = reportAt(0, historyWith({readingNorth(0, 0, 5)}));
  const Report two = reportAt(1, historyWith({readingNorth(0, 0, 5), readingNorth(1, 0, 5)}));
  const Report three = reportAt(
      2, historyWith({readingNorth(0, 0, 5), readingNorth(1, 0, 5), readingNorth(2, 0, 5)}));

  CHECK(one.score.consistencyFactor == doctest::Approx(0.60));
  CHECK(two.score.consistencyFactor == doctest::Approx(0.80));
  CHECK(three.score.consistencyFactor == doctest::Approx(1.00));
  CHECK(one.score.wasLimitedBy(ScoreLimit::SingleFix));
  CHECK_FALSE(two.score.wasLimitedBy(ScoreLimit::SingleFix));
}

TEST_CASE("SCR-010 JMP-021 a GPS jump added to a good series does not raise the score") {
  const LocationHistory goodSeries = historyWith({
      readingNorth(0, 0),
      readingNorth(1, 0),
      readingNorth(2, 0),
  });
  const LocationHistory withAJump = historyWith({
      readingNorth(0, 0),
      readingNorth(1, 0),
      readingNorth(1.5, 1000),  // 1 km away
      readingNorth(2, 0),
  });

  const Report before = reportAt(2, goodSeries);
  const Report after = reportAt(2, withAJump);

  CHECK(before.score.qualityScore == 98);  // 38 + 25 + 20 + 15
  CHECK(after.score.qualityScore == 75);
  CHECK(after.score.consistencyFactor == doctest::Approx(0.30));

  // The same reading is still the answer.
  CHECK(after.position.latitudeDegrees == before.position.latitudeDegrees);
  CHECK(after.readingAgeMs == before.readingAgeMs);
  CHECK(after.gpsJumpsFound == 1);
}

TEST_CASE("SCR-010 a reading that contradicts the answer never raises its score") {
  DeterministicRng rng(1010);
  int roundsThatKeptTheSameReading = 0;

  for (int round = 0; round < 2000; ++round) {
    // A few readings near the address, taken during 20 seconds.
    LocationHistory history;
    const std::size_t howMany = 1 + static_cast<std::size_t>(rng.below(5));
    for (std::size_t count = 0; count < howMany; ++count) {
      history.store(readingNorth(rng.uniform(0.0, 20.0), rng.uniform(0.0, 20.0),
                                 rng.uniform(1.0, 40.0)),
                    secondsToMs(20));
    }

    const double secondsNow = rng.uniform(20.0, 30.0);
    const Report before = reportAt(secondsNow, history);
    REQUIRE(before.hasPosition);

    // One more reading, far from everything.
    LocationHistory withOneMore = history;
    withOneMore.store(readingNorth(rng.uniform(0.0, 20.0), rng.uniform(5000.0, 9000.0),
                                   rng.uniform(1.0, 40.0)),
                      secondsToMs(20));
    const Report after = reportAt(secondsNow, withOneMore);
    REQUIRE(after.hasPosition);

    const bool sameReadingIsTheAnswer =
        (after.position.latitudeDegrees == before.position.latitudeDegrees &&
         after.readingAgeMs == before.readingAgeMs &&
         after.gpsErrorMeters == before.gpsErrorMeters);
    if (sameReadingIsTheAnswer) {
      roundsThatKeptTheSameReading += 1;
      REQUIRE(after.score.qualityScore <= before.score.qualityScore);
      REQUIRE(after.score.consistencyFactor <= before.score.consistencyFactor);
    }
  }

  // The property must really have been exercised.
  CHECK(roundsThatKeptTheSameReading > 1000);
}

TEST_CASE("INV-08 as time passes, the score of the same reading never rises") {
  DeterministicRng rng(808);
  int roundsThatKeptTheSameReading = 0;

  for (int round = 0; round < 2000; ++round) {
    LocationHistory history;
    const std::size_t howMany = 1 + static_cast<std::size_t>(rng.below(4));
    for (std::size_t count = 0; count < howMany; ++count) {
      history.store(readingNorth(rng.uniform(0.0, 20.0), rng.uniform(0.0, 300.0),
                                 rng.uniform(1.0, 80.0)),
                    secondsToMs(20));
    }

    PhoneActivityHistory phoneActivity;
    if (rng.below(2) == 0) {
      const double from = rng.uniform(0.0, 40.0);
      phoneActivity.store(activityReport(static_cast<PhoneActivity>(rng.below(5)), from,
                                         from + rng.uniform(0.0, 40.0)));
    }

    const double secondsNow = rng.uniform(20.0, 50.0);
    const double secondsLater = secondsNow + rng.uniform(0.0, 30.0);

    const Report now = reportAt(secondsNow, history, phoneActivity);
    const Report later = reportAt(secondsLater, history, phoneActivity);

    const bool sameReadingBothTimes =
        now.hasPosition && later.hasPosition &&
        now.position.latitudeDegrees == later.position.latitudeDegrees &&
        now.gpsErrorMeters == later.gpsErrorMeters &&
        later.readingAgeMs - now.readingAgeMs ==
            secondsToMs(secondsLater) - secondsToMs(secondsNow);
    if (sameReadingBothTimes) {
      roundsThatKeptTheSameReading += 1;
      REQUIRE(later.score.qualityScore <= now.score.qualityScore);
      REQUIRE(later.howFarPhoneCouldBeMeters >= now.howFarPhoneCouldBeMeters);
    }
  }

  CHECK(roundsThatKeptTheSameReading > 1000);
}

// --- Small things ---------------------------------------------------------------------------------

TEST_CASE("every step of a request has a public name") {
  CHECK(std::string(requestStepName(RequestStep::Started)) == "started");
  CHECK(std::string(requestStepName(RequestStep::FirstAttempt)) == "first_attempt");
  CHECK(std::string(requestStepName(RequestStep::ExtraTime)) == "extra_time");
  CHECK(std::string(requestStepName(RequestStep::AnswerFromHistory)) ==
        "answer_from_history");
  CHECK(std::string(requestStepName(RequestStep::Finished)) == "finished");
  CHECK(requestStepName(RequestStep::Count) == nullptr);
}

TEST_CASE("the report counts the readings it looked at") {
  const LocationHistory history = historyWith({
      readingNorth(0, 0),
      readingNorth(1, 0),
      readingNorth(2, 0),
      readingNorth(3, 1000),
  });

  const Report report = reportAt(3, history);

  CHECK(report.readingsExamined == 4);
  CHECK(report.gpsJumpsFound == 1);
  CHECK(report.requestId == 0);      // filled later, by the engine
  CHECK(report.howManySteps == 0);
}
