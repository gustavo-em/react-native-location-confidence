// Tests for the movement model: how far the phone could have moved since a
// GPS reading was taken.
#include "third_party/doctest.h"

#include "QualityScore.hpp"
#include "TestSupport.hpp"
#include "UncertaintyModel.hpp"

using namespace lce;
using lce_test::DeterministicRng;
using lce_test::perfectScoreInput;
using lce_test::readingAt;

namespace {

// A reading for which the GPS also reported a speed and how wrong it may be.
LocationReading readingWithSpeed(double speed, double speedError, double accuracyMeters = 10.0) {
  LocationReading reading = readingAt(0, 0.0, 0.0, accuracyMeters);
  reading.speedMetersPerSecond = speed;
  reading.speedAccuracyMetersPerSecond = speedError;
  return reading;
}

// A reading with no speed information at all.
LocationReading readingWithoutSpeed(double accuracyMeters = 10.0) {
  return readingAt(0, 0.0, 0.0, accuracyMeters);
}

// Builds the score input for a reading, using the model's answer.
ScoreInput scoreInputFor(const LocationReading& reading,
                         std::int64_t millisecondsSinceReading,
                         const DistancePhoneCouldHaveMoved& movement) {
  ScoreInput input = perfectScoreInput();
  input.horizontalAccuracyMeters = reading.horizontalAccuracyMeters;
  input.ageMs = static_cast<double>(millisecondsSinceReading);
  input.possibleMovementMeters = movement.meters;
  input.movementIsUnknown = movement.weOnlyAssumedTheWorst();
  return input;
}

// A table with a random amount of time in each activity.
TimeInEachPhoneActivity randomTimeInEachActivity(DeterministicRng& rng) {
  TimeInEachPhoneActivity timeInEachActivity;
  for (int activity = 0; activity < 6; ++activity) {
    const bool thisActivityHappened = rng.below(2) == 0;
    if (thisActivityHappened) {
      timeInEachActivity.add(static_cast<PhoneActivity>(activity),
                             static_cast<std::int64_t>(rng.below(8000)));
    }
  }
  return timeInEachActivity;
}

}  // namespace

// --- The table of time in each activity ---------------------------------------

TEST_CASE("the table has one line per activity and they add up") {
  TimeInEachPhoneActivity timeInEachActivity;
  CHECK(timeInEachActivity.totalMilliseconds() == 0);
  CHECK(timeInEachActivity.weKnowNothing());
  CHECK_FALSE(timeInEachActivity.standingStillTheWholeTime());

  timeInEachActivity.add(PhoneActivity::StandingStill, 20000);
  CHECK(timeInEachActivity.millisecondsStandingStill == 20000);
  CHECK(timeInEachActivity.totalMilliseconds() == 20000);
  CHECK_FALSE(timeInEachActivity.weKnowNothing());
  CHECK(timeInEachActivity.standingStillTheWholeTime());

  timeInEachActivity.add(PhoneActivity::Walking, 10000);
  timeInEachActivity.add(PhoneActivity::Walking, 500);  // the same line again
  timeInEachActivity.add(PhoneActivity::Running, 1);
  timeInEachActivity.add(PhoneActivity::Cycling, 2);
  timeInEachActivity.add(PhoneActivity::InVehicle, 3);
  timeInEachActivity.add(PhoneActivity::Unknown, 4);
  CHECK(timeInEachActivity.millisecondsWalking == 10500);
  CHECK(timeInEachActivity.millisecondsRunning == 1);
  CHECK(timeInEachActivity.millisecondsCycling == 2);
  CHECK(timeInEachActivity.millisecondsInVehicle == 3);
  CHECK(timeInEachActivity.millisecondsUnknown == 4);
  CHECK(timeInEachActivity.totalMilliseconds() == 30510);
  CHECK_FALSE(timeInEachActivity.standingStillTheWholeTime());

  // A negative time makes no sense and is ignored.
  timeInEachActivity.add(PhoneActivity::Walking, -700);
  CHECK(timeInEachActivity.millisecondsWalking == 10500);
}

TEST_CASE("the simplest table: one activity during the whole time") {
  const TimeInEachPhoneActivity walking = wholeTimeInOneActivity(PhoneActivity::Walking, 5000);
  CHECK(walking.millisecondsWalking == 5000);
  CHECK(walking.totalMilliseconds() == 5000);
  CHECK_FALSE(walking.weKnowNothing());

  const TimeInEachPhoneActivity nothingKnown =
      wholeTimeInOneActivity(PhoneActivity::Unknown, 5000);
  CHECK(nothingKnown.millisecondsUnknown == 5000);
  CHECK(nothingKnown.weKnowNothing());
}

// --- One activity during the whole time ----------------------------------------

TEST_CASE("MOT-002 stationary for 30 seconds allows a small drift (model part)") {
  const DistancePhoneCouldHaveMoved estimate = calculateDistancePhoneCouldHaveMoved(
      readingWithoutSpeed(), wholeTimeInOneActivity(PhoneActivity::StandingStill, 30000));
  CHECK(estimate.meters == doctest::Approx(15.0));  // 0.5 m/s x 30 s
  CHECK(estimate.howItWasCalculated == HowDistanceWasCalculated::FromPhoneActivity);
  CHECK_FALSE(estimate.weOnlyAssumedTheWorst());
}

TEST_CASE("MOT-005 vehicle for 10 seconds") {
  const DistancePhoneCouldHaveMoved estimate = calculateDistancePhoneCouldHaveMoved(
      readingWithoutSpeed(), wholeTimeInOneActivity(PhoneActivity::InVehicle, 10000));
  CHECK(estimate.meters == doctest::Approx(450.0));  // 45 m/s x 10 s
  CHECK(estimate.howItWasCalculated == HowDistanceWasCalculated::FromPhoneActivity);
}

TEST_CASE("MOT-006 walking for 5 seconds") {
  // Example 1 at the top of UncertaintyModel.cpp.
  const LocationReading reading = readingWithoutSpeed();
  const DistancePhoneCouldHaveMoved estimate = calculateDistancePhoneCouldHaveMoved(
      reading, wholeTimeInOneActivity(PhoneActivity::Walking, 5000));
  CHECK(estimate.meters == doctest::Approx(15.0));  // 3 m/s x 5 s
  CHECK(estimate.howItWasCalculated == HowDistanceWasCalculated::FromPhoneActivity);
  CHECK(calculateHowFarPhoneCouldBeFromReading(reading, estimate) == doctest::Approx(25.0));
}

TEST_CASE("MOT-007 running for 5 seconds") {
  const DistancePhoneCouldHaveMoved estimate = calculateDistancePhoneCouldHaveMoved(
      readingWithoutSpeed(), wholeTimeInOneActivity(PhoneActivity::Running, 5000));
  CHECK(estimate.meters == doctest::Approx(40.0));  // 8 m/s x 5 s
}

TEST_CASE("MOT-008 cycling for 5 seconds") {
  const DistancePhoneCouldHaveMoved estimate = calculateDistancePhoneCouldHaveMoved(
      readingWithoutSpeed(), wholeTimeInOneActivity(PhoneActivity::Cycling, 5000));
  CHECK(estimate.meters == doctest::Approx(60.0));  // 12 m/s x 5 s
}

TEST_CASE("MOT-009 nothing known for 5 seconds uses the worst case") {
  const DistancePhoneCouldHaveMoved estimate = calculateDistancePhoneCouldHaveMoved(
      readingWithoutSpeed(), wholeTimeInOneActivity(PhoneActivity::Unknown, 5000));
  CHECK(estimate.meters == doctest::Approx(225.0));  // 45 m/s x 5 s
  CHECK(estimate.howItWasCalculated == HowDistanceWasCalculated::AssumingFastestPossibleSpeed);
  CHECK(estimate.weOnlyAssumedTheWorst());
}

TEST_CASE("MOT-016 knowing the phone is stationary does not make an imprecise reading precise") {
  const LocationReading imprecise = readingWithoutSpeed(1000.0);
  const DistancePhoneCouldHaveMoved estimate = calculateDistancePhoneCouldHaveMoved(
      imprecise, wholeTimeInOneActivity(PhoneActivity::StandingStill, 2000));

  // The reading's own 1000 m error is still there.
  CHECK(calculateHowFarPhoneCouldBeFromReading(imprecise, estimate) == doctest::Approx(1001.0));

  const ScoreResult score = calculateQualityScore(scoreInputFor(imprecise, 2000, estimate));
  CHECK(score.wasLimitedBy(ScoreLimit::UncertaintyAtLeast300));
  CHECK(score.qualityScore <= 15);
}

// --- The phone did more than one thing -------------------------------------------

TEST_CASE("the phone did two things: the pieces are added") {
  // Example 5 at the top of UncertaintyModel.cpp.
  TimeInEachPhoneActivity timeInEachActivity;
  timeInEachActivity.add(PhoneActivity::StandingStill, 20000);
  timeInEachActivity.add(PhoneActivity::Walking, 10000);

  const DistancePhoneCouldHaveMoved estimate =
      calculateDistancePhoneCouldHaveMoved(readingWithoutSpeed(), timeInEachActivity);
  CHECK(estimate.meters == doctest::Approx(40.0));  // 0.5 x 20 + 3 x 10
  CHECK(estimate.howItWasCalculated == HowDistanceWasCalculated::FromPhoneActivity);
}

TEST_CASE("every activity enters the sum with its own speed") {
  TimeInEachPhoneActivity timeInEachActivity;
  timeInEachActivity.add(PhoneActivity::StandingStill, 1000);
  timeInEachActivity.add(PhoneActivity::Walking, 1000);
  timeInEachActivity.add(PhoneActivity::Running, 1000);
  timeInEachActivity.add(PhoneActivity::Cycling, 1000);
  timeInEachActivity.add(PhoneActivity::InVehicle, 1000);
  timeInEachActivity.add(PhoneActivity::Unknown, 1000);

  const DistancePhoneCouldHaveMoved estimate =
      calculateDistancePhoneCouldHaveMoved(readingWithoutSpeed(), timeInEachActivity);
  CHECK(estimate.meters == doctest::Approx(113.5));  // 0.5 + 3 + 8 + 12 + 45 + 45
}

TEST_CASE("time with no information counts at the fastest speed") {
  // Example 6 at the top of UncertaintyModel.cpp.
  TimeInEachPhoneActivity timeInEachActivity;
  timeInEachActivity.add(PhoneActivity::StandingStill, 2000);
  timeInEachActivity.add(PhoneActivity::Unknown, 8000);

  const DistancePhoneCouldHaveMoved estimate =
      calculateDistancePhoneCouldHaveMoved(readingWithoutSpeed(), timeInEachActivity);
  CHECK(estimate.meters == doctest::Approx(361.0));  // 0.5 x 2 + 45 x 8
  CHECK(estimate.howItWasCalculated == HowDistanceWasCalculated::FromPhoneActivity);

  // With a GPS speed on the reading, option 2 gives 138 meters for the same
  // 10 seconds, and the smaller distance wins.
  const DistancePhoneCouldHaveMoved withGpsSpeed =
      calculateDistancePhoneCouldHaveMoved(readingWithSpeed(0.3, 0.5), timeInEachActivity);
  CHECK(withGpsSpeed.meters == doctest::Approx(138.0));
  CHECK(withGpsSpeed.howItWasCalculated == HowDistanceWasCalculated::FromSpeedReportedByGps);
}

TEST_CASE("pieces at the fastest speed never go over the longest distance possible") {
  // 1.1 seconds in a vehicle and 2.2 seconds unknown: both at 45 m/s.
  // Adding the two pieces gives 148.50000000000003 because of rounding, a
  // hair above 45 x 3.3 = 148.5. The answer must still be the limit.
  TimeInEachPhoneActivity timeInEachActivity;
  timeInEachActivity.add(PhoneActivity::InVehicle, 1100);
  timeInEachActivity.add(PhoneActivity::Unknown, 2200);
  REQUIRE(45.0 * 1.1 + 45.0 * 2.2 > 45.0 * 3.3);

  const DistancePhoneCouldHaveMoved estimate =
      calculateDistancePhoneCouldHaveMoved(readingWithoutSpeed(), timeInEachActivity);
  CHECK(estimate.meters == 45.0 * 3.3);
  CHECK(estimate.howItWasCalculated == HowDistanceWasCalculated::FromPhoneActivity);
}

// --- The speed reported by the GPS -------------------------------------------------

TEST_CASE("MOT-026 the speed reported by the GPS narrows the estimate") {
  // Reported 0.3 m/s with 0.5 m/s error: the speed to be safe is 1.3 m/s.
  const LocationReading almostStill = readingWithSpeed(0.3, 0.5);

  SUBCASE("3 seconds old") {
    // Example 2 at the top of UncertaintyModel.cpp.
    const DistancePhoneCouldHaveMoved estimate = calculateDistancePhoneCouldHaveMoved(
        almostStill, wholeTimeInOneActivity(PhoneActivity::Unknown, 3000));
    // 1.3 x 3 + 2.5 x 3 x 3 / 2 = 3.9 + 11.25
    CHECK(estimate.meters == doctest::Approx(15.15));
    CHECK(estimate.howItWasCalculated == HowDistanceWasCalculated::FromSpeedReportedByGps);
    CHECK_FALSE(estimate.weOnlyAssumedTheWorst());
    CHECK(calculateHowFarPhoneCouldBeFromReading(almostStill, estimate) ==
          doctest::Approx(25.15));
  }
  SUBCASE("10 seconds old") {
    const DistancePhoneCouldHaveMoved estimate = calculateDistancePhoneCouldHaveMoved(
        almostStill, wholeTimeInOneActivity(PhoneActivity::Unknown, 10000));
    // 1.3 x 10 + 2.5 x 10 x 10 / 2 = 13 + 125
    CHECK(estimate.meters == doctest::Approx(138.0));
  }
  SUBCASE("it is far below the worst case") {
    const TimeInEachPhoneActivity threeSecondsUnknown =
        wholeTimeInOneActivity(PhoneActivity::Unknown, 3000);
    const double withSpeed =
        calculateDistancePhoneCouldHaveMoved(almostStill, threeSecondsUnknown).meters;
    const double worstCase =
        calculateDistancePhoneCouldHaveMoved(readingWithoutSpeed(), threeSecondsUnknown).meters;
    CHECK(worstCase == doctest::Approx(135.0));
    CHECK(withSpeed < worstCase);
  }
}

TEST_CASE("MOT-027 a speed without its error is not trusted") {
  const TimeInEachPhoneActivity threeSecondsUnknown =
      wholeTimeInOneActivity(PhoneActivity::Unknown, 3000);

  LocationReading speedOnly = readingWithoutSpeed();
  speedOnly.speedMetersPerSecond = 0.0;  // "standing still", but no error estimate

  const DistancePhoneCouldHaveMoved estimate =
      calculateDistancePhoneCouldHaveMoved(speedOnly, threeSecondsUnknown);
  CHECK(estimate.meters == doctest::Approx(135.0));
  CHECK(estimate.howItWasCalculated == HowDistanceWasCalculated::AssumingFastestPossibleSpeed);

  SUBCASE("an error without a speed is not enough either") {
    LocationReading errorOnly = readingWithoutSpeed();
    errorOnly.speedAccuracyMetersPerSecond = 0.5;
    CHECK(calculateDistancePhoneCouldHaveMoved(errorOnly, threeSecondsUnknown)
              .howItWasCalculated == HowDistanceWasCalculated::AssumingFastestPossibleSpeed);
  }
}

TEST_CASE("MOT-028 the estimate from reported speed never exceeds the worst case") {
  // Example 4 at the top of UncertaintyModel.cpp.
  // Reported 40 m/s with 5 m/s error, 10 seconds: 50 x 10 + 125 = 625 m,
  // but nothing is allowed to travel farther than 45 x 10 = 450 m.
  const LocationReading fast = readingWithSpeed(40.0, 5.0);
  const DistancePhoneCouldHaveMoved estimate = calculateDistancePhoneCouldHaveMoved(
      fast, wholeTimeInOneActivity(PhoneActivity::Unknown, 10000));
  CHECK(estimate.meters == doctest::Approx(450.0));
  CHECK(estimate.howItWasCalculated == HowDistanceWasCalculated::FromSpeedReportedByGps);
}

// --- The smallest distance wins ------------------------------------------------------

TEST_CASE("when both the activity and the GPS speed are known, the smaller distance wins") {
  // The GPS speed alone gives 15.15 meters after 3 seconds.
  const LocationReading almostStopped = readingWithSpeed(0.3, 0.5);

  SUBCASE("the activity gives less: walking 3 m/s x 3 s = 9 meters") {
    const DistancePhoneCouldHaveMoved moved = calculateDistancePhoneCouldHaveMoved(
        almostStopped, wholeTimeInOneActivity(PhoneActivity::Walking, 3000));
    CHECK(moved.meters == doctest::Approx(9.0));
    CHECK(moved.howItWasCalculated == HowDistanceWasCalculated::FromPhoneActivity);
  }
  SUBCASE("the GPS speed gives less: running 8 m/s x 3 s = 24 meters") {
    // Example 7 at the top of UncertaintyModel.cpp.
    const DistancePhoneCouldHaveMoved moved = calculateDistancePhoneCouldHaveMoved(
        almostStopped, wholeTimeInOneActivity(PhoneActivity::Running, 3000));
    CHECK(moved.meters == doctest::Approx(15.15));
    CHECK(moved.howItWasCalculated == HowDistanceWasCalculated::FromSpeedReportedByGps);
  }
  SUBCASE("in a vehicle for 10 seconds: 450 meters against 138 from the GPS speed") {
    const DistancePhoneCouldHaveMoved moved = calculateDistancePhoneCouldHaveMoved(
        almostStopped, wholeTimeInOneActivity(PhoneActivity::InVehicle, 10000));
    CHECK(moved.meters == doctest::Approx(138.0));
    CHECK(moved.howItWasCalculated == HowDistanceWasCalculated::FromSpeedReportedByGps);
  }
}

TEST_CASE("INV-08 losing the phone activity never lowers the distance nor raises the score") {
  DeterministicRng rng(0x494E563038ULL);
  const double fastestChoices[] = {45.0, 5.0, 10.0, 30.0};

  for (int i = 0; i < 6000; ++i) {
    const double accuracy = rng.uniform(0.0, 60.0);
    const bool hasSpeed = rng.below(2) == 0;
    const LocationReading reading =
        hasSpeed ? readingWithSpeed(rng.uniform(0.0, 50.0), rng.uniform(0.0, 5.0), accuracy)
                 : readingWithoutSpeed(accuracy);
    const double fastest = fastestChoices[rng.below(4)];

    // What the phone told us, and the same amount of time with nothing told.
    const TimeInEachPhoneActivity withActivity = randomTimeInEachActivity(rng);
    const std::int64_t age = withActivity.totalMilliseconds();
    const TimeInEachPhoneActivity withoutActivity =
        wholeTimeInOneActivity(PhoneActivity::Unknown, age);

    const DistancePhoneCouldHaveMoved informed =
        calculateDistancePhoneCouldHaveMoved(reading, withActivity, fastest);
    const DistancePhoneCouldHaveMoved uninformed =
        calculateDistancePhoneCouldHaveMoved(reading, withoutActivity, fastest);

    CAPTURE(age);
    CAPTURE(hasSpeed);
    CAPTURE(fastest);
    REQUIRE(uninformed.meters >= informed.meters);

    const int informedScore =
        calculateQualityScore(scoreInputFor(reading, age, informed)).qualityScore;
    const int uninformedScore =
        calculateQualityScore(scoreInputFor(reading, age, uninformed)).qualityScore;
    REQUIRE(uninformedScore <= informedScore);
  }
}

TEST_CASE("INV-08 time that becomes unknown never lowers the distance") {
  DeterministicRng rng(0x554E4B4E4FULL);
  for (int i = 0; i < 6000; ++i) {
    const bool hasSpeed = rng.below(2) == 0;
    const LocationReading reading =
        hasSpeed ? readingWithSpeed(rng.uniform(0.0, 50.0), rng.uniform(0.0, 5.0))
                 : readingWithoutSpeed();

    const TimeInEachPhoneActivity before = randomTimeInEachActivity(rng);

    // Part of the walking time is no longer covered by any report.
    const std::uint64_t walkingBefore = static_cast<std::uint64_t>(before.millisecondsWalking);
    const std::int64_t lost = static_cast<std::int64_t>(rng.below(walkingBefore + 1));

    TimeInEachPhoneActivity after = before;
    after.millisecondsWalking -= lost;
    after.millisecondsUnknown += lost;
    REQUIRE(after.totalMilliseconds() == before.totalMilliseconds());

    REQUIRE(calculateDistancePhoneCouldHaveMoved(reading, after).meters >=
            calculateDistancePhoneCouldHaveMoved(reading, before).meters);
  }
}

TEST_CASE("MOT-029 the estimate never shrinks as the reading gets older") {
  DeterministicRng rng(0x4D4F543239ULL);
  for (int i = 0; i < 6000; ++i) {
    const bool hasSpeed = rng.below(2) == 0;
    const LocationReading reading =
        hasSpeed ? readingWithSpeed(rng.uniform(0.0, 50.0), rng.uniform(0.0, 5.0))
                 : readingWithoutSpeed();

    const TimeInEachPhoneActivity now = randomTimeInEachActivity(rng);

    // The same reading later: more time passed, in any one activity.
    TimeInEachPhoneActivity later = now;
    later.add(static_cast<PhoneActivity>(rng.below(6)),
              static_cast<std::int64_t>(rng.below(60000)));

    const double metersNow = calculateDistancePhoneCouldHaveMoved(reading, now).meters;
    const double metersLater = calculateDistancePhoneCouldHaveMoved(reading, later).meters;

    REQUIRE(metersNow >= 0.0);
    REQUIRE(metersLater >= metersNow);
    // Never beyond the worst case either.
    const double secondsLater = static_cast<double>(later.totalMilliseconds()) / 1000.0;
    REQUIRE(metersLater <= 45.0 * secondsLater);
  }
}

TEST_CASE("MOT-030 losing the reported speed never lowers the estimate nor raises the score") {
  DeterministicRng rng(0x4D4F543330ULL);
  for (int i = 0; i < 6000; ++i) {
    const double accuracy = rng.uniform(0.0, 60.0);
    const LocationReading withSpeed =
        readingWithSpeed(rng.uniform(0.0, 50.0), rng.uniform(0.0, 5.0), accuracy);
    const LocationReading withoutSpeed = readingWithoutSpeed(accuracy);

    const TimeInEachPhoneActivity timeInEachActivity = randomTimeInEachActivity(rng);
    const std::int64_t age = timeInEachActivity.totalMilliseconds();

    const DistancePhoneCouldHaveMoved informed =
        calculateDistancePhoneCouldHaveMoved(withSpeed, timeInEachActivity);
    const DistancePhoneCouldHaveMoved uninformed =
        calculateDistancePhoneCouldHaveMoved(withoutSpeed, timeInEachActivity);

    REQUIRE(uninformed.meters >= informed.meters);

    const int informedScore =
        calculateQualityScore(scoreInputFor(withSpeed, age, informed)).qualityScore;
    const int uninformedScore =
        calculateQualityScore(scoreInputFor(withoutSpeed, age, uninformed)).qualityScore;
    REQUIRE(uninformedScore <= informedScore);
  }
}

TEST_CASE("SCR-008 replacing 'stationary' with 'unknown' never raises the score") {
  DeterministicRng rng(0x5343523038ULL);
  for (int i = 0; i < 3000; ++i) {
    const LocationReading reading = readingWithoutSpeed(rng.uniform(0.0, 60.0));
    const std::int64_t age = static_cast<std::int64_t>(rng.below(30000));

    const DistancePhoneCouldHaveMoved stationary = calculateDistancePhoneCouldHaveMoved(
        reading, wholeTimeInOneActivity(PhoneActivity::StandingStill, age));
    const DistancePhoneCouldHaveMoved unknown = calculateDistancePhoneCouldHaveMoved(
        reading, wholeTimeInOneActivity(PhoneActivity::Unknown, age));

    const int stationaryScore =
        calculateQualityScore(scoreInputFor(reading, age, stationary)).qualityScore;
    const int unknownScore =
        calculateQualityScore(scoreInputFor(reading, age, unknown)).qualityScore;
    REQUIRE(unknownScore <= stationaryScore);
  }
}

TEST_CASE("SCR-028 an older reading never has a smaller total uncertainty") {
  const LocationReading reading = readingWithoutSpeed(10.0);

  const double atOneSecond = calculateHowFarPhoneCouldBeFromReading(
      reading, calculateDistancePhoneCouldHaveMoved(
                   reading, wholeTimeInOneActivity(PhoneActivity::Walking, 1000)));
  const double atTwentySeconds = calculateHowFarPhoneCouldBeFromReading(
      reading, calculateDistancePhoneCouldHaveMoved(
                   reading, wholeTimeInOneActivity(PhoneActivity::Walking, 20000)));

  CHECK(atOneSecond == doctest::Approx(13.0));      // 10 m + 3 m
  CHECK(atTwentySeconds == doctest::Approx(70.0));  // 10 m + 60 m
  CHECK(atTwentySeconds >= atOneSecond);
}

// --- Edges ---------------------------------------------------------------------------

TEST_CASE("uncertainty: a reading taken right now could not have moved") {
  CHECK(calculateDistancePhoneCouldHaveMoved(readingWithoutSpeed(),
                                             wholeTimeInOneActivity(PhoneActivity::Unknown, 0))
            .meters == 0.0);
  CHECK(calculateDistancePhoneCouldHaveMoved(readingWithSpeed(10.0, 1.0),
                                             wholeTimeInOneActivity(PhoneActivity::Unknown, 0))
            .meters == 0.0);
  CHECK(calculateDistancePhoneCouldHaveMoved(
            readingWithoutSpeed(), wholeTimeInOneActivity(PhoneActivity::StandingStill, 0))
            .meters == 0.0);

  // A negative time makes no sense and is treated as zero.
  CHECK(calculateDistancePhoneCouldHaveMoved(
            readingWithoutSpeed(), wholeTimeInOneActivity(PhoneActivity::InVehicle, -5000))
            .meters == 0.0);
}

TEST_CASE("uncertainty: the app can change the fastest plausible speed") {
  const LocationReading reading = readingWithoutSpeed();

  // Couriers on foot only: nothing faster than 5 m/s.
  SUBCASE("it changes the worst case and the vehicle speed") {
    CHECK(calculateDistancePhoneCouldHaveMoved(
              reading, wholeTimeInOneActivity(PhoneActivity::Unknown, 10000), 5.0)
              .meters == doctest::Approx(50.0));
    CHECK(calculateDistancePhoneCouldHaveMoved(
              reading, wholeTimeInOneActivity(PhoneActivity::InVehicle, 10000), 5.0)
              .meters == doctest::Approx(50.0));
  }
  SUBCASE("a known activity is also held to that ceiling") {
    // Cycling would allow 120 m, but the ceiling is 5 m/s x 10 s.
    CHECK(calculateDistancePhoneCouldHaveMoved(
              reading, wholeTimeInOneActivity(PhoneActivity::Cycling, 10000), 5.0)
              .meters == doctest::Approx(50.0));
    CHECK(speedForPhoneActivity(PhoneActivity::Cycling, 5.0) == 5.0);
    CHECK(speedForPhoneActivity(PhoneActivity::Running, 5.0) == 5.0);
    CHECK(speedForPhoneActivity(PhoneActivity::Walking, 5.0) == 3.0);
  }
  SUBCASE("an invalid setting falls back to the default") {
    CHECK(calculateDistancePhoneCouldHaveMoved(
              reading, wholeTimeInOneActivity(PhoneActivity::Unknown, 10000), -1.0)
              .meters == doctest::Approx(450.0));
    CHECK(calculateDistancePhoneCouldHaveMoved(
              reading, wholeTimeInOneActivity(PhoneActivity::Unknown, 10000), 0.0)
              .meters == doctest::Approx(450.0));
  }
}

TEST_CASE("uncertainty: top speed per activity") {
  CHECK(speedForPhoneActivity(PhoneActivity::StandingStill, 45.0) == 0.5);
  CHECK(speedForPhoneActivity(PhoneActivity::Walking, 45.0) == 3.0);
  CHECK(speedForPhoneActivity(PhoneActivity::Running, 45.0) == 8.0);
  CHECK(speedForPhoneActivity(PhoneActivity::Cycling, 45.0) == 12.0);
  CHECK(speedForPhoneActivity(PhoneActivity::InVehicle, 45.0) == 45.0);
  CHECK(speedForPhoneActivity(PhoneActivity::Unknown, 45.0) == 45.0);
}
