// Tests for the GPS jump detector.
#include "third_party/doctest.h"

#include <cstdint>
#include <initializer_list>
#include <limits>

#include "GeoMath.hpp"
#include "JumpDetector.hpp"
#include "LocationHistory.hpp"
#include "TestSupport.hpp"

using namespace lce;
using lce_test::DeterministicRng;
using lce_test::readingAt;

using Label = JumpCheckResult;

namespace {

// A reading taken `seconds` after the start, `metersNorth` meters north of
// the starting point.
LocationReading readingNorth(double seconds, double metersNorth, double gpsErrorMeters = 5.0) {
  const double pi = 3.14159265358979323846;
  const double degreesPerMeter = 180.0 / (pi * EARTH_RADIUS_METERS);
  return readingAt(static_cast<std::int64_t>(seconds * 1000.0), metersNorth * degreesPerMeter,
                   0.0, gpsErrorMeters);
}

// A history holding these readings. "Now" is the moment of the newest one,
// so nothing is thrown away for being old.
LocationHistory historyWith(std::initializer_list<LocationReading> readings) {
  std::int64_t newestCaptureMs = 0;
  for (const LocationReading& reading : readings) {
    if (reading.capturedAtMs > newestCaptureMs) {
      newestCaptureMs = reading.capturedAtMs;
    }
  }
  LocationHistory history;
  for (const LocationReading& reading : readings) {
    history.store(reading, newestCaptureMs);
  }
  return history;
}

Label labelAt(const JumpCheckOfAllReadings& all, std::size_t position) {
  return all.ofEachReading[position].result;
}

}  // namespace

// --- One pair of readings ------------------------------------------------------

TEST_CASE("JMP-005 a distance exactly at the limit is not a contradiction") {
  // 2 seconds apart: 45 x 2 = 90 meters. The two GPS errors are chosen so
  // that 90 + errors is exactly the distance between the readings.
  LocationReading first = readingNorth(0, 0);
  LocationReading second = readingNorth(2, 120);
  const double meters = distanceBetweenMeters(first.position, second.position);
  REQUIRE(meters >= 45.0);
  REQUIRE(meters <= 180.0);

  const double halfOfTheRest = (meters - 90.0) / 2.0;
  first.horizontalAccuracyMeters = halfOfTheRest;
  second.horizontalAccuracyMeters = halfOfTheRest;
  REQUIRE(90.0 + (halfOfTheRest + halfOfTheRest) == meters);

  CHECK_FALSE(readingsContradictEachOther(first, second));

  // One millimeter less of GPS error and the same distance is too far.
  first.horizontalAccuracyMeters = halfOfTheRest - 0.001;
  CHECK(readingsContradictEachOther(first, second));
}

TEST_CASE("JMP-006 a distance above the limit is a contradiction") {
  // 2 seconds apart, errors 5 and 5: the limit is 45 x 2 + 5 + 5 = 100 meters.
  CHECK(readingsContradictEachOther(readingNorth(0, 0), readingNorth(2, 101)));
  CHECK_FALSE(readingsContradictEachOther(readingNorth(0, 0), readingNorth(2, 99)));

  // Example 2 at the top of JumpDetector.cpp.
  CHECK(readingsContradictEachOther(readingNorth(0, 0), readingNorth(2, 450)));
  // Example 1.
  CHECK_FALSE(readingsContradictEachOther(readingNorth(0, 0), readingNorth(2, 30)));
}

TEST_CASE("JMP-007 two readings taken at the same moment") {
  // Zero seconds between them: only the GPS errors (5 + 5 = 10) are allowed.
  CHECK(readingsContradictEachOther(readingNorth(1, 0), readingNorth(1, 450)));
  CHECK_FALSE(readingsContradictEachOther(readingNorth(1, 0), readingNorth(1, 8)));

  const LocationHistory history = historyWith({readingNorth(1, 0), readingNorth(1, 450)});
  REQUIRE(history.size() == 2);
  const JumpCheckOfAllReadings all = checkAllReadingsForGpsJumps(history);
  CHECK(labelAt(all, 0) == Label::InConflict);
  CHECK(labelAt(all, 1) == Label::InConflict);
}

TEST_CASE("JMP-008 the order of the two readings does not matter") {
  const LocationReading early = readingNorth(0, 0);
  const LocationReading lateAndFar = readingNorth(2, 450);
  const LocationReading lateAndNear = readingNorth(2, 30);

  CHECK(readingsContradictEachOther(early, lateAndFar));
  CHECK(readingsContradictEachOther(lateAndFar, early));
  CHECK_FALSE(readingsContradictEachOther(early, lateAndNear));
  CHECK_FALSE(readingsContradictEachOther(lateAndNear, early));

  // Readings that arrive late are put in time order by the history, so the
  // labels are the same as when they arrive in order.
  LocationHistory history;
  CHECK(history.store(readingNorth(4, 0), 4000) == LocationHistory::StoreResult::Stored);
  CHECK(history.store(readingNorth(2, 450), 4000) ==
        LocationHistory::StoreResult::StoredOutOfOrder);
  CHECK(history.store(readingNorth(0, 0), 4000) ==
        LocationHistory::StoreResult::StoredOutOfOrder);

  const JumpCheckOfAllReadings all = checkAllReadingsForGpsJumps(history);
  CHECK(labelAt(all, 0) == Label::InConflict);
  CHECK(labelAt(all, 1) == Label::ProbablyGpsJump);
  CHECK(labelAt(all, 2) == Label::InConflict);
}

TEST_CASE("JMP-011 a teleport of 4 km in one second") {
  CHECK(readingsContradictEachOther(readingNorth(0, 0), readingNorth(1, 4000)));
}

TEST_CASE("JMP-016 crossing the date line is not a jump") {
  // Longitude 179.9999 and -179.9999 are about 22 meters apart, not a whole
  // turn around the planet.
  const LocationReading west = readingAt(0, 0.0, 179.9999, 5.0);
  const LocationReading east = readingAt(1000, 0.0, -179.9999, 5.0);
  CHECK(distanceBetweenMeters(west.position, east.position) < 30.0);
  CHECK_FALSE(readingsContradictEachOther(west, east));
}

TEST_CASE("JMP-017 near the north pole a big change of longitude is a short distance") {
  const LocationReading oneSide = readingAt(0, 89.9999, 0.0, 5.0);
  const LocationReading otherSide = readingAt(1000, 89.9999, 180.0, 5.0);
  CHECK(distanceBetweenMeters(oneSide.position, otherSide.position) < 30.0);
  CHECK_FALSE(readingsContradictEachOther(oneSide, otherSide));
}

TEST_CASE("JMP-023 one millisecond between readings, and extreme times") {
  // 1 millisecond: 45 x 0.001 = 0.045 meters, plus errors 5 + 5.
  const LocationReading first = readingAt(1000, 0.0, 0.0, 5.0);
  LocationReading second = readingNorth(0, 500);
  second.capturedAtMs = 1001;
  CHECK(readingsContradictEachOther(first, second));

  // The largest and smallest times a reading can carry must not break the
  // calculation of the time between them.
  LocationReading veryLate = readingNorth(0, 500);
  veryLate.capturedAtMs = std::numeric_limits<std::int64_t>::max();
  LocationReading veryEarly = readingNorth(0, 0);
  veryEarly.capturedAtMs = std::numeric_limits<std::int64_t>::min();
  CHECK_FALSE(readingsContradictEachOther(veryEarly, veryLate));
  CHECK_FALSE(readingsContradictEachOther(veryLate, veryEarly));
}

TEST_CASE("JMP-024 a tiny variation of the GPS is not a jump") {
  // Less than 1 meter apart, GPS errors of 10 meters.
  CHECK_FALSE(readingsContradictEachOther(readingNorth(0, 0, 10.0), readingNorth(1, 0.8, 10.0)));

  const LocationHistory history =
      historyWith({readingNorth(0, 0.0, 10.0), readingNorth(1, 0.8, 10.0),
                   readingNorth(2, 0.3, 10.0), readingNorth(3, 0.9, 10.0),
                   readingNorth(4, 0.1, 10.0)});
  const JumpCheckOfAllReadings all = checkAllReadingsForGpsJumps(history);
  CHECK(all.howManyReadings == 5);
  CHECK_FALSE(all.someReadingsContradictEachOther());
  for (std::size_t position = 0; position < all.howManyReadings; ++position) {
    CHECK(labelAt(all, position) == Label::LooksFine);
  }
}

TEST_CASE("the fastest possible speed can be changed, and an invalid one becomes 45") {
  const LocationReading first = readingNorth(0, 0);
  const LocationReading second = readingNorth(2, 450);

  // At 300 meters per second, 450 meters in 2 seconds is possible.
  CHECK_FALSE(readingsContradictEachOther(first, second, 300.0));

  // Zero, negative and "not a number" make no sense: the default 45 is used.
  CHECK(readingsContradictEachOther(first, second, 0.0));
  CHECK(readingsContradictEachOther(first, second, -10.0));
  CHECK(readingsContradictEachOther(first, second, std::numeric_limits<double>::quiet_NaN()));
  CHECK(readingsContradictEachOther(first, second, std::numeric_limits<double>::infinity()));

  const LocationHistory history = historyWith({first, second, readingNorth(4, 0)});
  CHECK(checkAllReadingsForGpsJumps(history, 300.0).howManyAreProbablyGpsJumps == 0);
  CHECK(checkAllReadingsForGpsJumps(history, -1.0).howManyAreProbablyGpsJumps == 1);
}

// --- All the readings of a history -------------------------------------------------

TEST_CASE("an empty history and a history with one reading") {
  const JumpCheckOfAllReadings nothing = checkAllReadingsForGpsJumps(LocationHistory{});
  CHECK(nothing.howManyReadings == 0);
  CHECK_FALSE(nothing.someReadingsContradictEachOther());

  const JumpCheckOfAllReadings one = checkAllReadingsForGpsJumps(historyWith({readingNorth(0, 0)}));
  CHECK(one.howManyReadings == 1);
  CHECK(labelAt(one, 0) == Label::LooksFine);
  CHECK(one.ofEachReading[0].readingsOnItsSide == 1);
  CHECK(one.ofEachReading[0].readingsAgainstIt == 0);
}

TEST_CASE("JMP-001 the GPS jumps away and comes back (A-B-A)") {
  // Example 3 at the top of JumpDetector.cpp.
  const LocationHistory history =
      historyWith({readingNorth(0, 0), readingNorth(2, 450), readingNorth(4, 0)});
  const JumpCheckOfAllReadings all = checkAllReadingsForGpsJumps(history);

  CHECK(labelAt(all, 0) == Label::InConflict);
  CHECK(labelAt(all, 1) == Label::ProbablyGpsJump);
  CHECK(labelAt(all, 2) == Label::InConflict);

  CHECK(all.ofEachReading[0].readingsOnItsSide == 2);
  CHECK(all.ofEachReading[0].readingsAgainstIt == 1);
  CHECK(all.ofEachReading[1].readingsOnItsSide == 1);
  CHECK(all.ofEachReading[1].readingsAgainstIt == 2);

  CHECK(all.howManyAreProbablyGpsJumps == 1);
  CHECK(all.howManyAreInConflict == 2);
  CHECK(all.someReadingsContradictEachOther());
}

TEST_CASE("JMP-002 a new place confirmed by several readings is not thrown away (A-B-B-B)") {
  // Example 5 at the top of JumpDetector.cpp.
  const LocationHistory history = historyWith(
      {readingNorth(0, 0), readingNorth(2, 450), readingNorth(3, 455), readingNorth(4, 460)});
  const JumpCheckOfAllReadings all = checkAllReadingsForGpsJumps(history);

  // The old reading is the one alone, so it is the suspect.
  CHECK(labelAt(all, 0) == Label::ProbablyGpsJump);
  CHECK(all.ofEachReading[0].readingsAgainstIt == 3);

  // The new place is kept, but it is not treated as free of doubt.
  for (std::size_t position = 1; position <= 3; ++position) {
    CHECK(labelAt(all, position) == Label::InConflict);
    CHECK(all.ofEachReading[position].readingsOnItsSide == 3);
    CHECK(all.ofEachReading[position].readingsAgainstIt == 1);
  }
}

TEST_CASE("JMP-003 only two readings that contradict: neither is called a jump") {
  // Example 4 at the top of JumpDetector.cpp.
  const JumpCheckOfAllReadings all =
      checkAllReadingsForGpsJumps(historyWith({readingNorth(0, 0), readingNorth(2, 450)}));

  CHECK(labelAt(all, 0) == Label::InConflict);
  CHECK(labelAt(all, 1) == Label::InConflict);
  CHECK(all.howManyAreProbablyGpsJumps == 0);
  CHECK(all.someReadingsContradictEachOther());
}

TEST_CASE("JMP-004 with a GPS error of 300 meters the same jump cannot be proven") {
  // Limit for 2 seconds: 45 x 2 + 300 + 300 = 690 meters. 450 fits.
  const LocationHistory history = historyWith(
      {readingNorth(0, 0, 300.0), readingNorth(2, 450, 300.0), readingNorth(4, 0, 300.0)});
  const JumpCheckOfAllReadings all = checkAllReadingsForGpsJumps(history);

  CHECK_FALSE(all.someReadingsContradictEachOther());
  CHECK(all.howManyAreProbablyGpsJumps == 0);
  for (std::size_t position = 0; position < 3; ++position) {
    CHECK(labelAt(all, position) == Label::LooksFine);
  }
}

TEST_CASE("JMP-009 a motorbike at 15 meters per second is not a jump") {
  const LocationHistory history = historyWith(
      {readingNorth(0, 0), readingNorth(1, 15), readingNorth(2, 30), readingNorth(3, 45),
       readingNorth(4, 60), readingNorth(5, 75), readingNorth(6, 90), readingNorth(7, 105)});
  const JumpCheckOfAllReadings all = checkAllReadingsForGpsJumps(history);
  CHECK(all.howManyReadings == 8);
  CHECK_FALSE(all.someReadingsContradictEachOther());
}

TEST_CASE("JMP-010 a runner at 6 meters per second is not a jump") {
  const LocationHistory history =
      historyWith({readingNorth(0, 0), readingNorth(1, 6), readingNorth(2, 12),
                   readingNorth(3, 18), readingNorth(4, 24), readingNorth(5, 30)});
  CHECK_FALSE(checkAllReadingsForGpsJumps(history).someReadingsContradictEachOther());
}

TEST_CASE("JMP-012 going and coming back through the middle is not a jump") {
  const LocationHistory history =
      historyWith({readingNorth(0, 0), readingNorth(5, 100), readingNorth(10, 200),
                   readingNorth(15, 100), readingNorth(20, 0)});
  const JumpCheckOfAllReadings all = checkAllReadingsForGpsJumps(history);
  CHECK_FALSE(all.someReadingsContradictEachOther());
  CHECK(all.howManyAreProbablyGpsJumps == 0);
}

TEST_CASE("JMP-013 readings that keep switching between two streets (detector part)") {
  // 150 meters apart, one second between them, GPS errors of 10 meters.
  const LocationHistory history =
      historyWith({readingNorth(0, 0, 10.0), readingNorth(1, 150, 10.0),
                   readingNorth(2, 0, 10.0), readingNorth(3, 150, 10.0)});
  const JumpCheckOfAllReadings all = checkAllReadingsForGpsJumps(history);

  // No reading is free of doubt, so none can get a high score later.
  CHECK(all.someReadingsContradictEachOther());
  for (std::size_t position = 0; position < 4; ++position) {
    CHECK(labelAt(all, position) != Label::LooksFine);
  }
}

TEST_CASE("JMP-014 one bad reading at the end of a good series (detector part)") {
  const LocationHistory history =
      historyWith({readingNorth(0, 0), readingNorth(1, 2), readingNorth(2, 1),
                   readingNorth(3, 0), readingNorth(4, 1000)});
  const JumpCheckOfAllReadings all = checkAllReadingsForGpsJumps(history);

  // The newest reading is the suspect, not the good series before it.
  CHECK(labelAt(all, 4) == Label::ProbablyGpsJump);
  CHECK(all.ofEachReading[4].readingsOnItsSide == 1);
  CHECK(all.ofEachReading[4].readingsAgainstIt == 4);
  CHECK(all.howManyAreProbablyGpsJumps == 1);
  for (std::size_t position = 0; position < 4; ++position) {
    CHECK(labelAt(all, position) == Label::InConflict);
  }
}

TEST_CASE("JMP-015 a whole group of readings appears far away") {
  // Three readings at one place, then three readings 2 km away.
  LocationHistory history =
      historyWith({readingNorth(0, 0), readingNorth(1, 1), readingNorth(2, 0),
                   readingNorth(3, 2000), readingNorth(4, 2001), readingNorth(5, 2000)});
  JumpCheckOfAllReadings all = checkAllReadingsForGpsJumps(history);

  // Three against three: nobody is called a jump, and nobody is free of doubt.
  CHECK(all.howManyAreProbablyGpsJumps == 0);
  CHECK(all.howManyAreInConflict == 6);

  // One more reading at the new place: now the old place has fewer readings.
  history.store(readingNorth(6, 2001), 6000);
  all = checkAllReadingsForGpsJumps(history);
  for (std::size_t position = 0; position < 3; ++position) {
    CHECK(labelAt(all, position) == Label::ProbablyGpsJump);
  }
  // The new place can be used, but it is still not free of doubt.
  for (std::size_t position = 3; position < 7; ++position) {
    CHECK(labelAt(all, position) == Label::InConflict);
  }
}

TEST_CASE("JMP-019 readings from different providers that contradict (detector part)") {
  LocationReading storedBySystem = readingNorth(0, 0);
  storedBySystem.provider = Provider::SystemCache;
  LocationReading freshAndFar = readingNorth(2, 450);
  freshAndFar.provider = Provider::Fused;
  LocationReading freshAndNear = readingNorth(2, 30);
  freshAndNear.provider = Provider::Fused;

  // Different providers and a contradiction.
  CHECK(checkAllReadingsForGpsJumps(historyWith({storedBySystem, freshAndFar}))
            .readingsFromDifferentProvidersContradict);

  // Different providers, but no contradiction.
  CHECK_FALSE(checkAllReadingsForGpsJumps(historyWith({storedBySystem, freshAndNear}))
                  .readingsFromDifferentProvidersContradict);

  // A contradiction, but the same provider.
  CHECK_FALSE(checkAllReadingsForGpsJumps(historyWith({readingNorth(0, 0), readingNorth(2, 450)}))
                  .readingsFromDifferentProvidersContradict);
}

TEST_CASE("JMP-021 adding a bad reading never helps the good ones (detector part)") {
  LocationHistory history =
      historyWith({readingNorth(0, 0), readingNorth(1, 1), readingNorth(2, 0)});
  const JumpCheckOfAllReadings before = checkAllReadingsForGpsJumps(history);
  for (std::size_t position = 0; position < 3; ++position) {
    CHECK(labelAt(before, position) == Label::LooksFine);
    CHECK(before.ofEachReading[position].readingsOnItsSide == 3);
  }

  history.store(readingNorth(3, 1000), 3000);
  const JumpCheckOfAllReadings after = checkAllReadingsForGpsJumps(history);
  for (std::size_t position = 0; position < 3; ++position) {
    CHECK(after.ofEachReading[position].readingsOnItsSide == 3);  // did not grow
    CHECK(labelAt(after, position) == Label::InConflict);         // and got worse
  }
  CHECK(labelAt(after, 3) == Label::ProbablyGpsJump);
}

TEST_CASE("JMP-022 the same bad reading delivered ten times does not support itself") {
  LocationHistory history =
      historyWith({readingNorth(0, 0), readingNorth(1, 1), readingNorth(2, 0)});
  for (int delivery = 0; delivery < 10; ++delivery) {
    history.store(readingNorth(3, 1000), 3000);
  }
  // The history keeps a repeated reading only once.
  REQUIRE(history.size() == 4);

  const JumpCheckOfAllReadings all = checkAllReadingsForGpsJumps(history);
  CHECK(labelAt(all, 3) == Label::ProbablyGpsJump);
  CHECK(all.ofEachReading[3].readingsOnItsSide == 1);
  CHECK(all.howManyAreProbablyGpsJumps == 1);
}

TEST_CASE("three readings that all contradict each other are all suspects") {
  const JumpCheckOfAllReadings all = checkAllReadingsForGpsJumps(
      historyWith({readingNorth(0, 0), readingNorth(1, 2000), readingNorth(2, 4000)}));

  // Each one has only itself on its side and two readings against it.
  CHECK(all.howManyAreProbablyGpsJumps == 3);
  CHECK(all.howManyAreInConflict == 0);
  CHECK(all.someReadingsContradictEachOther());
}

TEST_CASE("equal sides is a conflict, one more against is a jump") {
  // Two readings at one place and two readings 2 km away: two against two.
  LocationHistory history = historyWith(
      {readingNorth(0, 0), readingNorth(1, 0.5), readingNorth(2, 2000), readingNorth(3, 2000.5)});
  JumpCheckOfAllReadings all = checkAllReadingsForGpsJumps(history);
  CHECK(all.howManyAreProbablyGpsJumps == 0);
  CHECK(all.howManyAreInConflict == 4);

  // A third reading at the second place: now it is two against three.
  history.store(readingNorth(4, 2001), 4000);
  all = checkAllReadingsForGpsJumps(history);
  CHECK(labelAt(all, 0) == Label::ProbablyGpsJump);
  CHECK(labelAt(all, 1) == Label::ProbablyGpsJump);
  CHECK(all.howManyAreProbablyGpsJumps == 2);
  CHECK(all.howManyAreInConflict == 3);
}

// --- Rules that must hold for any set of readings ----------------------------------

TEST_CASE("property: the counts always add up, and a new reading never clears a doubt") {
  DeterministicRng rng(20261116);

  for (int round = 0; round < 3000; ++round) {
    LocationHistory history;
    const std::size_t howMany = 1 + static_cast<std::size_t>(rng.below(8));
    for (std::size_t index = 0; index < howMany; ++index) {
      // Different capture times, so no reading is a repeat of another.
      const double seconds = static_cast<double>(index) * 2.0 + rng.uniform(0.0, 1.5);
      history.store(readingNorth(seconds, rng.uniform(0.0, 600.0), rng.uniform(3.0, 60.0)),
                    30000);
    }
    REQUIRE(history.size() == howMany);

    const JumpCheckOfAllReadings before = checkAllReadingsForGpsJumps(history);
    REQUIRE(before.howManyReadings == howMany);

    std::size_t withSomethingAgainst = 0;
    std::size_t totalAgainst = 0;
    for (std::size_t position = 0; position < howMany; ++position) {
      const JumpCheckOfOneReading& check = before.ofEachReading[position];

      // Every reading is either on its side or against it.
      CHECK(check.readingsOnItsSide + check.readingsAgainstIt == howMany);
      CHECK(check.readingsOnItsSide >= 1);

      // The label always matches the counts.
      if (check.readingsAgainstIt == 0) {
        CHECK(check.result == Label::LooksFine);
      } else if (check.readingsAgainstIt > check.readingsOnItsSide) {
        CHECK(check.result == Label::ProbablyGpsJump);
      } else {
        CHECK(check.result == Label::InConflict);
      }

      if (check.readingsAgainstIt > 0) {
        withSomethingAgainst += 1;
      }
      totalAgainst += check.readingsAgainstIt;
    }
    CHECK(before.howManyAreInConflict + before.howManyAreProbablyGpsJumps ==
          withSomethingAgainst);
    // "A contradicts B" always comes with "B contradicts A".
    CHECK(totalAgainst % 2 == 0);

    // Add one more reading, newer than all the others.
    history.store(readingNorth(20.0, rng.uniform(0.0, 600.0), rng.uniform(3.0, 60.0)), 30000);
    REQUIRE(history.size() == howMany + 1);
    const JumpCheckOfAllReadings after = checkAllReadingsForGpsJumps(history);

    for (std::size_t position = 0; position < howMany; ++position) {
      // What was against a reading is still against it.
      CHECK(after.ofEachReading[position].readingsAgainstIt >=
            before.ofEachReading[position].readingsAgainstIt);
      // A reading that was in doubt does not become fine.
      if (before.ofEachReading[position].result != Label::LooksFine) {
        CHECK(after.ofEachReading[position].result != Label::LooksFine);
      }
    }
  }
}
