// =============================================================================
// THE ORDER OF THE CALLS IN THIS FILE
// =============================================================================
//
//   calculateDistancePhoneCouldHaveMoved(reading, timeInEachActivity)
//     |                                                    <- everything starts here
//     |-- replaceInvalidSpeedWithDefault()                 always, first
//     |
//     |-- secondsSinceReading     = all the lines of the table / 1000
//     |-- longestDistancePossible = fastest speed x seconds
//     |
//     |-- OPTION 3, always                                 we start with it
//     |      `-- meters = longestDistancePossible
//     |
//     |-- OPTION 2, if the GPS reported a speed and its error
//     |      |-- calculateDistanceUsingSpeedReportedByGps()
//     |      |-- if that is more than longestDistancePossible, it is cut down
//     |      `-- meters = that distance
//     |
//     `-- OPTION 1, if we know some of the phone activity
//            |-- calculateDistanceUsingPhoneActivity()
//            |      `-- speedForPhoneActivity()            once for each activity
//            |-- if that is more than longestDistancePossible, it is cut down
//            `-- if that is not longer than meters         THE SMALLEST WINS
//                   `-- meters = that distance
//
//   calculateHowFarPhoneCouldBeFromReading()               <- called afterwards
//     `-- GPS error + meters
//
//   wholeTimeInOneActivity()                               <- makes the simplest table
//     `-- TimeInEachPhoneActivity::add()
//
// Option 3 always runs. Options 2 and 1 run only when we have the information.
//
// =============================================================================
// EXAMPLES, STEP BY STEP
// In all of them the GPS error of the reading is 10 meters.
// =============================================================================
//
// EXAMPLE 1 - the courier was walking the whole time, reading taken 5 seconds ago
//
//   timeInEachActivity = wholeTimeInOneActivity(Walking, 5000)   -> walking 5000
//
//   calculateDistancePhoneCouldHaveMoved(reading, timeInEachActivity)
//     replaceInvalidSpeedWithDefault(45)        -> 45
//     secondsSinceReading     = 5000 / 1000     =  5
//     longestDistancePossible = 45 x 5          =  225
//     OPTION 3                                     meters = 225
//     did the GPS report speed and its error?      no, option 2 is skipped
//     do we know some of the phone activity?       yes -> OPTION 1
//       calculateDistanceUsingPhoneActivity()
//         walking: 3 x 5                        =  15     (all other lines are 0)
//       is 15 longer than 225?                     no -> meters = 15
//     returns 15 meters, FromPhoneActivity
//
//   calculateHowFarPhoneCouldBeFromReading()
//     10 + 15                                   -> 25 meters
//
// EXAMPLE 2 - activity unknown, but the GPS reported speed 0.3 with error 0.5,
//             reading taken 3 seconds ago
//
//   timeInEachActivity = wholeTimeInOneActivity(Unknown, 3000)   -> unknown 3000
//
//   calculateDistancePhoneCouldHaveMoved(reading, timeInEachActivity)
//     longestDistancePossible = 45 x 3          =  135
//     OPTION 3                                     meters = 135
//     did the GPS report speed and its error?      yes -> OPTION 2
//       calculateDistanceUsingSpeedReportedByGps(0.3, 0.5, 3)
//         speedToBeSafe         = 0.3 + 2 x 0.5 =  1.3
//         distanceAtThatSpeed   = 1.3 x 3       =  3.9
//         extraDistanceIfThePhoneSpedUp
//                               = 2.5 x 3 x 3 / 2 = 11.25
//                                               -> 15.15
//       is 15.15 more than 135?                    no -> meters = 15.15
//     do we know some of the phone activity?       no, option 1 is skipped
//     returns 15.15 meters, FromSpeedReportedByGps
//
//   calculateHowFarPhoneCouldBeFromReading()
//     10 + 15.15                                -> 25.15 meters
//
// EXAMPLE 3 - we know nothing, reading taken 3 seconds ago
//
//     longestDistancePossible = 45 x 3          =  135
//     OPTION 3                                     meters = 135
//     options 2 and 1 are skipped
//     returns 135 meters, AssumingFastestPossibleSpeed
//
//   calculateHowFarPhoneCouldBeFromReading()
//     10 + 135                                  -> 145 meters
//
// EXAMPLE 4 - the limit in action: the GPS reported speed 40 with error 5,
//             reading taken 10 seconds ago, activity unknown
//
//     longestDistancePossible = 45 x 10         =  450
//     OPTION 3                                     meters = 450
//     OPTION 2
//       speedToBeSafe         = 40 + 2 x 5      =  50
//       distanceAtThatSpeed   = 50 x 10         =  500
//       extraDistanceIfThePhoneSpedUp
//                             = 2.5 x 10 x 10 / 2 = 125
//                                               -> 625
//       is 625 more than 450?                      YES -> cut down to 450
//                                                  meters = 450
//     returns 450 meters, FromSpeedReportedByGps
//
// EXAMPLE 5 - the phone did two things: 20 seconds standing still, then
//             10 seconds walking. Reading taken 30 seconds ago.
//
//   timeInEachActivity:   standing still 20000,   walking 10000
//
//     secondsSinceReading     = 30000 / 1000    =  30
//     longestDistancePossible = 45 x 30         =  1350
//     OPTION 3                                     meters = 1350
//     OPTION 1
//       calculateDistanceUsingPhoneActivity()
//         standing still: 0.5 x 20              =  10
//         walking:        3 x 10                =  30
//                                               -> 40
//       is 40 longer than 1350?                    no -> meters = 40
//     returns 40 meters, FromPhoneActivity
//
// EXAMPLE 6 - part of the time is unknown: 2 seconds standing still and
//             8 seconds for which nobody told us anything
//
//   timeInEachActivity:   standing still 2000,   unknown 8000
//
//     longestDistancePossible = 45 x 10         =  450
//     OPTION 3                                     meters = 450
//     OPTION 1
//         standing still: 0.5 x 2               =  1
//         unknown:        45 x 8                =  360
//                                               -> 361
//       is 361 longer than 450?                    no -> meters = 361
//     returns 361 meters, FromPhoneActivity
//
// EXAMPLE 7 - THE SMALLEST WINS: the phone was running the whole time, and
//             the GPS reported speed 0.3 with error 0.5. Reading 3 seconds ago.
//
//     longestDistancePossible = 45 x 3          =  135
//     OPTION 3                                     meters = 135
//     OPTION 2 (same numbers as example 2)         meters = 15.15
//     OPTION 1
//         running: 8 x 3                        =  24
//       is 24 longer than 15.15?                   YES -> meters stays 15.15
//     returns 15.15 meters, FromSpeedReportedByGps
//
//   Without this rule, knowing that the phone was running would give 24
//   meters, a worse answer than knowing nothing about the activity (15.15).
//   Knowing more must never make the answer worse.
//
// =============================================================================
// WHAT THE NUMBER IS USED FOR
// =============================================================================
//
// With example 2: the phone could be up to 25.15 meters from the reading.
// If the reading is 30 meters from the delivery address and the delivery
// radius is 100 meters:
//
//     30 + 25.15 = 55.15, which is less than 100   ->  "inside"
//
// With example 3 the same reading gives 30 + 145 = 175, more than 100, and
// the engine answers "cannot tell" instead of guessing.
//
// That comparison is made by classifyRelation() in ProximityRelation.cpp.
// The same number also lowers the quality score in QualityScore.cpp.
// =============================================================================

#include "UncertaintyModel.hpp"

#include <cmath>

namespace lce {

// =============================================================================
// HELPERS - used only inside this file
//
// Everything between "namespace {" and "}  // namespace" is private: no other
// file can see it or call it. In TypeScript this would be a function without
// "export".
//
// The helpers come BEFORE the functions that use them because C++ reads the
// file from top to bottom: a function must exist above the line that calls it.
// (speedForPhoneActivity is used here although it is written further down.
// That works because the header already announced it.)
// =============================================================================
namespace {

// --- The speed we assume for each activity, in meters per second -------------
// "Standing still" is not zero on purpose: a phone that says it is not moving
// can still move a little (the person shifts in the seat).
constexpr double SPEED_STANDING_STILL = 0.5;
constexpr double SPEED_WALKING = 3.0;   // about 11 km/h
constexpr double SPEED_RUNNING = 8.0;   // about 29 km/h
constexpr double SPEED_CYCLING = 12.0;  // about 43 km/h

// --- Numbers used only in option 2 (speed reported by the GPS) ---------------

// The GPS tells us a speed and also how wrong that speed may be.
// To be safe, we add that error two times to the speed.
// Example: speed 0.3 and error 0.5  ->  0.3 + 2 x 0.5 = 1.3
constexpr double HOW_MANY_TIMES_WE_ADD_THE_GPS_SPEED_ERROR = 2.0;

// The GPS measured the speed at the moment of the reading. After that, the
// phone may have started to move faster (a courier who was stopped gets on
// the motorbike). We assume the speed can grow by 2.5 meters per second,
// every second. That is about how a car speeds up when it leaves quickly.
constexpr double SPEED_THE_PHONE_CAN_GAIN_EACH_SECOND = 2.5;

double millisecondsToSeconds(std::int64_t milliseconds) noexcept {
  return static_cast<double>(milliseconds) / 1000.0;
}

// Option 1: the distance, using what the phone was doing.
// For each activity: speed of that activity x seconds spent in it.
//
// Example: 20 seconds standing still and 10 seconds walking.
//   standing still  0.5 x 20 = 10 meters
//   walking         3 x 10   = 30 meters
//   answer          10 + 30  = 40 meters
//
// Time for which nobody told us the activity counts at the fastest speed.
double calculateDistanceUsingPhoneActivity(const TimeInEachPhoneActivity& timeInEachActivity,
                                           double fastestPossibleSpeed) noexcept {
  const double metersStandingStill =
      speedForPhoneActivity(PhoneActivity::StandingStill, fastestPossibleSpeed) *
      millisecondsToSeconds(timeInEachActivity.millisecondsStandingStill);

  const double metersWalking =
      speedForPhoneActivity(PhoneActivity::Walking, fastestPossibleSpeed) *
      millisecondsToSeconds(timeInEachActivity.millisecondsWalking);

  const double metersRunning =
      speedForPhoneActivity(PhoneActivity::Running, fastestPossibleSpeed) *
      millisecondsToSeconds(timeInEachActivity.millisecondsRunning);

  const double metersCycling =
      speedForPhoneActivity(PhoneActivity::Cycling, fastestPossibleSpeed) *
      millisecondsToSeconds(timeInEachActivity.millisecondsCycling);

  const double metersInVehicle =
      speedForPhoneActivity(PhoneActivity::InVehicle, fastestPossibleSpeed) *
      millisecondsToSeconds(timeInEachActivity.millisecondsInVehicle);

  const double metersUnknown =
      speedForPhoneActivity(PhoneActivity::Unknown, fastestPossibleSpeed) *
      millisecondsToSeconds(timeInEachActivity.millisecondsUnknown);

  return metersStandingStill + metersWalking + metersRunning + metersCycling + metersInVehicle +
         metersUnknown;
}

// Option 2: the distance, using the speed that the GPS reported.
//
// Example: the GPS said speed 0.3 with error 0.5, and that was 3 seconds ago.
//   speed to be safe            = 0.3 + 2 x 0.5     = 1.3 meters per second
//   distance at that speed      = 1.3 x 3           = 3.9 meters
//   extra distance if it sped up = 2.5 x 3 x 3 / 2  = 11.25 meters
//   answer                      = 3.9 + 11.25       = 15.15 meters
double calculateDistanceUsingSpeedReportedByGps(double gpsSpeed,
                                                double gpsSpeedError,
                                                double secondsSinceReading) noexcept {
  const double speedToBeSafe =
      gpsSpeed + HOW_MANY_TIMES_WE_ADD_THE_GPS_SPEED_ERROR * gpsSpeedError;

  const double distanceAtThatSpeed = speedToBeSafe * secondsSinceReading;

  const double extraDistanceIfThePhoneSpedUp =
      SPEED_THE_PHONE_CAN_GAIN_EACH_SECOND * secondsSinceReading * secondsSinceReading / 2.0;

  return distanceAtThatSpeed + extraDistanceIfThePhoneSpedUp;
}

}  // namespace

// =============================================================================
// FUNCTIONS OTHER FILES CAN CALL
//
// These are announced in UncertaintyModel.hpp. The main one is
// calculateDistancePhoneCouldHaveMoved().
// =============================================================================

// If the app gave a speed that makes no sense (zero, negative, or not a real
// number), we ignore it and use 45.
// It is not private because the jump detector needs the same rule.
double replaceInvalidSpeedWithDefault(double speed) noexcept {
  const bool speedIsValid = std::isfinite(speed) && speed > 0.0;
  if (speedIsValid) {
    return speed;
  }
  return FASTEST_POSSIBLE_SPEED_METERS_PER_SECOND;
}

// "TimeInEachPhoneActivity::add" means: the function add that belongs to the
// table TimeInEachPhoneActivity. Inside it, the lines of the table are used
// directly by their names.
void TimeInEachPhoneActivity::add(PhoneActivity phoneActivity,
                                  std::int64_t milliseconds) noexcept {
  const bool timeIsNegative = (milliseconds < 0);
  if (timeIsNegative) {
    return;  // a negative time makes no sense
  }

  switch (phoneActivity) {
    case PhoneActivity::StandingStill:
      millisecondsStandingStill += milliseconds;
      break;
    case PhoneActivity::Walking:
      millisecondsWalking += milliseconds;
      break;
    case PhoneActivity::Running:
      millisecondsRunning += milliseconds;
      break;
    case PhoneActivity::Cycling:
      millisecondsCycling += milliseconds;
      break;
    case PhoneActivity::InVehicle:
      millisecondsInVehicle += milliseconds;
      break;
    case PhoneActivity::Unknown:
      millisecondsUnknown += milliseconds;
      break;
  }
}

TimeInEachPhoneActivity wholeTimeInOneActivity(PhoneActivity phoneActivity,
                                               std::int64_t milliseconds) noexcept {
  TimeInEachPhoneActivity timeInEachActivity;
  timeInEachActivity.add(phoneActivity, milliseconds);
  return timeInEachActivity;
}

double speedForPhoneActivity(PhoneActivity phoneActivity,
                             double fastestPossibleSpeedMetersPerSecond) noexcept {
  const double fastestPossibleSpeed =
      replaceInvalidSpeedWithDefault(fastestPossibleSpeedMetersPerSecond);

  // InVehicle and Unknown keep this value.
  double speed = fastestPossibleSpeed;

  switch (phoneActivity) {
    case PhoneActivity::StandingStill:
      speed = SPEED_STANDING_STILL;
      break;
    case PhoneActivity::Walking:
      speed = SPEED_WALKING;
      break;
    case PhoneActivity::Running:
      speed = SPEED_RUNNING;
      break;
    case PhoneActivity::Cycling:
      speed = SPEED_CYCLING;
      break;
    case PhoneActivity::InVehicle:
    case PhoneActivity::Unknown:
      break;
  }

  // No activity is faster than the fastest possible speed. This matters when
  // the app lowers that speed, for example to 5 for couriers on foot.
  if (speed > fastestPossibleSpeed) {
    speed = fastestPossibleSpeed;
  }

  return speed;
}

DistancePhoneCouldHaveMoved calculateDistancePhoneCouldHaveMoved(
    const LocationReading& reading,
    const TimeInEachPhoneActivity& timeInEachActivity,
    double fastestPossibleSpeedMetersPerSecond) noexcept {
  const double fastestPossibleSpeed =
      replaceInvalidSpeedWithDefault(fastestPossibleSpeedMetersPerSecond);

  // The time since the reading was taken: all the lines of the table together.
  const double secondsSinceReading =
      millisecondsToSeconds(timeInEachActivity.totalMilliseconds());

  // The longest distance anything could have moved in that time.
  // No option below is allowed to give more than this.
  const double longestDistancePossible = fastestPossibleSpeed * secondsSinceReading;

  // The two questions that decide which options we can calculate.
  const bool weKnowSomeOfThePhoneActivity = !timeInEachActivity.weKnowNothing();

  // We need the speed AND how wrong it may be. A speed alone is not enough
  // to trust it.
  const bool gpsReportedSpeedAndItsError = reading.speedMetersPerSecond.has_value() &&
                                           reading.speedAccuracyMetersPerSecond.has_value();

  DistancePhoneCouldHaveMoved result;

  // --- Option 3: we assume the fastest speed possible --------------------------
  // It can always be calculated, so we start with it.
  result.meters = longestDistancePossible;
  result.howItWasCalculated = HowDistanceWasCalculated::AssumingFastestPossibleSpeed;

  // --- Option 2: the speed that came with the GPS reading ----------------------
  if (gpsReportedSpeedAndItsError) {
    const double gpsSpeed = *reading.speedMetersPerSecond;
    const double gpsSpeedError = *reading.speedAccuracyMetersPerSecond;

    double distanceUsingGpsSpeed =
        calculateDistanceUsingSpeedReportedByGps(gpsSpeed, gpsSpeedError, secondsSinceReading);

    // Never more than the longest distance possible.
    if (distanceUsingGpsSpeed > longestDistancePossible) {
      distanceUsingGpsSpeed = longestDistancePossible;
    }

    // After that limit it is never longer than option 3, so it takes its place.
    result.meters = distanceUsingGpsSpeed;
    result.howItWasCalculated = HowDistanceWasCalculated::FromSpeedReportedByGps;
  }

  // --- Option 1: what the phone was doing --------------------------------------
  if (weKnowSomeOfThePhoneActivity) {
    double distanceUsingPhoneActivity =
        calculateDistanceUsingPhoneActivity(timeInEachActivity, fastestPossibleSpeed);

    // Never more than the longest distance possible.
    if (distanceUsingPhoneActivity > longestDistancePossible) {
      distanceUsingPhoneActivity = longestDistancePossible;
    }

    // THE SMALLEST WINS. Option 1 only takes the place of what we have so far
    // when it is not longer.
    const bool optionOneIsNotLonger = (distanceUsingPhoneActivity <= result.meters);
    if (optionOneIsNotLonger) {
      result.meters = distanceUsingPhoneActivity;
      result.howItWasCalculated = HowDistanceWasCalculated::FromPhoneActivity;
    }
  }

  return result;
}

double calculateHowFarPhoneCouldBeFromReading(
    const LocationReading& reading,
    const DistancePhoneCouldHaveMoved& distanceMoved) noexcept {
  const double gpsErrorMeters = reading.horizontalAccuracyMeters;
  return gpsErrorMeters + distanceMoved.meters;
}

}  // namespace lce
