// Tests for the movement uncertainty model.
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

// Builds the score input for a reading, using the model's estimate.
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

}  // namespace

TEST_CASE("MOT-002 stationary for 30 seconds allows a small drift (model part)") {
  const DistancePhoneCouldHaveMoved estimate =
      calculateDistancePhoneCouldHaveMoved(readingWithoutSpeed(), 30000, PhoneActivity::StandingStill);
  CHECK(estimate.meters == doctest::Approx(15.0));  // 0.5 m/s x 30 s
  CHECK(estimate.howItWasCalculated == HowDistanceWasCalculated::FromPhoneActivity);
  CHECK_FALSE(estimate.weOnlyAssumedTheWorst());
}

TEST_CASE("MOT-005 vehicle for 10 seconds") {
  const DistancePhoneCouldHaveMoved estimate =
      calculateDistancePhoneCouldHaveMoved(readingWithoutSpeed(), 10000, PhoneActivity::InVehicle);
  CHECK(estimate.meters == doctest::Approx(450.0));  // 45 m/s x 10 s
}

TEST_CASE("MOT-006 walking for 5 seconds") {
  const DistancePhoneCouldHaveMoved estimate =
      calculateDistancePhoneCouldHaveMoved(readingWithoutSpeed(), 5000, PhoneActivity::Walking);
  CHECK(estimate.meters == doctest::Approx(15.0));  // 3 m/s x 5 s
}

TEST_CASE("MOT-007 running for 5 seconds") {
  const DistancePhoneCouldHaveMoved estimate =
      calculateDistancePhoneCouldHaveMoved(readingWithoutSpeed(), 5000, PhoneActivity::Running);
  CHECK(estimate.meters == doctest::Approx(40.0));  // 8 m/s x 5 s
}

TEST_CASE("MOT-008 cycling for 5 seconds") {
  const DistancePhoneCouldHaveMoved estimate =
      calculateDistancePhoneCouldHaveMoved(readingWithoutSpeed(), 5000, PhoneActivity::Cycling);
  CHECK(estimate.meters == doctest::Approx(60.0));  // 12 m/s x 5 s
}

TEST_CASE("MOT-009 nothing known for 5 seconds uses the worst case") {
  const DistancePhoneCouldHaveMoved estimate =
      calculateDistancePhoneCouldHaveMoved(readingWithoutSpeed(), 5000, PhoneActivity::Unknown);
  CHECK(estimate.meters == doctest::Approx(225.0));  // 45 m/s x 5 s
  CHECK(estimate.howItWasCalculated == HowDistanceWasCalculated::AssumingFastestPossibleSpeed);
  CHECK(estimate.weOnlyAssumedTheWorst());
}

TEST_CASE("MOT-016 knowing the phone is stationary does not make an imprecise reading precise") {
  const LocationReading imprecise = readingWithoutSpeed(1000.0);
  const DistancePhoneCouldHaveMoved estimate =
      calculateDistancePhoneCouldHaveMoved(imprecise, 2000, PhoneActivity::StandingStill);

  // The reading's own 1000 m error is still there.
  CHECK(calculateHowFarPhoneCouldBeFromReading(imprecise, estimate) == doctest::Approx(1001.0));

  const ScoreResult score = calculateQualityScore(scoreInputFor(imprecise, 2000, estimate));
  CHECK(score.wasLimitedBy(ScoreLimit::UncertaintyAtLeast300));
  CHECK(score.qualityScore <= 15);
}

TEST_CASE("MOT-026 the speed reported by the GPS narrows the estimate") {
  // Reported 0.3 m/s with 0.5 m/s error: pessimistic speed is 1.3 m/s.
  const LocationReading almostStill = readingWithSpeed(0.3, 0.5);

  SUBCASE("3 seconds old") {
    const DistancePhoneCouldHaveMoved estimate =
        calculateDistancePhoneCouldHaveMoved(almostStill, 3000, PhoneActivity::Unknown);
    // 1.3 x 3 + 2.5 x 3 x 3 / 2 = 3.9 + 11.25
    CHECK(estimate.meters == doctest::Approx(15.15));
    CHECK(estimate.howItWasCalculated == HowDistanceWasCalculated::FromSpeedReportedByGps);
    CHECK_FALSE(estimate.weOnlyAssumedTheWorst());
  }
  SUBCASE("10 seconds old") {
    const DistancePhoneCouldHaveMoved estimate =
        calculateDistancePhoneCouldHaveMoved(almostStill, 10000, PhoneActivity::Unknown);
    // 1.3 x 10 + 2.5 x 10 x 10 / 2 = 13 + 125
    CHECK(estimate.meters == doctest::Approx(138.0));
  }
  SUBCASE("it is far below the worst case") {
    const double withSpeed =
        calculateDistancePhoneCouldHaveMoved(almostStill, 3000, PhoneActivity::Unknown).meters;
    const double worstCase =
        calculateDistancePhoneCouldHaveMoved(readingWithoutSpeed(), 3000, PhoneActivity::Unknown)
            .meters;
    CHECK(worstCase == doctest::Approx(135.0));
    CHECK(withSpeed < worstCase);
  }
}

TEST_CASE("MOT-027 a speed without its error is not trusted") {
  LocationReading speedOnly = readingWithoutSpeed();
  speedOnly.speedMetersPerSecond = 0.0;  // "standing still", but no error estimate

  const DistancePhoneCouldHaveMoved estimate =
      calculateDistancePhoneCouldHaveMoved(speedOnly, 3000, PhoneActivity::Unknown);
  CHECK(estimate.meters == doctest::Approx(135.0));
  CHECK(estimate.howItWasCalculated == HowDistanceWasCalculated::AssumingFastestPossibleSpeed);

  SUBCASE("an error without a speed is not enough either") {
    LocationReading errorOnly = readingWithoutSpeed();
    errorOnly.speedAccuracyMetersPerSecond = 0.5;
    CHECK(calculateDistancePhoneCouldHaveMoved(errorOnly, 3000, PhoneActivity::Unknown).howItWasCalculated ==
          HowDistanceWasCalculated::AssumingFastestPossibleSpeed);
  }
}

TEST_CASE("MOT-028 the estimate from reported speed never exceeds the worst case") {
  // Reported 40 m/s with 5 m/s error, 10 seconds: 50 x 10 + 125 = 625 m,
  // but nothing is allowed to travel farther than 45 x 10 = 450 m.
  const LocationReading fast = readingWithSpeed(40.0, 5.0);
  const DistancePhoneCouldHaveMoved estimate = calculateDistancePhoneCouldHaveMoved(fast, 10000, PhoneActivity::Unknown);
  CHECK(estimate.meters == doctest::Approx(450.0));
  CHECK(estimate.howItWasCalculated == HowDistanceWasCalculated::FromSpeedReportedByGps);
}

TEST_CASE("MOT-029 the estimate never shrinks as the reading gets older") {
  DeterministicRng rng(0x4D4F543239ULL);
  for (int i = 0; i < 3000; ++i) {
    const bool hasSpeed = rng.below(2) == 0;
    const LocationReading reading =
        hasSpeed ? readingWithSpeed(rng.uniform(0.0, 50.0), rng.uniform(0.0, 5.0))
                 : readingWithoutSpeed();
    const PhoneActivity kind = static_cast<PhoneActivity>(rng.below(6));

    const std::int64_t age = static_cast<std::int64_t>(rng.below(60000));
    const std::int64_t olderAge = age + static_cast<std::int64_t>(rng.below(60000));

    const double now = calculateDistancePhoneCouldHaveMoved(reading, age, kind).meters;
    const double later = calculateDistancePhoneCouldHaveMoved(reading, olderAge, kind).meters;

    REQUIRE(now >= 0.0);
    REQUIRE(later >= now);
    // Never beyond the worst case either.
    REQUIRE(later <= 45.0 * static_cast<double>(olderAge) / 1000.0 + 1e-9);
  }
}

TEST_CASE("MOT-030 losing the reported speed never lowers the estimate nor raises the score") {
  DeterministicRng rng(0x4D4F543330ULL);
  for (int i = 0; i < 3000; ++i) {
    const double accuracy = rng.uniform(0.0, 60.0);
    const LocationReading withSpeed =
        readingWithSpeed(rng.uniform(0.0, 50.0), rng.uniform(0.0, 5.0), accuracy);
    const LocationReading withoutSpeed = readingWithoutSpeed(accuracy);
    const std::int64_t age = static_cast<std::int64_t>(rng.below(30000));

    const DistancePhoneCouldHaveMoved informed =
        calculateDistancePhoneCouldHaveMoved(withSpeed, age, PhoneActivity::Unknown);
    const DistancePhoneCouldHaveMoved uninformed =
        calculateDistancePhoneCouldHaveMoved(withoutSpeed, age, PhoneActivity::Unknown);

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

    const DistancePhoneCouldHaveMoved stationary =
        calculateDistancePhoneCouldHaveMoved(reading, age, PhoneActivity::StandingStill);
    const DistancePhoneCouldHaveMoved unknown = calculateDistancePhoneCouldHaveMoved(reading, age, PhoneActivity::Unknown);

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
      reading, calculateDistancePhoneCouldHaveMoved(reading, 1000, PhoneActivity::Walking));
  const double atTwentySeconds = calculateHowFarPhoneCouldBeFromReading(
      reading, calculateDistancePhoneCouldHaveMoved(reading, 20000, PhoneActivity::Walking));

  CHECK(atOneSecond == doctest::Approx(13.0));      // 10 m + 3 m
  CHECK(atTwentySeconds == doctest::Approx(70.0));  // 10 m + 60 m
  CHECK(atTwentySeconds >= atOneSecond);
}

TEST_CASE("uncertainty: a known phone activity is used even when the GPS reported a speed") {
  // Option 1 comes before option 2: walking 3 m/s x 3 s = 9 m,
  // not the 15.15 m that the GPS speed would give.
  const LocationReading almostStopped = readingWithSpeed(0.3, 0.5);
  const DistancePhoneCouldHaveMoved moved =
      calculateDistancePhoneCouldHaveMoved(almostStopped, 3000, PhoneActivity::Walking);
  CHECK(moved.meters == doctest::Approx(9.0));
  CHECK(moved.howItWasCalculated == HowDistanceWasCalculated::FromPhoneActivity);
}

TEST_CASE("uncertainty: a reading taken right now could not have moved") {
  CHECK(calculateDistancePhoneCouldHaveMoved(readingWithoutSpeed(), 0, PhoneActivity::Unknown)
            .meters == 0.0);
  CHECK(calculateDistancePhoneCouldHaveMoved(readingWithSpeed(10.0, 1.0), 0, PhoneActivity::Unknown)
            .meters == 0.0);

  // A negative age makes no sense and is treated as zero.
  CHECK(calculateDistancePhoneCouldHaveMoved(readingWithoutSpeed(), -5000, PhoneActivity::InVehicle)
            .meters == 0.0);
}

TEST_CASE("uncertainty: the app can change the fastest plausible speed") {
  // Couriers on foot only: nothing faster than 5 m/s.
  SUBCASE("it changes the worst case and the vehicle speed") {
    CHECK(calculateDistancePhoneCouldHaveMoved(readingWithoutSpeed(), 10000, PhoneActivity::Unknown, 5.0)
              .meters == doctest::Approx(50.0));
    CHECK(calculateDistancePhoneCouldHaveMoved(readingWithoutSpeed(), 10000, PhoneActivity::InVehicle, 5.0)
              .meters == doctest::Approx(50.0));
  }
  SUBCASE("a known activity is also held to that ceiling") {
    // Cycling would allow 120 m, but the ceiling is 5 m/s x 10 s.
    CHECK(calculateDistancePhoneCouldHaveMoved(readingWithoutSpeed(), 10000, PhoneActivity::Cycling, 5.0)
              .meters == doctest::Approx(50.0));
  }
  SUBCASE("an invalid setting falls back to the default") {
    CHECK(calculateDistancePhoneCouldHaveMoved(readingWithoutSpeed(), 10000, PhoneActivity::Unknown, -1.0)
              .meters == doctest::Approx(450.0));
    CHECK(calculateDistancePhoneCouldHaveMoved(readingWithoutSpeed(), 10000, PhoneActivity::Unknown, 0.0)
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
