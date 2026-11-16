// Tests of the reading selection (ReadingSelection.cpp): of all the readings
// we remember, which one is used?
//
// In every test the starting point is latitude 0, longitude 0, and the other
// positions are some meters north of it.
#include "third_party/doctest.h"

#include <cstdint>

#include "GeoMath.hpp"
#include "JumpDetector.hpp"
#include "LocationHistory.hpp"
#include "PhoneActivityTimeline.hpp"
#include "ReadingSelection.hpp"
#include "TestSupport.hpp"
#include "UncertaintyModel.hpp"

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

// The usual rules: readings up to 60 seconds old, fastest speed 45.
const RulesForChoosingReading USUAL_RULES{};

ChosenReading chooseAt(double secondsNow,
                       const LocationHistory& history,
                       const PhoneActivityHistory& phoneActivity = PhoneActivityHistory{},
                       const RulesForChoosingReading& rules = USUAL_RULES) {
  return chooseBestReading(history, phoneActivity, secondsToMs(secondsNow), rules);
}

// A reading that came with a speed from the GPS.
LocationReading readingNorthWithSpeed(double seconds,
                                      double metersNorth,
                                      double gpsSpeed,
                                      double gpsSpeedError,
                                      double gpsErrorMeters = 5.0) {
  LocationReading reading = readingNorth(seconds, metersNorth, gpsErrorMeters);
  reading.speedMetersPerSecond = gpsSpeed;
  reading.speedAccuracyMetersPerSecond = gpsSpeedError;
  return reading;
}

bool samePlace(const LocationReading& one, const LocationReading& other) {
  return one.position.latitudeDegrees == other.position.latitudeDegrees &&
         one.position.longitudeDegrees == other.position.longitudeDegrees;
}

}  // namespace

// --- The examples at the top of ReadingSelection.cpp -----------------------------

TEST_CASE("HIS-018 nothing known: the new imprecise reading beats the old precise one") {
  const LocationHistory history = historyWith({
      readingNorth(0, 0, 5),     // A: old and precise
      readingNorth(49, 0, 200),  // B: new and imprecise
  });

  const ChosenReading chosen = chooseAt(50, history);

  REQUIRE(chosen.found);
  CHECK(chosen.best.reading.capturedAtMs == 49000);
  CHECK(chosen.best.ageMs == 1000);
  CHECK(chosen.best.distanceMoved.meters == doctest::Approx(45.0));     // 45 x 1
  CHECK(chosen.best.howFarPhoneCouldBeMeters == doctest::Approx(245.0));  // 200 + 45
  CHECK(chosen.howManyReadingsWereExamined == 2);
}

TEST_CASE("HIS-018 the phone stood still: the old precise reading beats the new one") {
  const LocationHistory history = historyWith({
      readingNorth(0, 0, 5),
      readingNorth(49, 0, 200),
  });

  const ChosenReading chosen =
      chooseAt(50, history, phoneReported(PhoneActivity::StandingStill, 0, 50));

  REQUIRE(chosen.found);
  CHECK(chosen.best.reading.capturedAtMs == 0);
  CHECK(chosen.best.distanceMoved.meters == doctest::Approx(25.0));    // 0.5 x 50
  CHECK(chosen.best.howFarPhoneCouldBeMeters == doctest::Approx(30.0));  // 5 + 25
  CHECK(chosen.best.timeInEachActivity.standingStillTheWholeTime());
}

TEST_CASE("HIS-019 a new precise reading beats an old imprecise one") {
  const LocationHistory history = historyWith({
      readingNorth(0, 0, 200),  // 40 seconds old at second 40
      readingNorth(38, 0, 5),   // 2 seconds old
  });

  const ChosenReading chosen = chooseAt(40, history);

  REQUIRE(chosen.found);
  CHECK(chosen.best.reading.capturedAtMs == 38000);
  CHECK(chosen.best.howFarPhoneCouldBeMeters == doctest::Approx(95.0));  // 5 + 45 x 2
}

TEST_CASE("JMP-001 a reading that is probably a GPS jump is not chosen") {
  const LocationHistory history = historyWith({
      readingNorth(0, 0, 5),    // A: at the door
      readingNorth(2, 450, 5),  // B: 450 meters away
      readingNorth(3, 0, 60),   // C: at the door again, less precise
  });

  const ChosenReading chosen = chooseAt(5, history);

  // B has the smallest "how far" (5 + 45 x 3 = 140), but it is probably a
  // jump. The choice is between A (5 + 225 = 230) and C (60 + 90 = 150).
  REQUIRE(chosen.found);
  CHECK(chosen.best.reading.capturedAtMs == 3000);
  CHECK(chosen.best.howFarPhoneCouldBeMeters == doctest::Approx(150.0));
  CHECK(chosen.best.jumpCheck.result == JumpCheckResult::InConflict);

  CHECK(chosen.howManyAreProbablyGpsJumps == 1);
  CHECK(chosen.someReadingsContradictEachOther);
}

TEST_CASE("HIS-020 MOT-017 readings older than the app allows are never chosen") {
  const LocationHistory history = historyWith({
      readingNorth(10, 0, 5),  // 90 seconds old at second 100
      readingNorth(30, 0, 5),  // 70 seconds old
  });

  // Nothing known about the activity.
  const ChosenReading nothingKnown = chooseAt(100, history);
  CHECK_FALSE(nothingKnown.found);
  CHECK(nothingKnown.howManyReadingsWereExamined == 2);
  CHECK(nothingKnown.howManyWereTooOld == 2);

  // Even a phone that stood still the whole time does not bring them back.
  const ChosenReading standingStill =
      chooseAt(100, history, phoneReported(PhoneActivity::StandingStill, 0, 100));
  CHECK_FALSE(standingStill.found);
}

TEST_CASE("a reading exactly as old as the app allows can still be chosen") {
  const LocationHistory history = historyWith({readingNorth(0, 0, 5)});

  const PhoneActivityHistory nothingReported;

  // 60000 milliseconds old: allowed.
  CHECK(chooseBestReading(history, nothingReported, 60000, USUAL_RULES).found);
  // 60001 milliseconds old: too old.
  CHECK_FALSE(chooseBestReading(history, nothingReported, 60001, USUAL_RULES).found);
}

TEST_CASE("JMP-018 a reported speed that does not fit the movement is ignored") {
  SUBCASE("the GPS says almost stopped, the positions show a fast phone") {
    const LocationHistory history = historyWith({
        readingNorth(0, 0, 5),
        readingNorthWithSpeed(2, 100, 0.3, 0.5),
    });

    const ChosenReading chosen = chooseAt(5, history);

    REQUIRE(chosen.found);
    CHECK(chosen.best.reading.capturedAtMs == 2000);
    CHECK(chosen.best.gpsSpeedDoesNotMatchTheMovement);
    // Without the check the distance would be 15.15 (the speed option).
    CHECK(chosen.best.distanceMoved.meters == doctest::Approx(135.0));  // 45 x 3
    CHECK(chosen.best.distanceMoved.weOnlyAssumedTheWorst());
  }

  SUBCASE("the GPS says fast, the positions show a phone that did not move") {
    const LocationHistory history = historyWith({
        readingNorth(0, 0, 5),
        readingNorthWithSpeed(2, 0, 30.0, 1.0),
    });

    const ChosenReading chosen = chooseAt(5, history);

    REQUIRE(chosen.found);
    CHECK(chosen.best.gpsSpeedDoesNotMatchTheMovement);
    // With the speed it would be (30 + 2) x 3 + 11.25 = 107.25.
    CHECK(chosen.best.distanceMoved.meters == doctest::Approx(135.0));
  }

  SUBCASE("a speed that fits the movement is used") {
    const LocationHistory history = historyWith({
        readingNorth(0, 0, 5),
        readingNorthWithSpeed(2, 1, 0.3, 0.5),
    });

    const ChosenReading chosen = chooseAt(5, history);

    REQUIRE(chosen.found);
    CHECK_FALSE(chosen.best.gpsSpeedDoesNotMatchTheMovement);
    CHECK(chosen.best.distanceMoved.meters == doctest::Approx(15.15));
    CHECK(chosen.best.distanceMoved.howItWasCalculated ==
          HowDistanceWasCalculated::FromSpeedReportedByGps);
  }

  SUBCASE("the first reading has nothing before it to compare with") {
    const LocationHistory history = historyWith({readingNorthWithSpeed(0, 0, 30.0, 1.0)});

    const ChosenReading chosen = chooseAt(1, history);

    REQUIRE(chosen.found);
    CHECK_FALSE(chosen.best.gpsSpeedDoesNotMatchTheMovement);
  }

  SUBCASE("the GPS error of the two positions is taken into account") {
    // The GPS says 10 (at least 9). The positions are 2 meters apart, but
    // each has a GPS error of 10, so they may really be up to 22 meters
    // apart: up to 11 meters per second. That fits.
    const LocationHistory tooFast = historyWith({
        readingNorth(0, 0, 10),
        readingNorthWithSpeed(2, 2, 10.0, 0.5, 10),
    });
    CHECK_FALSE(chooseAt(5, tooFast).best.gpsSpeedDoesNotMatchTheMovement);

    // The GPS says almost stopped (at most 1.3). The positions are 25 meters
    // apart, but they may really be only 5 meters apart: 2.5 meters per
    // second, which the phone can reach from 1.3. That fits too.
    const LocationHistory tooSlow = historyWith({
        readingNorth(0, 0, 10),
        readingNorthWithSpeed(2, 25, 0.3, 0.5, 10),
    });
    CHECK_FALSE(chooseAt(5, tooSlow).best.gpsSpeedDoesNotMatchTheMovement);
  }

  SUBCASE("the phone may have sped up or slowed down a little") {
    // The GPS says exactly 3. The positions show at most (0 + 4) / 2 = 2.
    // The difference of 1 is within what the speed can change in 2 seconds.
    const LocationHistory history = historyWith({
        readingNorth(0, 0, 2),
        readingNorthWithSpeed(2, 0, 3.0, 0.0, 2),
    });
    CHECK_FALSE(chooseAt(5, history).best.gpsSpeedDoesNotMatchTheMovement);
  }

  SUBCASE("the error of the reported speed is taken into account") {
    // The GPS says 6 with an error of 2, so maybe only 6 - 2 x 2 = 2. The
    // positions show at most (0 + 4) / 2 = 2. That fits.
    const LocationHistory history = historyWith({
        readingNorth(0, 0, 2),
        readingNorthWithSpeed(2, 0, 6.0, 2.0, 2),
    });
    CHECK_FALSE(chooseAt(5, history).best.gpsSpeedDoesNotMatchTheMovement);
  }

  SUBCASE("two readings taken at the same moment are never compared") {
    const LocationHistory history = historyWith({
        readingNorth(0, 0, 5),
        readingNorthWithSpeed(0, 100, 0.3, 0.5),
    });

    RulesForChoosingReading onlyTheSecond;
    onlyTheSecond.firstArrivalNumberAllowed = 2;

    const ChosenReading chosen = chooseAt(1, history, phoneReportedNothing(), onlyTheSecond);
    REQUIRE(chosen.found);
    CHECK_FALSE(chosen.best.gpsSpeedDoesNotMatchTheMovement);
  }

  SUBCASE("a speed with no error value is still compared") {
    LocationReading fastWithoutError = readingNorth(2, 0, 5);
    fastWithoutError.speedMetersPerSecond = 30.0;
    const LocationHistory history = historyWith({readingNorth(0, 0, 5), fastWithoutError});

    const ChosenReading chosen = chooseAt(5, history);

    REQUIRE(chosen.found);
    CHECK(chosen.best.gpsSpeedDoesNotMatchTheMovement);
  }
}

// --- GPS jumps ---------------------------------------------------------------------

TEST_CASE("JMP-014 one bad reading at the end does not replace a good series") {
  const LocationHistory history = historyWith({
      readingNorth(0, 0),
      readingNorth(1, 0),
      readingNorth(2, 0),
      readingNorth(3, 1000),  // alone, 1 km away, and the newest
  });

  const ChosenReading chosen = chooseAt(3, history);

  // The newest reading has the smallest number (5 + 0), but three readings
  // contradict it.
  REQUIRE(chosen.found);
  CHECK(chosen.best.reading.capturedAtMs == 2000);
  CHECK(chosen.best.howFarPhoneCouldBeMeters == doctest::Approx(50.0));  // 5 + 45 x 1
  CHECK(chosen.howManyAreProbablyGpsJumps == 1);
}

TEST_CASE("when only probable jumps can be used, the best of them keeps its label") {
  const LocationHistory history = historyWith({
      readingNorth(0, 0),
      readingNorth(1, 0),
      readingNorth(70, 5000),  // 5 km away: both older readings contradict it
  });

  // At second 75 the two good readings are older than 60 seconds.
  const ChosenReading chosen = chooseAt(75, history);

  REQUIRE(chosen.found);
  CHECK(chosen.best.reading.capturedAtMs == 70000);
  CHECK(chosen.best.jumpCheck.result == JumpCheckResult::ProbablyGpsJump);
  CHECK(chosen.howManyWereTooOld == 2);
}

TEST_CASE("JMP-019 readings from different providers that contradict are reported") {
  LocationReading fromTheSystemCache = readingNorth(0, 0);
  fromTheSystemCache.provider = Provider::SystemCache;
  LocationReading fromTheGps = readingNorth(2, 450);
  fromTheGps.provider = Provider::Fused;

  const ChosenReading chosen = chooseAt(3, historyWith({fromTheSystemCache, fromTheGps}));

  // One against one: nobody is labelled a jump, the smaller number wins.
  REQUIRE(chosen.found);
  CHECK(chosen.best.reading.capturedAtMs == 2000);
  CHECK(chosen.best.jumpCheck.result == JumpCheckResult::InConflict);
  CHECK(chosen.readingsFromDifferentProvidersContradict);
  CHECK(chosen.someReadingsContradictEachOther);
  CHECK(chosen.howManyAreProbablyGpsJumps == 0);
}

// --- The five rules, one by one -------------------------------------------------------

TEST_CASE("2nd rule: the smaller 'how far the phone could be' wins") {
  const LocationHistory history = historyWith({
      readingNorth(0, 0, 10),  // at second 2: 10 + 90 = 100
      readingNorth(1, 0, 50),  // at second 2: 50 + 45 = 95
  });

  const ChosenReading chosen = chooseAt(2, history);

  REQUIRE(chosen.found);
  CHECK(chosen.best.reading.capturedAtMs == 1000);
  CHECK(chosen.best.howFarPhoneCouldBeMeters == doctest::Approx(95.0));
}

TEST_CASE("3rd rule: with the same 'how far', the newer reading wins") {
  const LocationHistory history = historyWith({
      readingNorth(0, 0, 95),   // at second 2: 95 + 90 = 185
      readingNorth(1, 0, 140),  // at second 2: 140 + 45 = 185
  });

  const ChosenReading chosen = chooseAt(2, history);

  REQUIRE(chosen.found);
  CHECK(chosen.best.reading.capturedAtMs == 1000);
  CHECK(chosen.best.howFarPhoneCouldBeMeters == 185.0);
}

TEST_CASE("4th rule: same 'how far' and same age, the smaller GPS error wins") {
  // Both readings were taken at second 0 and are judged at second 2.
  //   without a speed:  GPS error 30, could have moved 45 x 2 = 90   -> 120
  //   with a speed:     GPS error 50, could have moved
  //                     (30.5 + 2 x 1) x 2 + 2.5 x 2 x 2 / 2 = 70    -> 120
  const LocationReading withoutSpeed = readingNorth(0, 0, 30);
  const LocationReading withSpeed = readingNorthWithSpeed(0, 1, 30.5, 1.0, 50);

  SUBCASE("the one with the smaller GPS error is stored first") {
    const ChosenReading chosen = chooseAt(2, historyWith({withoutSpeed, withSpeed}));
    REQUIRE(chosen.found);
    CHECK(chosen.best.howFarPhoneCouldBeMeters == 120.0);
    CHECK(chosen.best.reading.horizontalAccuracyMeters == 30.0);
  }

  SUBCASE("the one with the smaller GPS error is stored last") {
    const ChosenReading chosen = chooseAt(2, historyWith({withSpeed, withoutSpeed}));
    REQUIRE(chosen.found);
    CHECK(chosen.best.reading.horizontalAccuracyMeters == 30.0);
  }
}

TEST_CASE("5th rule: when everything is equal, the reading that arrived first wins") {
  // Same moment, same GPS error, 1 meter apart.
  const LocationHistory history = historyWith({
      readingNorth(0, 1, 20),
      readingNorth(0, 0, 20),
  });

  const ChosenReading chosen = chooseAt(1, history);

  REQUIRE(chosen.found);
  CHECK(chosen.best.reading.arrivalNumber == 1);
  CHECK(samePlace(chosen.best.reading, readingNorth(0, 1, 20)));
}

// --- What the app allows -----------------------------------------------------------------

TEST_CASE("when the app does not want the phone activity, the reports are ignored") {
  const LocationHistory history = historyWith({readingNorth(0, 0, 5)});
  const PhoneActivityHistory standingStill = phoneReported(PhoneActivity::StandingStill, 0, 10);

  RulesForChoosingReading rules;
  rules.usePhoneActivity = false;

  const ChosenReading chosen = chooseAt(10, history, standingStill, rules);

  REQUIRE(chosen.found);
  CHECK(chosen.best.distanceMoved.meters == doctest::Approx(450.0));  // 45 x 10, not 0.5 x 10
  CHECK(chosen.best.timeInEachActivity.weKnowNothing());
  CHECK(chosen.best.timeInEachActivity.millisecondsUnknown == 10000);
}

TEST_CASE("readings that arrived before the allowed arrival number are left out") {
  const LocationHistory history = historyWith({
      readingNorth(0, 0, 5),   // arrival number 1
      readingNorth(1, 0, 5),   // arrival number 2
      readingNorth(2, 0, 90),  // arrival number 3: the worst of the three
  });

  RulesForChoosingReading rules;

  rules.firstArrivalNumberAllowed = 3;
  const ChosenReading onlyTheLast = chooseAt(2, history, phoneReportedNothing(), rules);
  REQUIRE(onlyTheLast.found);
  CHECK(onlyTheLast.best.reading.arrivalNumber == 3);

  rules.firstArrivalNumberAllowed = 4;
  CHECK_FALSE(chooseAt(2, history, phoneReportedNothing(), rules).found);

  rules.firstArrivalNumberAllowed = 0;
  const ChosenReading anyReading = chooseAt(2, history, phoneReportedNothing(), rules);
  REQUIRE(anyReading.found);
  CHECK(anyReading.best.reading.arrivalNumber == 2);  // 5 + 45 = 50 beats 90
}

TEST_CASE("a slower fastest speed makes every reading closer") {
  const LocationHistory history = historyWith({readingNorth(0, 0, 5)});

  RulesForChoosingReading onFoot;
  onFoot.fastestPossibleSpeedMetersPerSecond = 5.0;

  const ChosenReading chosen = chooseAt(10, history, phoneReportedNothing(), onFoot);

  REQUIRE(chosen.found);
  CHECK(chosen.best.howFarPhoneCouldBeMeters == doctest::Approx(55.0));  // 5 + 5 x 10
}

// --- Edges ---------------------------------------------------------------------------------

TEST_CASE("an empty history gives no reading") {
  const ChosenReading chosen = chooseAt(10, LocationHistory{});

  CHECK_FALSE(chosen.found);
  CHECK(chosen.howManyReadingsWereExamined == 0);
  CHECK(chosen.howManyWereTooOld == 0);
  CHECK_FALSE(chosen.someReadingsContradictEachOther);
}

TEST_CASE("a single reading judged at the moment it was taken") {
  const ChosenReading chosen = chooseAt(7, historyWith({readingNorth(7, 0, 12)}));

  REQUIRE(chosen.found);
  CHECK(chosen.best.ageMs == 0);
  CHECK(chosen.best.distanceMoved.meters == 0.0);
  CHECK(chosen.best.howFarPhoneCouldBeMeters == 12.0);
  CHECK(chosen.best.jumpCheck.result == JumpCheckResult::LooksFine);
  CHECK(chosen.best.jumpCheck.readingsOnItsSide == 1);
}

TEST_CASE("the phone activity of each reading is counted from its own moment") {
  // Standing still only from second 20 on.
  const PhoneActivityHistory phoneActivity = phoneReported(PhoneActivity::StandingStill, 20, 40);
  const LocationHistory history = historyWith({
      readingNorth(10, 0, 5),  // 10 unknown seconds, then 20 standing still
      readingNorth(25, 0, 30),  // 15 seconds standing still
  });

  const ChosenReading chosen = chooseAt(40, history, phoneActivity);

  // The first: 5 + 45 x 10 + 0.5 x 20 = 465. The second: 30 + 0.5 x 15 = 37.5.
  REQUIRE(chosen.found);
  CHECK(chosen.best.reading.capturedAtMs == 25000);
  CHECK(chosen.best.howFarPhoneCouldBeMeters == doctest::Approx(37.5));
}

// --- Properties ------------------------------------------------------------------------------

namespace {

// A history of 1 to 10 readings scattered over 100 seconds and 2 km, with no
// speeds, so the test can redo the numbers with the public functions.
LocationHistory randomHistory(DeterministicRng& rng) {
  LocationHistory history;
  const std::size_t howMany = 1 + static_cast<std::size_t>(rng.below(10));
  for (std::size_t count = 0; count < howMany; ++count) {
    const double seconds = rng.uniform(0.0, 100.0);
    const bool farAway = (rng.below(4) == 0);
    const double metersNorth = farAway ? rng.uniform(0.0, 2000.0) : rng.uniform(0.0, 30.0);
    const double gpsError = rng.uniform(1.0, 120.0);
    history.store(readingNorth(seconds, metersNorth, gpsError), secondsToMs(100));
  }
  return history;
}

PhoneActivityHistory randomPhoneActivity(DeterministicRng& rng) {
  PhoneActivityHistory phoneActivity;
  const std::size_t howMany = static_cast<std::size_t>(rng.below(4));
  for (std::size_t count = 0; count < howMany; ++count) {
    const double from = rng.uniform(0.0, 100.0);
    const double to = from + rng.uniform(0.0, 60.0);
    const PhoneActivity activity = static_cast<PhoneActivity>(rng.below(5));
    phoneActivity.store(activityReport(activity, from, to));
  }
  return phoneActivity;
}

}  // namespace

TEST_CASE("JMP-020 the chosen reading is always one of the measured readings") {
  DeterministicRng rng(20261116);

  for (int round = 0; round < 2000; ++round) {
    const LocationHistory history = randomHistory(rng);
    const PhoneActivityHistory phoneActivity = randomPhoneActivity(rng);
    const double secondsNow = rng.uniform(100.0, 150.0);

    const ChosenReading chosen = chooseAt(secondsNow, history, phoneActivity);
    if (!chosen.found) {
      continue;
    }

    bool isOneOfTheStoredReadings = false;
    for (const LocationReading& stored : history) {
      if (stored.arrivalNumber == chosen.best.reading.arrivalNumber) {
        isOneOfTheStoredReadings = samePlace(stored, chosen.best.reading) &&
                                   stored.capturedAtMs == chosen.best.reading.capturedAtMs &&
                                   stored.horizontalAccuracyMeters ==
                                       chosen.best.reading.horizontalAccuracyMeters;
      }
    }
    REQUIRE(isOneOfTheStoredReadings);
  }
}

TEST_CASE("no usable reading is better than the chosen one") {
  DeterministicRng rng(77001);

  for (int round = 0; round < 2000; ++round) {
    const LocationHistory history = randomHistory(rng);
    const PhoneActivityHistory phoneActivity = randomPhoneActivity(rng);
    const std::int64_t nowMs = secondsToMs(rng.uniform(100.0, 150.0));

    const ChosenReading chosen = chooseBestReading(history, phoneActivity, nowMs, USUAL_RULES);
    const JumpCheckOfAllReadings jumpCheck = checkAllReadingsForGpsJumps(history);

    bool someReadingIsUsable = false;

    for (std::size_t position = 0; position < history.size(); ++position) {
      const LocationReading& reading = history.at(position);
      const bool isTooOld = (ageInMs(reading, nowMs) > USUAL_RULES.oldestAgeAllowedMs);
      if (isTooOld) {
        continue;
      }
      someReadingIsUsable = true;
      REQUIRE(chosen.found);

      // The same numbers, redone with the public functions.
      const TimeInEachPhoneActivity table =
          calculateTimeInEachPhoneActivity(phoneActivity, reading.capturedAtMs, nowMs);
      const double howFar = reading.horizontalAccuracyMeters +
                            calculateDistancePhoneCouldHaveMoved(reading, table).meters;

      const bool thisIsAJump =
          (jumpCheck.ofEachReading[position].result == JumpCheckResult::ProbablyGpsJump);
      const bool chosenIsAJump =
          (chosen.best.jumpCheck.result == JumpCheckResult::ProbablyGpsJump);

      if (chosenIsAJump) {
        // A jump is only chosen when every usable reading is a jump.
        REQUIRE(thisIsAJump);
      }
      if (thisIsAJump == chosenIsAJump) {
        REQUIRE(howFar >= chosen.best.howFarPhoneCouldBeMeters);
      }
    }

    REQUIRE(chosen.found == someReadingIsUsable);
  }
}

TEST_CASE("INV-08 as time passes, the same chosen reading never gets closer") {
  DeterministicRng rng(4242);

  for (int round = 0; round < 2000; ++round) {
    const LocationHistory history = randomHistory(rng);
    const PhoneActivityHistory phoneActivity = randomPhoneActivity(rng);
    const double secondsNow = rng.uniform(100.0, 140.0);
    const double secondsLater = secondsNow + rng.uniform(0.0, 20.0);

    const ChosenReading now = chooseAt(secondsNow, history, phoneActivity);
    const ChosenReading later = chooseAt(secondsLater, history, phoneActivity);

    const bool sameReadingBothTimes =
        now.found && later.found &&
        now.best.reading.arrivalNumber == later.best.reading.arrivalNumber;
    if (sameReadingBothTimes) {
      REQUIRE(later.best.howFarPhoneCouldBeMeters >= now.best.howFarPhoneCouldBeMeters);
      REQUIRE(later.best.ageMs >= now.best.ageMs);
    }
  }
}
