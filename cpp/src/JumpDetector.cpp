// =============================================================================
// THE ORDER OF THE CALLS IN THIS FILE
// =============================================================================
//
//   checkAllReadingsForGpsJumps(history)                   <- everything starts here
//     |
//     |-- for each reading of the history ("this reading")
//     |      |
//     |      |-- for each OTHER reading                    STEP 1: compare two by two
//     |      |      |-- readingsContradictEachOther()
//     |      |      |      |-- replaceInvalidSpeedWithDefault()  (lives in UncertaintyModel.cpp)
//     |      |      |      |-- distanceBetweenMeters()           (lives in GeoMath.cpp)
//     |      |      |      `-- calculateLongestDistancePossibleBetweenReadings()
//     |      |      |
//     |      |      |-- if they contradict:   readingsAgainstIt + 1
//     |      |      `-- else:                 readingsOnItsSide + 1
//     |      |
//     |      |-- if nothing is against it                  STEP 2: give the label
//     |      |      `-- LooksFine
//     |      |-- else if more against it than on its side
//     |      |      `-- ProbablyGpsJump
//     |      `-- else
//     |             `-- InConflict
//     |
//     `-- returns one label for each reading, plus the totals
//
// =============================================================================
// EXAMPLES, STEP BY STEP
// In all of them the GPS error of every reading is 5 meters.
// =============================================================================
//
// EXAMPLE 1 - one pair that is fine: a courier on a motorbike
//
//   Reading A: taken at second 0
//   Reading B: taken at second 2, 30 meters away from A
//
//   readingsContradictEachOther(A, B)
//     metersBetweenReadings                     =  30
//     calculateLongestDistancePossibleBetweenReadings(A, B, 45)
//       secondsBetweenReadings                  =  2
//       distanceAtFastestSpeed = 45 x 2         =  90
//       gpsErrorOfBothReadings = 5 + 5          =  10
//                                               -> 100
//     is 30 bigger than 100?                       no
//     returns false: they do NOT contradict each other
//
// EXAMPLE 2 - one pair that is impossible
//
//   Reading A: taken at second 0
//   Reading B: taken at second 2, 450 meters away from A
//
//   readingsContradictEachOther(A, B)
//     metersBetweenReadings                     =  450
//     longest distance possible (same as above) =  100
//     is 450 bigger than 100?                      YES
//     returns true: they contradict each other
//
// EXAMPLE 3 - three readings: the GPS jumped away and came back
//
//   A: second 0, at the delivery address
//   B: second 2, 450 meters away
//   C: second 4, back at the address
//
//   Step 1, the pairs:
//     A and B    450 meters apart, longest possible 100    contradict
//     B and C    450 meters apart, longest possible 100    contradict
//     A and C      0 meters apart, longest possible 190    fine
//
//   Step 2, the counting:
//          on its side     against it     label
//     A    2  (A and C)    1  (B)         InConflict
//     B    1  (only B)     2  (A and C)   ProbablyGpsJump
//     C    2  (C and A)    1  (B)         InConflict
//
//   B is alone against two readings that agree, so B is the suspect.
//   A and C are not "fine" either: a reading that contradicts them exists,
//   and we are not 100% sure it is the wrong one.
//
// EXAMPLE 4 - only two readings, and they contradict each other
//
//          on its side     against it     label
//     A    1  (only A)     1  (B)         InConflict
//     B    1  (only B)     1  (A)         InConflict
//
//   One against one: nothing tells us which of the two is wrong.
//
//   The same idea with three readings that are all far from each other:
//   each one has 1 on its side and 2 against it, so all three get
//   ProbablyGpsJump. When nothing agrees with anything, nothing is trusted.
//
// EXAMPLE 5 - the old reading is the wrong one
//
//   A: second 0.   B1, B2, B3: seconds 2, 3 and 4, all 450 meters from A.
//
//          on its side     against it     label
//     A    1               3              ProbablyGpsJump
//     B1   3               1              InConflict
//     B2   3               1              InConflict
//     B3   3               1              InConflict
//
//   The newest reading is not always the suspect. The side with fewer
//   readings is.
//
// =============================================================================
// WHAT THE LABELS ARE USED FOR
// =============================================================================
//
//   LooksFine         nothing changes.
//   InConflict        the reading can still be used, but with less trust:
//                     the quality score gets a ceiling ("readings disagree").
//   ProbablyGpsJump   the reading is not trusted: the quality score gets a
//                     very low ceiling and the engine answers "cannot tell"
//                     instead of "inside" or "outside".
//
// This file only gives the labels. Turning them into a score is the job of
// the candidate selector, which is the next piece to be written.
// =============================================================================

#include "JumpDetector.hpp"

#include "GeoMath.hpp"

namespace lce {

// =============================================================================
// HELPERS - used only inside this file
// =============================================================================
namespace {

// The longest distance the phone could have moved between two readings,
// counting also that both readings may be a little wrong.
//
// Example: taken 2 seconds apart, GPS errors 5 and 5.
//   distance at the fastest speed = 45 x 2 = 90 meters
//   GPS error of both readings    = 5 + 5  = 10 meters
//   answer                        = 90 + 10 = 100 meters
double calculateLongestDistancePossibleBetweenReadings(const LocationReading& oneReading,
                                                       const LocationReading& otherReading,
                                                       double fastestPossibleSpeed) noexcept {
  // The time between the two readings. It does not matter which one was
  // taken first, so a negative difference is turned into a positive one.
  double millisecondsBetweenReadings = static_cast<double>(otherReading.capturedAtMs) -
                                       static_cast<double>(oneReading.capturedAtMs);
  if (millisecondsBetweenReadings < 0.0) {
    millisecondsBetweenReadings = -millisecondsBetweenReadings;
  }
  const double secondsBetweenReadings = millisecondsBetweenReadings / 1000.0;

  // When both readings were taken at the same moment this is zero, and only
  // the GPS errors are left. Nothing is divided, so zero seconds is safe.
  const double distanceAtFastestSpeed = fastestPossibleSpeed * secondsBetweenReadings;

  const double gpsErrorOfBothReadings =
      oneReading.horizontalAccuracyMeters + otherReading.horizontalAccuracyMeters;

  return distanceAtFastestSpeed + gpsErrorOfBothReadings;
}

}  // namespace

// =============================================================================
// FUNCTIONS OTHER FILES CAN CALL
//
// These two are announced in JumpDetector.hpp. The main one is
// checkAllReadingsForGpsJumps().
// =============================================================================

bool readingsContradictEachOther(const LocationReading& oneReading,
                                 const LocationReading& otherReading,
                                 double fastestPossibleSpeedMetersPerSecond) noexcept {
  const double fastestPossibleSpeed =
      replaceInvalidSpeedWithDefault(fastestPossibleSpeedMetersPerSecond);

  const double metersBetweenReadings =
      distanceBetweenMeters(oneReading.position, otherReading.position);

  const double longestDistancePossible =
      calculateLongestDistancePossibleBetweenReadings(oneReading, otherReading,
                                                      fastestPossibleSpeed);

  // "Bigger than", not "bigger or equal": a distance exactly at the limit is
  // still possible, so it is not a contradiction.
  return metersBetweenReadings > longestDistancePossible;
}

JumpCheckOfAllReadings checkAllReadingsForGpsJumps(
    const LocationHistory& history,
    double fastestPossibleSpeedMetersPerSecond) noexcept {
  JumpCheckOfAllReadings all;
  all.howManyReadings = history.size();

  for (std::size_t thisPosition = 0; thisPosition < history.size(); ++thisPosition) {
    const LocationReading& thisReading = history.at(thisPosition);

    // Starts with 1 on its side (the reading itself) and 0 against it.
    JumpCheckOfOneReading check;

    // Step 1: compare this reading with every other reading.
    for (std::size_t otherPosition = 0; otherPosition < history.size(); ++otherPosition) {
      const bool itIsTheSameReading = (otherPosition == thisPosition);
      if (itIsTheSameReading) {
        continue;  // a reading is not compared with itself
      }

      const LocationReading& otherReading = history.at(otherPosition);

      const bool theyContradictEachOther = readingsContradictEachOther(
          thisReading, otherReading, fastestPossibleSpeedMetersPerSecond);

      if (theyContradictEachOther) {
        check.readingsAgainstIt += 1;

        const bool theyCameFromDifferentProviders =
            (thisReading.provider != otherReading.provider);
        if (theyCameFromDifferentProviders) {
          all.readingsFromDifferentProvidersContradict = true;
        }
      } else {
        check.readingsOnItsSide += 1;
      }
    }

    // Step 2: give the label.
    const bool nothingIsAgainstIt = (check.readingsAgainstIt == 0);
    const bool moreAgainstItThanOnItsSide = (check.readingsAgainstIt > check.readingsOnItsSide);

    if (nothingIsAgainstIt) {
      check.result = JumpCheckResult::LooksFine;

    } else if (moreAgainstItThanOnItsSide) {
      check.result = JumpCheckResult::ProbablyGpsJump;
      all.howManyAreProbablyGpsJumps += 1;

    } else {
      check.result = JumpCheckResult::InConflict;
      all.howManyAreInConflict += 1;
    }

    all.ofEachReading[thisPosition] = check;
  }

  return all;
}

}  // namespace lce
