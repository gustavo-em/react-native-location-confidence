// Usage tests: the pieces of the engine working together.
//
// These scenarios are not in the spec's list of tests. They show, with round
// numbers, what the engine answers in everyday moments of a delivery.
//
//   a reading arrives from the phone              receiveReading()
//     |-- checkReading()                            ReadingChecker.cpp
//     `-- history.store()                           LocationHistory.cpp
//
//   the app asks "is the courier at the address?" askTheEngine()
//     `-- buildReport()                             Report.cpp
//           |-- chooseBestReading()                 ReadingSelection.cpp
//           |     |-- checkAllReadingsForGpsJumps()            JumpDetector.cpp
//           |     |-- calculateTimeInEachPhoneActivity()       PhoneActivityTimeline.cpp
//           |     `-- calculateDistancePhoneCouldHaveMoved()   UncertaintyModel.cpp
//           |-- classifyRelation()                  ProximityRelation.cpp
//           `-- calculateQualityScore()             QualityScore.cpp
//
// In every scenario the delivery address is the starting point, the delivery
// radius is 100 meters and the GPS error of each reading is 5 meters. The
// request obtained every reading itself.
#include "third_party/doctest.h"

#include <cstdint>

#include "Report.hpp"
#include "JumpDetector.hpp"
#include "LocationHistory.hpp"
#include "PhoneActivityTimeline.hpp"
#include "ReadingChecker.hpp"
#include "TestSupport.hpp"

using namespace lce;
using lce_test::phoneReported;
using lce_test::phoneReportedNothing;
using lce_test::readingNorth;
using lce_test::secondsToMs;

namespace {

// A reading exactly as the phone would deliver it, not checked yet.
UncheckedReading readingFromThePhone(double seconds, double metersFromAddress) {
  const LocationReading where = readingNorth(seconds, metersFromAddress);

  UncheckedReading unchecked = lce_test::validUncheckedReading();
  unchecked.latitudeDegrees = where.position.latitudeDegrees;
  unchecked.longitudeDegrees = where.position.longitudeDegrees;
  unchecked.horizontalAccuracyMeters = 5.0;
  unchecked.capturedAtMs = where.capturedAtMs;
  return unchecked;
}

// A reading arrives: it passes the entrance check and goes into the history.
void receiveReading(LocationHistory& history, const UncheckedReading& unchecked) {
  const ReadingCheck check = checkReading(unchecked, unchecked.capturedAtMs);
  REQUIRE(check.accepted);
  history.store(check.reading, unchecked.capturedAtMs);
}

void receiveReading(LocationHistory& history, double seconds, double metersFromAddress) {
  receiveReading(history, readingFromThePhone(seconds, metersFromAddress));
}

// The app asks: is the courier at the address?
Report askTheEngine(const LocationHistory& history,
                        double secondsNow,
                        const PhoneActivityHistory& phoneActivity) {
  WhatTheAppAskedFor asked;
  asked.destination = Coordinates{0.0, 0.0};
  asked.radiusMeters = 100.0;

  const WhatTheRequestKnows request;  // it obtained every reading; permission granted

  return buildReport(history, phoneActivity, secondsToMs(secondsNow), asked, request);
}

// Three readings 20 meters from the address, one per second.
LocationHistory courierAtTheDoor() {
  LocationHistory history;
  receiveReading(history, 0, 20);
  receiveReading(history, 1, 20);
  receiveReading(history, 2, 20);
  return history;
}

}  // namespace

TEST_CASE("USE-1 courier at the door, and the phone says it is standing still") {
  const LocationHistory history = courierAtTheDoor();

  // The newest reading is 1 second old.
  const Report answer =
      askTheEngine(history, 3, phoneReported(PhoneActivity::StandingStill, 0, 3));

  CHECK(answer.status == ReportStatus::Available);
  CHECK(answer.source == FixSource::Fresh);
  CHECK(answer.readingAgeMs == 1000);
  CHECK(answer.howFarPhoneCouldBeMeters == doctest::Approx(5.5));  // 5 + 0.5 x 1
  CHECK(answer.distanceToDestinationMeters == doctest::Approx(20.0));

  // 20 + 5.5 = 25.5, less than 100.
  CHECK(answer.relation == EvidenceRelation::SupportsInside);

  // accuracy 0.95, freshness 0.98, movement 0.995, three readings agree.
  CHECK(answer.score.qualityScore == 97);
  CHECK_FALSE(answer.score.anyLimitApplied());
}

TEST_CASE("USE-2 same place, but the phone reported nothing about its activity") {
  const LocationHistory history = courierAtTheDoor();

  // 1 second after the newest reading: 45 x 1 = 45 meters.
  const Report afterOneSecond = askTheEngine(history, 3, phoneReportedNothing());
  CHECK(afterOneSecond.howFarPhoneCouldBeMeters == doctest::Approx(50.0));
  CHECK(afterOneSecond.relation == EvidenceRelation::SupportsInside);  // 20 + 50 = 70
  CHECK(afterOneSecond.score.qualityScore == 89);

  // 3 seconds after it: 45 x 3 = 135 meters. The same reading no longer
  // supports "inside", and the score falls to the limit for a wide doubt.
  const Report afterThreeSeconds = askTheEngine(history, 5, phoneReportedNothing());
  CHECK(afterThreeSeconds.howFarPhoneCouldBeMeters == doctest::Approx(140.0));
  CHECK(afterThreeSeconds.relation == EvidenceRelation::Inconclusive);  // 20 + 140 = 160
  CHECK(afterThreeSeconds.score.rawScore == 77);
  CHECK(afterThreeSeconds.score.qualityScore == 35);
  CHECK(afterThreeSeconds.score.wasLimitedBy(ScoreLimit::UncertaintyAtLeast100));
  // The reading is the same and it was obtained now: the status does not change.
  CHECK(afterThreeSeconds.status == ReportStatus::Available);

  // Knowing that the phone stood still keeps the answer after 3 seconds.
  const Report standingStill =
      askTheEngine(history, 5, phoneReported(PhoneActivity::StandingStill, 0, 5));
  CHECK(standingStill.howFarPhoneCouldBeMeters == doctest::Approx(6.5));  // 5 + 0.5 x 3
  CHECK(standingStill.relation == EvidenceRelation::SupportsInside);
  CHECK(standingStill.score.qualityScore == 96);
}

TEST_CASE("USE-3 the GPS jumps away for one reading and comes back") {
  LocationHistory history;
  receiveReading(history, 0, 20);
  receiveReading(history, 2, 470);  // 450 meters from the others
  receiveReading(history, 4, 20);

  // The reading in the middle is the one labelled as a jump.
  const JumpCheckOfAllReadings jumpCheck = checkAllReadingsForGpsJumps(history);
  CHECK(jumpCheck.ofEachReading[1].result == JumpCheckResult::ProbablyGpsJump);

  const Report answer =
      askTheEngine(history, 5, phoneReported(PhoneActivity::StandingStill, 0, 5));

  // The answer uses the newest good reading, not the one that jumped.
  CHECK(answer.distanceToDestinationMeters == doctest::Approx(20.0));
  CHECK(answer.readingAgeMs == 1000);
  CHECK(answer.gpsJumpsFound == 1);
  CHECK(answer.reasons.contains(ReasonCode::SuspectedGpsJump));
  CHECK(answer.relation == EvidenceRelation::SupportsInside);  // 20 + 5.5 = 25.5

  // It is still usable, with less trust: a reading contradicts it.
  CHECK(answer.score.rawScore == 87);
  CHECK(answer.score.qualityScore == 75);
  CHECK(answer.score.wasLimitedBy(ScoreLimit::ConflictingEvidence));
  CHECK_FALSE(answer.score.wasLimitedBy(ScoreLimit::SuspectedJump));
}

TEST_CASE("USE-4 courier walking, still far from the address") {
  LocationHistory history;
  receiveReading(history, 0, 180);
  receiveReading(history, 1, 177);
  receiveReading(history, 2, 174);

  const Report answer = askTheEngine(history, 3, phoneReported(PhoneActivity::Walking, 0, 3));

  CHECK(answer.howFarPhoneCouldBeMeters == doctest::Approx(8.0));  // 5 + 3 x 1
  CHECK(answer.distanceToDestinationMeters == doctest::Approx(174.0));

  // 174 - 8 = 166, more than 100: outside even in the best case.
  CHECK(answer.relation == EvidenceRelation::SupportsOutside);

  // The score is high because the EVIDENCE is good. It says nothing about
  // the courier having arrived.
  CHECK(answer.score.qualityScore == 97);
  CHECK(answer.status == ReportStatus::Available);
}

TEST_CASE("USE-5 one reading that came with a speed from the GPS") {
  UncheckedReading unchecked = readingFromThePhone(0, 20);
  unchecked.hasSpeed = true;
  unchecked.speedMetersPerSecond = 0.3;
  unchecked.hasSpeedAccuracy = true;
  unchecked.speedAccuracyMetersPerSecond = 0.5;

  LocationHistory history;
  receiveReading(history, unchecked);

  // 3 seconds later, nothing reported about the activity: the GPS speed is
  // what makes the distance small (option 2 of the movement model).
  const Report answer = askTheEngine(history, 3, phoneReportedNothing());

  CHECK(answer.howFarPhoneCouldBeMeters == doctest::Approx(20.15));  // 5 + 15.15
  CHECK(answer.relation == EvidenceRelation::SupportsInside);        // 20 + 20.15 = 40.15

  // A single reading has nothing to confirm it: at most 75.
  CHECK(answer.score.rawScore == 88);
  CHECK(answer.score.qualityScore == 75);
  CHECK(answer.score.wasLimitedBy(ScoreLimit::SingleFix));
}

TEST_CASE("USE-6 one old reading and nothing else known") {
  LocationHistory history;
  receiveReading(history, 0, 20);

  // 20 seconds later: 45 x 20 = 900 meters.
  const Report answer = askTheEngine(history, 20, phoneReportedNothing());

  CHECK(answer.howFarPhoneCouldBeMeters == doctest::Approx(905.0));
  CHECK(answer.relation == EvidenceRelation::Inconclusive);

  // A reading older than 10 seconds no longer counts as "obtained now".
  CHECK(answer.source == FixSource::Cached);
  CHECK(answer.status == ReportStatus::Degraded);
  CHECK(answer.reasons.contains(ReasonCode::LocationStale));

  CHECK(answer.score.rawScore == 64);
  CHECK(answer.score.qualityScore == 15);
  CHECK(answer.score.wasLimitedBy(ScoreLimit::UncertaintyAtLeast300));
  CHECK(answer.score.wasLimitedBy(ScoreLimit::Cached));
  CHECK(answer.score.wasLimitedBy(ScoreLimit::UnknownMovementNotFresh));
}

TEST_CASE("USE-7 the phone only started to report 2 seconds ago") {
  const LocationHistory history = courierAtTheDoor();

  // Now is second 12: the newest reading (taken at second 2) is 10 seconds old.

  // "Standing still" from second 10 to second 12 covers only the last 2 of
  // those 10 seconds. The other 8 are unknown: 45 x 8 + 0.5 x 2 = 361 meters.
  const Report coveredAtTheEnd =
      askTheEngine(history, 12, phoneReported(PhoneActivity::StandingStill, 10, 12));
  CHECK(coveredAtTheEnd.howFarPhoneCouldBeMeters == doctest::Approx(366.0));
  CHECK(coveredAtTheEnd.relation == EvidenceRelation::Inconclusive);
  CHECK(coveredAtTheEnd.score.rawScore == 74);
  CHECK(coveredAtTheEnd.score.qualityScore == 15);
  CHECK(coveredAtTheEnd.score.wasLimitedBy(ScoreLimit::UncertaintyAtLeast300));
  CHECK(coveredAtTheEnd.phoneActivity.hasGaps);
  CHECK_FALSE(coveredAtTheEnd.phoneActivity.standingStillSinceTheReading);

  // "Standing still" during the whole 10 seconds: 0.5 x 10 = 5 meters.
  const Report coveredAllTheTime =
      askTheEngine(history, 12, phoneReported(PhoneActivity::StandingStill, 0, 12));
  CHECK(coveredAllTheTime.howFarPhoneCouldBeMeters == doctest::Approx(10.0));
  CHECK(coveredAllTheTime.relation == EvidenceRelation::SupportsInside);  // 20 + 10 = 30
  CHECK(coveredAllTheTime.score.qualityScore == 93);
  CHECK(coveredAllTheTime.phoneActivity.standingStillSinceTheReading);
}
