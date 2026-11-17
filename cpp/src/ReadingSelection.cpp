// =============================================================================
// THE ORDER OF THE CALLS IN THIS FILE
// =============================================================================
//
//   chooseBestReading(history, activityHistory, nowMs, rules)
//     |                                                    <- everything starts here
//     |-- checkAllReadingsForGpsJumps()                    STEP 1   (JumpDetector.cpp)
//     |
//     `-- for each reading of the history
//            |-- too old, or not allowed?  ->  skip it
//            |
//            |-- examineReading()                          STEP 2
//            |      |-- calculateTimeInEachPhoneActivity()     (PhoneActivityTimeline.cpp)
//            |      |-- gpsSpeedMatchesTheMovement()           compares with the reading before it
//            |      |-- calculateDistancePhoneCouldHaveMoved() (UncertaintyModel.cpp)
//            |      `-- calculateHowFarPhoneCouldBeFromReading()
//            |
//            `-- firstIsBetterThanSecond()                 STEP 3
//                   is this reading better than the best one so far?
//
// =============================================================================
// EXAMPLES, STEP BY STEP
// Times are written in seconds here. The code works in milliseconds.
// =============================================================================
//
// EXAMPLE 1 - an old precise reading against a new imprecise one
//
//   now is second 50; nothing is known about the phone activity
//   reading A: taken at second 0,  GPS error 5 meters
//   reading B: taken at second 49, GPS error 200 meters
//
//   chooseBestReading()
//     checkAllReadingsForGpsJumps()         -> both look fine
//     examineReading(A)
//       age 50 seconds, activity unknown    -> could have moved 45 x 50 = 2250 meters
//       how far the phone could be          -> 5 + 2250 = 2255 meters
//     examineReading(B)
//       age 1 second, activity unknown      -> could have moved 45 x 1 = 45 meters
//       how far the phone could be          -> 200 + 45 = 245 meters
//     firstIsBetterThanSecond(B, A)         -> true   (245 is less than 2255)
//     returns B
//
// EXAMPLE 2 - the same readings, but the phone was standing still
//
//   the phone reported "standing still" from second 0 to second 50
//
//     examineReading(A)   could have moved 0.5 x 50 = 25    -> 5 + 25    = 30 meters
//     examineReading(B)   could have moved 0.5 x 1  = 0.5   -> 200 + 0.5 = 200.5 meters
//     returns A   (30 is less than 200.5)
//
//   Knowing that the phone did not move makes the old reading useful again.
//
// EXAMPLE 3 - a GPS jump is never the best reading
//
//   now is second 5; nothing is known about the phone activity
//   reading A: second 0, at the door,          GPS error 5
//   reading B: second 2, 450 meters away,      GPS error 5
//   reading C: second 3, at the door again,    GPS error 60
//
//     checkAllReadingsForGpsJumps()
//       A and B contradict, B and C contradict, A and C do not
//       A: 2 on its side, 1 against it   -> InConflict
//       B: 1 on its side, 2 against it   -> ProbablyGpsJump
//       C: 2 on its side, 1 against it   -> InConflict
//     examineReading(A)   5 + 45 x 5 = 230 meters
//     examineReading(B)   5 + 45 x 3 = 140 meters
//     examineReading(C)   60 + 45 x 2 = 150 meters
//     returns C
//
//   B has the smallest number (140), but B is probably a GPS jump. A reading
//   that is probably a jump loses to any reading that is not, so the choice
//   is between A (230) and C (150).
//
// EXAMPLE 4 - every reading is too old
//
//   now is second 100; the app accepts readings up to 60 seconds old
//   reading A: second 10 (90 seconds old),  reading B: second 30 (70 seconds old)
//
//     both are skipped                      -> found = false
//
//   The history keeps readings for 2 minutes, but that does not make them
//   good enough to decide anything.
//
// EXAMPLE 5 - the speed reported by the GPS does not fit the movement
//
//   reading A: second 0, at the door,     GPS error 5
//   reading B: second 2, 100 meters away, GPS error 5, reported speed 0.3 (error 0.5)
//
//     gpsSpeedMatchesTheMovement(A, B)
//       the positions show at least (100 - 10) / 2 = 45 meters per second
//       the GPS says at most 0.3 + 2 x 0.5 = 1.3, plus 2.5 it could have changed
//       1.3 + 2.5 = 3.8 is far below 45      -> false
//     the reported speed of B is ignored when its distance is calculated
//
//   Without this check, B would look like "almost stopped" and get a small
//   distance, although the positions show the phone moving fast.
//
// =============================================================================
// WHAT THE ANSWER IS USED FOR
// =============================================================================
//
// buildReport() in Report.cpp takes the chosen reading and writes the
// final report: is the phone inside the delivery radius, what is the quality
// score, and why.
//
// The engine also calls chooseBestReading() before it stores a new reading in
// a full history, to know which reading must not lose its place.
// =============================================================================

#include "ReadingSelection.hpp"

#include "GeoMath.hpp"

namespace lce {

// =============================================================================
// HELPERS - used only inside this file
// =============================================================================
namespace {

// Does the speed the GPS reported with a reading fit with how far the phone
// really moved since the reading before it?
//
// Two speeds are compared:
//   - the speed the GPS reported, with its error on both sides;
//   - the speed the two positions show: meters between them / seconds between
//     them, with the GPS error of both positions on both sides.
//
// The reported speed is from one instant and the positions give an average,
// so some difference is normal: the phone can speed up or slow down. We allow
// for that with the same number the movement model uses (2.5 meters per
// second, every second).
//
// When the phone did not report a speed, or the two readings were taken at
// the same moment, there is nothing to compare and the answer is "it fits".
bool gpsSpeedMatchesTheMovement(const LocationReading& previousReading,
                                const LocationReading& reading) noexcept {
  const bool gpsReportedASpeed = reading.speedMetersPerSecond.has_value();

  const double secondsBetween =
      static_cast<double>(reading.capturedAtMs - previousReading.capturedAtMs) / 1000.0;
  const bool readingsHaveTimeBetweenThem = (secondsBetween > 0.0);

  bool speedMatches = true;

  if (gpsReportedASpeed && readingsHaveTimeBetweenThem) {
    // The speed the GPS reported, with its error on both sides.
    const double gpsSpeed = *reading.speedMetersPerSecond;
    double gpsSpeedError = 0.0;
    if (reading.speedAccuracyMetersPerSecond.has_value()) {
      gpsSpeedError = *reading.speedAccuracyMetersPerSecond;
    }
    const double errorOnEachSide = HOW_MANY_TIMES_WE_ADD_THE_GPS_SPEED_ERROR * gpsSpeedError;
    const double fastestReportedSpeed = gpsSpeed + errorOnEachSide;
    double slowestReportedSpeed = gpsSpeed - errorOnEachSide;
    if (slowestReportedSpeed < 0.0) {
      slowestReportedSpeed = 0.0;
    }

    // The speed the two positions show.
    const double metersBetween =
        distanceBetweenMeters(previousReading.position, reading.position);
    const double gpsErrorOfBothReadings =
        previousReading.horizontalAccuracyMeters + reading.horizontalAccuracyMeters;
    const double longestDistance = metersBetween + gpsErrorOfBothReadings;
    double shortestDistance = metersBetween - gpsErrorOfBothReadings;
    if (shortestDistance < 0.0) {
      shortestDistance = 0.0;
    }
    const double fastestSpeedThePositionsShow = longestDistance / secondsBetween;
    const double slowestSpeedThePositionsShow = shortestDistance / secondsBetween;

    // How much the speed can change between the average and the instant.
    const double howMuchTheSpeedCanChange =
        SPEED_THE_PHONE_CAN_GAIN_EACH_SECOND * secondsBetween / 2.0;

    const bool reportedSpeedIsTooFast =
        (slowestReportedSpeed - howMuchTheSpeedCanChange > fastestSpeedThePositionsShow);
    const bool reportedSpeedIsTooSlow =
        (fastestReportedSpeed + howMuchTheSpeedCanChange < slowestSpeedThePositionsShow);

    speedMatches = !reportedSpeedIsTooFast && !reportedSpeedIsTooSlow;
  }

  return speedMatches;
}

// Step 2: works out everything we need to know about one reading.
WhatWeKnowAboutReading examineReading(const LocationHistory& history,
                                      std::size_t position,
                                      const JumpCheckOfAllReadings& jumpCheckOfAllReadings,
                                      const PhoneActivityHistory& activityHistory,
                                      std::int64_t nowMs,
                                      const RulesForChoosingReading& rules) noexcept {
  WhatWeKnowAboutReading result;
  result.reading = history.at(position);
  result.ageMs = ageInMs(result.reading, nowMs);
  result.jumpCheck = jumpCheckOfAllReadings.ofEachReading[position];

  // How long the phone spent in each activity since the reading was taken.
  if (rules.usePhoneActivity) {
    result.timeInEachActivity =
        calculateTimeInEachPhoneActivity(activityHistory, result.reading.capturedAtMs, nowMs);
  } else {
    result.timeInEachActivity = wholeTimeInOneActivity(PhoneActivity::Unknown, result.ageMs);
  }

  // Does the speed the GPS reported fit with the reading before this one?
  const bool thereIsAReadingBeforeThisOne = (position > 0);
  if (thereIsAReadingBeforeThisOne) {
    const LocationReading& previousReading = history.at(position - 1);
    result.gpsSpeedDoesNotMatchTheMovement =
        !gpsSpeedMatchesTheMovement(previousReading, result.reading);
  }

  // A speed that does not fit is not trusted: the distance is calculated
  // from a copy of the reading that has no speed.
  LocationReading readingUsedForTheDistance = result.reading;
  if (result.gpsSpeedDoesNotMatchTheMovement) {
    readingUsedForTheDistance.speedMetersPerSecond.reset();
    readingUsedForTheDistance.speedAccuracyMetersPerSecond.reset();
  }

  result.distanceMoved = calculateDistancePhoneCouldHaveMoved(
      readingUsedForTheDistance, result.timeInEachActivity,
      rules.fastestPossibleSpeedMetersPerSecond);

  result.howFarPhoneCouldBeMeters =
      calculateHowFarPhoneCouldBeFromReading(result.reading, result.distanceMoved);

  return result;
}

// Step 3: is the first reading better than the second one?
// The five rules are tried in order. A rule only decides when the two
// readings are different in what that rule looks at.
bool firstIsBetterThanSecond(const WhatWeKnowAboutReading& first,
                             const WhatWeKnowAboutReading& second) noexcept {
  const bool firstIsProbablyAJump = (first.jumpCheck.result == JumpCheckResult::ProbablyGpsJump);
  const bool secondIsProbablyAJump =
      (second.jumpCheck.result == JumpCheckResult::ProbablyGpsJump);

  bool firstIsBetter = false;

  if (firstIsProbablyAJump != secondIsProbablyAJump) {
    // 1st rule: the one that is probably a GPS jump loses.
    firstIsBetter = secondIsProbablyAJump;

  } else if (first.howFarPhoneCouldBeMeters != second.howFarPhoneCouldBeMeters) {
    // 2nd rule: the smaller "how far the phone could be" wins.
    firstIsBetter = (first.howFarPhoneCouldBeMeters < second.howFarPhoneCouldBeMeters);

  } else if (first.ageMs != second.ageMs) {
    // 3rd rule: the newer reading wins.
    firstIsBetter = (first.ageMs < second.ageMs);

  } else if (first.reading.horizontalAccuracyMeters != second.reading.horizontalAccuracyMeters) {
    // 4th rule: the smaller GPS error wins.
    firstIsBetter =
        (first.reading.horizontalAccuracyMeters < second.reading.horizontalAccuracyMeters);

  } else {
    // 5th rule: the one that arrived first wins.
    firstIsBetter = (first.reading.arrivalNumber < second.reading.arrivalNumber);
  }

  return firstIsBetter;
}

}  // namespace

// =============================================================================
// FUNCTIONS OTHER FILES CAN CALL
//
// Only one is announced in ReadingSelection.hpp.
// =============================================================================

ChosenReading chooseBestReading(const LocationHistory& history,
                                const PhoneActivityHistory& activityHistory,
                                std::int64_t nowMs,
                                const RulesForChoosingReading& rules) noexcept {
  ChosenReading chosen;

  // Step 1: look for GPS jumps among all the readings.
  const JumpCheckOfAllReadings jumpCheckOfAllReadings =
      checkAllReadingsForGpsJumps(history, rules.fastestPossibleSpeedMetersPerSecond);

  chosen.howManyAreProbablyGpsJumps = jumpCheckOfAllReadings.howManyAreProbablyGpsJumps;
  chosen.someReadingsContradictEachOther =
      jumpCheckOfAllReadings.someReadingsContradictEachOther();
  chosen.readingsFromDifferentProvidersContradict =
      jumpCheckOfAllReadings.readingsFromDifferentProvidersContradict;

  for (std::size_t position = 0; position < history.size(); ++position) {
    const LocationReading& reading = history.at(position);
    chosen.howManyReadingsWereExamined += 1;

    const bool isTooOld = (ageInMs(reading, nowMs) > rules.oldestAgeAllowedMs);
    if (isTooOld) {
      chosen.howManyWereTooOld += 1;
      continue;
    }

    const bool isNotAllowed = (reading.arrivalNumber < rules.firstArrivalNumberAllowed);
    if (isNotAllowed) {
      continue;
    }

    // Step 2: everything we need to know about this reading.
    const WhatWeKnowAboutReading candidate = examineReading(
        history, position, jumpCheckOfAllReadings, activityHistory, nowMs, rules);

    // Step 3: keep it when it is the first one, or better than the best so far.
    const bool isTheFirstOne = !chosen.found;
    if (isTheFirstOne || firstIsBetterThanSecond(candidate, chosen.best)) {
      chosen.best = candidate;
      chosen.found = true;
    }
  }

  return chosen;
}

}  // namespace lce
