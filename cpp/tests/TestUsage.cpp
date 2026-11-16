// Usage tests: the pieces that exist today, working together.
//
// These scenarios are not in the spec's list of tests. They show, with round
// numbers, what the engine answers in everyday moments of a delivery.
//
// The candidate selector (step 2d) does not exist yet. Until it does,
// judgeReading() below connects the pieces by hand, in the order the selector
// will use:
//
//   a reading arrives from the phone              receiveReading()
//     |-- checkReading()                            ReadingChecker.cpp
//     `-- history.store()                           LocationHistory.cpp
//
//   the phone reports its activity                phoneReported()
//     `-- phoneActivity.store()                     PhoneActivityTimeline.hpp
//
//   the app asks "is the courier at the address?" judgeReading()
//     |-- checkAllReadingsForGpsJumps()             JumpDetector.cpp
//     |-- calculateTimeInEachPhoneActivity()        PhoneActivityTimeline.cpp
//     |-- calculateDistancePhoneCouldHaveMoved()    UncertaintyModel.cpp
//     |-- calculateHowFarPhoneCouldBeFromReading()  UncertaintyModel.cpp
//     |-- distanceBetweenMeters()                   GeoMath.cpp
//     |-- classifyRelation()                        ProximityRelation.cpp
//     `-- calculateQualityScore()                   QualityScore.cpp
//
// In every scenario the delivery address is the starting point, the delivery
// radius is 100 meters and the GPS error of each reading is 5 meters.
#include "third_party/doctest.h"

#include <cstdint>

#include "GeoMath.hpp"
#include "JumpDetector.hpp"
#include "LocationHistory.hpp"
#include "PhoneActivityTimeline.hpp"
#include "ProximityRelation.hpp"
#include "QualityScore.hpp"
#include "ReadingChecker.hpp"
#include "TestSupport.hpp"
#include "UncertaintyModel.hpp"

using namespace lce;
using lce_test::readingNorth;

namespace {

const Coordinates DELIVERY_ADDRESS{0.0, 0.0};
const double DELIVERY_RADIUS_METERS = 100.0;

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

// The phone reported one activity, and it is very sure about it.
PhoneActivityHistory phoneReported(PhoneActivity activity, double fromSeconds, double toSeconds) {
  PhoneActivityReport report;
  report.startedAtMs = static_cast<std::int64_t>(fromSeconds * 1000.0);
  report.endedAtMs = static_cast<std::int64_t>(toSeconds * 1000.0);
  report.activity = activity;
  report.confidence = PhoneActivityConfidence::High;

  PhoneActivityHistory phoneActivity;
  phoneActivity.store(report);
  return phoneActivity;
}

// The phone reported nothing about its activity.
PhoneActivityHistory phoneReportedNothing() {
  return PhoneActivityHistory{};
}

// Everything the engine can say today about one reading.
struct Answer {
  JumpCheckResult label = JumpCheckResult::LooksFine;
  double metersPhoneCouldHaveMoved = 0.0;
  double metersPhoneCouldBeFromReading = 0.0;
  double metersFromReadingToAddress = 0.0;
  EvidenceRelation relation = EvidenceRelation::Inconclusive;
  ScoreResult score;
};

Answer judgeReading(const LocationHistory& history,
                    std::size_t position,
                    double secondsNow,
                    const PhoneActivityHistory& phoneActivity) {
  const LocationReading& reading = history.at(position);
  const std::int64_t nowMs = static_cast<std::int64_t>(secondsNow * 1000.0);

  Answer answer;

  // 1. Does this reading look like a GPS jump?
  const JumpCheckOfAllReadings jumpCheck = checkAllReadingsForGpsJumps(history);
  const JumpCheckOfOneReading& checkOfThisReading = jumpCheck.ofEachReading[position];
  answer.label = checkOfThisReading.result;

  const bool probablyGpsJump = (answer.label == JumpCheckResult::ProbablyGpsJump);
  const bool somethingContradictsIt = (answer.label != JumpCheckResult::LooksFine);

  // 2. Since the reading was taken, how long did the phone spend in each
  //    activity?
  const TimeInEachPhoneActivity timeInEachActivity =
      calculateTimeInEachPhoneActivity(phoneActivity, reading.capturedAtMs, nowMs);

  // 3. How far could the phone have moved in that time, and how far from the
  //    reading could it be now?
  const DistancePhoneCouldHaveMoved distanceMoved =
      calculateDistancePhoneCouldHaveMoved(reading, timeInEachActivity);
  answer.metersPhoneCouldHaveMoved = distanceMoved.meters;
  answer.metersPhoneCouldBeFromReading =
      calculateHowFarPhoneCouldBeFromReading(reading, distanceMoved);

  // 4. Inside the delivery radius, outside it, or cannot tell?
  answer.metersFromReadingToAddress = distanceBetweenMeters(reading.position, DELIVERY_ADDRESS);

  RelationGates gates = lce_test::openGates();
  gates.strongJumpSuspicion = probablyGpsJump;
  answer.relation = classifyRelation(answer.metersFromReadingToAddress,
                                     answer.metersPhoneCouldBeFromReading,
                                     DELIVERY_RADIUS_METERS, gates);

  // 5. The quality score of the evidence.
  ScoreInput input;
  input.hasCandidate = true;
  input.ageReliable = true;
  input.horizontalAccuracyMeters = reading.horizontalAccuracyMeters;
  input.ageMs = static_cast<double>(timeInEachActivity.totalMilliseconds());
  input.possibleMovementMeters = distanceMoved.meters;
  input.movementIsUnknown = distanceMoved.weOnlyAssumedTheWorst();
  input.consistency = consistencyFromAgreeingReadings(checkOfThisReading.readingsOnItsSide,
                                                      somethingContradictsIt);
  input.source = FixSource::Fresh;
  input.suspectedJump = probablyGpsJump;
  input.mockSignal = MockSignal::NotFlaggedByOs;
  answer.score = calculateQualityScore(input);

  return answer;
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
  const Answer answer =
      judgeReading(history, 2, 3, phoneReported(PhoneActivity::StandingStill, 0, 3));

  CHECK(answer.label == JumpCheckResult::LooksFine);
  CHECK(answer.metersPhoneCouldHaveMoved == doctest::Approx(0.5));      // 0.5 x 1
  CHECK(answer.metersPhoneCouldBeFromReading == doctest::Approx(5.5));  // 5 + 0.5
  CHECK(answer.metersFromReadingToAddress == doctest::Approx(20.0));

  // 20 + 5.5 = 25.5, less than 100.
  CHECK(answer.relation == EvidenceRelation::SupportsInside);

  // accuracy 0.95, freshness 0.98, movement 0.995, three readings agree.
  CHECK(answer.score.qualityScore == 97);
  CHECK_FALSE(answer.score.anyLimitApplied());
}

TEST_CASE("USE-2 same place, but the phone reported nothing about its activity") {
  const LocationHistory history = courierAtTheDoor();

  // 1 second after the newest reading: 45 x 1 = 45 meters.
  const Answer afterOneSecond = judgeReading(history, 2, 3, phoneReportedNothing());
  CHECK(afterOneSecond.metersPhoneCouldHaveMoved == doctest::Approx(45.0));
  CHECK(afterOneSecond.metersPhoneCouldBeFromReading == doctest::Approx(50.0));
  CHECK(afterOneSecond.relation == EvidenceRelation::SupportsInside);  // 20 + 50 = 70
  CHECK(afterOneSecond.score.qualityScore == 89);

  // 3 seconds after it: 45 x 3 = 135 meters. The same reading no longer
  // supports "inside", and the score falls to the limit for a wide doubt.
  const Answer afterThreeSeconds = judgeReading(history, 2, 5, phoneReportedNothing());
  CHECK(afterThreeSeconds.metersPhoneCouldHaveMoved == doctest::Approx(135.0));
  CHECK(afterThreeSeconds.metersPhoneCouldBeFromReading == doctest::Approx(140.0));
  CHECK(afterThreeSeconds.relation == EvidenceRelation::Inconclusive);  // 20 + 140 = 160
  CHECK(afterThreeSeconds.score.rawScore == 77);
  CHECK(afterThreeSeconds.score.qualityScore == 35);
  CHECK(afterThreeSeconds.score.wasLimitedBy(ScoreLimit::UncertaintyAtLeast100));

  // Knowing that the phone stood still keeps the answer after 3 seconds.
  const Answer standingStill =
      judgeReading(history, 2, 5, phoneReported(PhoneActivity::StandingStill, 0, 5));
  CHECK(standingStill.metersPhoneCouldBeFromReading == doctest::Approx(6.5));  // 5 + 0.5 x 3
  CHECK(standingStill.relation == EvidenceRelation::SupportsInside);
  CHECK(standingStill.score.qualityScore == 96);
}

TEST_CASE("USE-3 the GPS jumps away for one reading and comes back") {
  LocationHistory history;
  receiveReading(history, 0, 20);
  receiveReading(history, 2, 470);  // 450 meters from the others
  receiveReading(history, 4, 20);

  const PhoneActivityHistory standingStill = phoneReported(PhoneActivity::StandingStill, 0, 5);

  // The reading that jumped: not trusted.
  const Answer theJump = judgeReading(history, 1, 5, standingStill);
  CHECK(theJump.label == JumpCheckResult::ProbablyGpsJump);
  // The numbers alone would say "outside" (470 - 6.5 is more than 100), but
  // a probable GPS jump never takes a side.
  CHECK(theJump.relation == EvidenceRelation::Inconclusive);
  CHECK(theJump.score.rawScore == 86);
  CHECK(theJump.score.qualityScore == 20);
  CHECK(theJump.score.wasLimitedBy(ScoreLimit::SuspectedJump));

  // The newest good reading: still usable, with less trust.
  const Answer theGoodOne = judgeReading(history, 2, 5, standingStill);
  CHECK(theGoodOne.label == JumpCheckResult::InConflict);
  CHECK(theGoodOne.relation == EvidenceRelation::SupportsInside);  // 20 + 5.5 = 25.5
  CHECK(theGoodOne.score.rawScore == 87);
  CHECK(theGoodOne.score.qualityScore == 75);
  CHECK(theGoodOne.score.wasLimitedBy(ScoreLimit::ConflictingEvidence));
  CHECK_FALSE(theGoodOne.score.wasLimitedBy(ScoreLimit::SuspectedJump));
}

TEST_CASE("USE-4 courier walking, still far from the address") {
  LocationHistory history;
  receiveReading(history, 0, 180);
  receiveReading(history, 1, 177);
  receiveReading(history, 2, 174);

  const Answer answer = judgeReading(history, 2, 3, phoneReported(PhoneActivity::Walking, 0, 3));

  CHECK(answer.label == JumpCheckResult::LooksFine);
  CHECK(answer.metersPhoneCouldHaveMoved == doctest::Approx(3.0));      // 3 x 1
  CHECK(answer.metersPhoneCouldBeFromReading == doctest::Approx(8.0));  // 5 + 3

  // 174 - 8 = 166, more than 100: outside even in the best case.
  CHECK(answer.relation == EvidenceRelation::SupportsOutside);

  // The score is high because the EVIDENCE is good. It says nothing about
  // the courier having arrived.
  CHECK(answer.score.qualityScore == 97);
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
  // what makes the distance small (option 2).
  const Answer answer = judgeReading(history, 0, 3, phoneReportedNothing());

  CHECK(answer.metersPhoneCouldHaveMoved == doctest::Approx(15.15));
  CHECK(answer.metersPhoneCouldBeFromReading == doctest::Approx(20.15));
  CHECK(answer.relation == EvidenceRelation::SupportsInside);  // 20 + 20.15 = 40.15

  // A single reading has nothing to confirm it: at most 75.
  CHECK(answer.score.rawScore == 88);
  CHECK(answer.score.qualityScore == 75);
  CHECK(answer.score.wasLimitedBy(ScoreLimit::SingleFix));
}

TEST_CASE("USE-6 one old reading and nothing else known") {
  LocationHistory history;
  receiveReading(history, 0, 20);

  // 20 seconds later: 45 x 20 = 900 meters.
  const Answer answer = judgeReading(history, 0, 20, phoneReportedNothing());

  CHECK(answer.metersPhoneCouldHaveMoved == doctest::Approx(900.0));
  CHECK(answer.metersPhoneCouldBeFromReading == doctest::Approx(905.0));
  CHECK(answer.relation == EvidenceRelation::Inconclusive);

  CHECK(answer.score.rawScore == 64);
  CHECK(answer.score.qualityScore == 15);
  CHECK(answer.score.wasLimitedBy(ScoreLimit::UncertaintyAtLeast300));
}

TEST_CASE("USE-7 the phone only started to report 2 seconds ago") {
  const LocationHistory history = courierAtTheDoor();

  // Now is second 12: the newest reading (taken at second 2) is 10 seconds old.

  // "Standing still" from second 10 to second 12 covers only the last 2 of
  // those 10 seconds. The other 8 are unknown: 45 x 8 + 0.5 x 2 = 361 meters.
  const Answer coveredAtTheEnd =
      judgeReading(history, 2, 12, phoneReported(PhoneActivity::StandingStill, 10, 12));
  CHECK(coveredAtTheEnd.metersPhoneCouldHaveMoved == doctest::Approx(361.0));
  CHECK(coveredAtTheEnd.metersPhoneCouldBeFromReading == doctest::Approx(366.0));
  CHECK(coveredAtTheEnd.relation == EvidenceRelation::Inconclusive);
  CHECK(coveredAtTheEnd.score.rawScore == 74);
  CHECK(coveredAtTheEnd.score.qualityScore == 15);
  CHECK(coveredAtTheEnd.score.wasLimitedBy(ScoreLimit::UncertaintyAtLeast300));

  // "Standing still" during the whole 10 seconds: 0.5 x 10 = 5 meters.
  const Answer coveredAllTheTime =
      judgeReading(history, 2, 12, phoneReported(PhoneActivity::StandingStill, 0, 12));
  CHECK(coveredAllTheTime.metersPhoneCouldHaveMoved == doctest::Approx(5.0));
  CHECK(coveredAllTheTime.metersPhoneCouldBeFromReading == doctest::Approx(10.0));
  CHECK(coveredAllTheTime.relation == EvidenceRelation::SupportsInside);  // 20 + 10 = 30
  CHECK(coveredAllTheTime.score.qualityScore == 93);
}
