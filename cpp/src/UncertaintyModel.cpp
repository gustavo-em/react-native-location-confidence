// =============================================================================
// THE ORDER OF THE CALLS IN THIS FILE
// =============================================================================
//
//   calculateDistancePhoneCouldHaveMoved()                 <- everything starts here
//     |
//     |-- replaceInvalidSpeedWithDefault()                 always, first
//     |
//     |-- secondsSinceReading     = milliseconds / 1000
//     |-- longestDistancePossible = fastest speed x seconds
//     |
//     |-- if we know the phone activity                    OPTION 1
//     |      |-- speedForPhoneActivity()
//     |      |      `-- replaceInvalidSpeedWithDefault()
//     |      `-- meters = speed of the activity x seconds
//     |
//     |-- else if the GPS reported a speed and its error   OPTION 2
//     |      |-- calculateDistanceUsingSpeedReportedByGps()
//     |      `-- meters = what that function returns
//     |
//     |-- else                                             OPTION 3
//     |      `-- meters = longestDistancePossible
//     |
//     `-- if meters > longestDistancePossible              the limit, for all options
//            `-- meters = longestDistancePossible
//
//   calculateHowFarPhoneCouldBeFromReading()               <- called afterwards
//     `-- GPS error + meters
//
// Only ONE of the three options runs in each call.
//
// =============================================================================
// EXAMPLES, STEP BY STEP
// In all of them the GPS error of the reading is 10 meters.
// =============================================================================
//
// EXAMPLE 1 - we know the courier was walking, reading taken 5 seconds ago
//
//   calculateDistancePhoneCouldHaveMoved(reading, 5000, Walking)
//     replaceInvalidSpeedWithDefault(45)        -> 45
//     secondsSinceReading     = 5000 / 1000     =  5
//     longestDistancePossible = 45 x 5          =  225
//     do we know the phone activity?               yes -> OPTION 1
//       speedForPhoneActivity(Walking)          -> 3
//       meters = 3 x 5                          =  15
//     is 15 bigger than 225?                       no
//     returns 15 meters, FromPhoneActivity
//
//   calculateHowFarPhoneCouldBeFromReading()
//     10 + 15                                   -> 25 meters
//
// EXAMPLE 2 - activity unknown, but the GPS reported speed 0.3 with error 0.5,
//             reading taken 3 seconds ago
//
//   calculateDistancePhoneCouldHaveMoved(reading, 3000, Unknown)
//     replaceInvalidSpeedWithDefault(45)        -> 45
//     secondsSinceReading                       =  3
//     longestDistancePossible = 45 x 3          =  135
//     do we know the phone activity?               no
//     did the GPS report speed and its error?      yes -> OPTION 2
//       calculateDistanceUsingSpeedReportedByGps(0.3, 0.5, 3)
//         speedToBeSafe         = 0.3 + 2 x 0.5 =  1.3
//         distanceAtThatSpeed   = 1.3 x 3       =  3.9
//         extraDistanceIfThePhoneSpedUp
//                               = 2.5 x 3 x 3 / 2 = 11.25
//                                               -> 15.15
//     is 15.15 bigger than 135?                    no
//     returns 15.15 meters, FromSpeedReportedByGps
//
//   calculateHowFarPhoneCouldBeFromReading()
//     10 + 15.15                                -> 25.15 meters
//
// EXAMPLE 3 - we know nothing, reading taken 3 seconds ago
//
//   calculateDistancePhoneCouldHaveMoved(reading, 3000, Unknown)
//     replaceInvalidSpeedWithDefault(45)        -> 45
//     secondsSinceReading                       =  3
//     longestDistancePossible = 45 x 3          =  135
//     do we know the phone activity?               no
//     did the GPS report speed and its error?      no -> OPTION 3
//       meters = longestDistancePossible        =  135
//     is 135 bigger than 135?                      no
//     returns 135 meters, AssumingFastestPossibleSpeed
//
//   calculateHowFarPhoneCouldBeFromReading()
//     10 + 135                                  -> 145 meters
//
// EXAMPLE 4 - the limit in action: the GPS reported speed 40 with error 5,
//             reading taken 10 seconds ago
//
//   calculateDistancePhoneCouldHaveMoved(reading, 10000, Unknown)
//     longestDistancePossible = 45 x 10         =  450
//     OPTION 2
//       speedToBeSafe         = 40 + 2 x 5      =  50
//       distanceAtThatSpeed   = 50 x 10         =  500
//       extraDistanceIfThePhoneSpedUp
//                             = 2.5 x 10 x 10 / 2 = 125
//                                               -> 625
//     is 625 bigger than 450?                      YES -> meters = 450
//     returns 450 meters, FromSpeedReportedByGps
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

// If the app gave a speed that makes no sense (zero, negative, or not a real
// number), we ignore it and use 45.
double replaceInvalidSpeedWithDefault(double speed) noexcept {
  const bool speedIsValid = std::isfinite(speed) && speed > 0.0;
  if (speedIsValid) {
    return speed;
  }
  return FASTEST_POSSIBLE_SPEED_METERS_PER_SECOND;
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
// These three are announced in UncertaintyModel.hpp. The main one is
// calculateDistancePhoneCouldHaveMoved().
// =============================================================================

double speedForPhoneActivity(PhoneActivity phoneActivity,
                             double fastestPossibleSpeedMetersPerSecond) noexcept {
  const double fastestPossibleSpeed =
      replaceInvalidSpeedWithDefault(fastestPossibleSpeedMetersPerSecond);

  switch (phoneActivity) {
    case PhoneActivity::StandingStill:
      return SPEED_STANDING_STILL;
    case PhoneActivity::Walking:
      return SPEED_WALKING;
    case PhoneActivity::Running:
      return SPEED_RUNNING;
    case PhoneActivity::Cycling:
      return SPEED_CYCLING;
    case PhoneActivity::InVehicle:
    case PhoneActivity::Unknown:
      return fastestPossibleSpeed;
  }
  return fastestPossibleSpeed;  // never reached; some compilers ask for it
}

DistancePhoneCouldHaveMoved calculateDistancePhoneCouldHaveMoved(
    const LocationReading& reading,
    std::int64_t millisecondsSinceReading,
    PhoneActivity phoneActivity,
    double fastestPossibleSpeedMetersPerSecond) noexcept {
  const double fastestPossibleSpeed =
      replaceInvalidSpeedWithDefault(fastestPossibleSpeedMetersPerSecond);

  // Turn milliseconds into seconds. A negative time makes no sense, so it
  // becomes zero.
  double secondsSinceReading = static_cast<double>(millisecondsSinceReading) / 1000.0;
  if (secondsSinceReading < 0.0) {
    secondsSinceReading = 0.0;
  }

  // The longest distance anything could have moved in that time.
  // No answer below is allowed to be bigger than this.
  const double longestDistancePossible = fastestPossibleSpeed * secondsSinceReading;

  // The two questions that decide which option we use.
  const bool weKnowThePhoneActivity = (phoneActivity != PhoneActivity::Unknown);

  // We need the speed AND how wrong it may be. A speed alone is not enough
  // to trust it.
  const bool gpsReportedSpeedAndItsError = reading.speedMetersPerSecond.has_value() &&
                                           reading.speedAccuracyMetersPerSecond.has_value();

  DistancePhoneCouldHaveMoved result;

  if (weKnowThePhoneActivity) {
    // Option 1: speed of the activity x seconds.
    const double speedOfTheActivity = speedForPhoneActivity(phoneActivity, fastestPossibleSpeed);

    result.meters = speedOfTheActivity * secondsSinceReading;
    result.howItWasCalculated = HowDistanceWasCalculated::FromPhoneActivity;

  } else if (gpsReportedSpeedAndItsError) {
    // Option 2: use the speed that came with the GPS reading.
    const double gpsSpeed = *reading.speedMetersPerSecond;
    const double gpsSpeedError = *reading.speedAccuracyMetersPerSecond;

    result.meters =
        calculateDistanceUsingSpeedReportedByGps(gpsSpeed, gpsSpeedError, secondsSinceReading);
    result.howItWasCalculated = HowDistanceWasCalculated::FromSpeedReportedByGps;

  } else {
    // Option 3: we know nothing, so we assume the fastest speed possible.
    result.meters = longestDistancePossible;
    result.howItWasCalculated = HowDistanceWasCalculated::AssumingFastestPossibleSpeed;
  }

  // The rule for all options: never more than the longest distance possible.
  if (result.meters > longestDistancePossible) {
    result.meters = longestDistancePossible;
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
