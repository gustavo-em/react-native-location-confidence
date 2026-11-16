// How far could the phone have moved since a GPS reading was taken?
//
// A GPS reading tells us where the phone WAS at one moment. If that moment
// was 10 seconds ago, the courier may have walked 30 meters since then, or
// ridden 450 meters on a motorbike. This file calculates that distance.
//
// There are three ways to calculate it:
//
//   Option 1 - We know what the phone was doing (walking, in a vehicle...).
//              distance = speed of that activity x seconds
//              walking for 5 seconds: 3 x 5 = 15 meters
//
//              The phone may have done more than one thing since the reading.
//              Then each piece is calculated and the pieces are added.
//              20 seconds standing still, then 10 seconds walking:
//              0.5 x 20 + 3 x 10 = 40 meters
//              A piece for which nobody told us the activity counts at the
//              fastest speed possible.
//
//   Option 2 - The GPS reading came with a speed. We use that speed, a little
//              higher to be safe, and we also assume the phone may have sped
//              up afterwards.
//              almost stopped, 3 seconds ago: about 15 meters
//
//   Option 3 - We know nothing. We assume the fastest speed possible.
//              3 seconds ago: 45 x 3 = 135 meters
//
// Option 3 can always be calculated. Options 1 and 2 only when we have the
// information. Every option that can be calculated gives a distance, and
// THE SMALLEST DISTANCE WINS.
//
// Why the smallest? Each option is a limit: "the phone cannot be farther
// than this". When two limits are true, the phone respects both, so the
// smaller one is the one that counts. It also guarantees that knowing more
// never makes the answer worse.
//
// A picture of the order of the calls, with examples worked step by step, is
// at the top of UncertaintyModel.cpp.
//
// All speeds in this file are guesses made for this project. They are not
// physical limits: a phone can be carried faster than any of them.
#ifndef LCE_UNCERTAINTY_MODEL_HPP
#define LCE_UNCERTAINTY_MODEL_HPP

#include <cstdint>

#include "Types.hpp"

namespace lce {

// What the phone was doing after the GPS reading was taken.
enum class PhoneActivity : std::uint8_t {
  StandingStill,
  Walking,
  Running,
  Cycling,
  InVehicle,
  Unknown,  // nobody told us what the phone was doing
};

// The fastest speed we believe a courier can move: 45 meters per second,
// which is 162 km/h. The app can choose a different number.
inline constexpr double FASTEST_POSSIBLE_SPEED_METERS_PER_SECOND = 45.0;

// --- The two numbers of option 2 (speed reported by the GPS) -----------------

// The GPS tells us a speed and also how wrong that speed may be.
// To be safe, we add that error two times to the speed.
// Example: speed 0.3 and error 0.5  ->  0.3 + 2 x 0.5 = 1.3
inline constexpr double HOW_MANY_TIMES_WE_ADD_THE_GPS_SPEED_ERROR = 2.0;

// The GPS measured the speed at the moment of the reading. After that, the
// phone may have started to move faster (a courier who was stopped gets on
// the motorbike). We assume the speed can grow by 2.5 meters per second,
// every second. That is about how a car speeds up when it leaves quickly.
inline constexpr double SPEED_THE_PHONE_CAN_GAIN_EACH_SECOND = 2.5;

// If the app gave a fastest speed that makes no sense (zero, negative, or not
// a real number), this returns 45 instead. Otherwise it returns the same speed.
// The jump detector (JumpDetector.cpp) uses it too.
double replaceInvalidSpeedWithDefault(double speed) noexcept;

// How long the phone spent in each activity since the GPS reading was taken.
// It is a small table with one line for each activity:
//
//     standing still    20000 milliseconds
//     walking           10000 milliseconds
//     all the others        0
//
// The lines added together are the time since the reading was taken.
// PhoneActivityTimeline.cpp fills this table from what the phone reported.
struct TimeInEachPhoneActivity {
  std::int64_t millisecondsStandingStill = 0;
  std::int64_t millisecondsWalking = 0;
  std::int64_t millisecondsRunning = 0;
  std::int64_t millisecondsCycling = 0;
  std::int64_t millisecondsInVehicle = 0;

  // Time for which nobody told us what the phone was doing.
  std::int64_t millisecondsUnknown = 0;

  // True when, for some stretch of time, two reports from the phone said
  // different things. That stretch was counted as unknown.
  bool twoReportsDisagreed = false;

  // Adds time to the line of one activity. A negative time is ignored.
  void add(PhoneActivity phoneActivity, std::int64_t milliseconds) noexcept;

  // All the lines added together.
  std::int64_t totalMilliseconds() const noexcept {
    return millisecondsStandingStill + millisecondsWalking + millisecondsRunning +
           millisecondsCycling + millisecondsInVehicle + millisecondsUnknown;
  }

  // True when the whole time is unknown. Also true when no time has passed.
  bool weKnowNothing() const noexcept { return millisecondsUnknown == totalMilliseconds(); }

  // True when the phone was standing still during the whole time.
  bool standingStillTheWholeTime() const noexcept {
    return millisecondsStandingStill > 0 && millisecondsStandingStill == totalMilliseconds();
  }
};

// The simplest table: the phone did one single thing during the whole time.
//     wholeTimeInOneActivity(PhoneActivity::Walking, 5000)   ->   walking 5000
TimeInEachPhoneActivity wholeTimeInOneActivity(PhoneActivity phoneActivity,
                                               std::int64_t milliseconds) noexcept;

// Which of the three options gave the distance.
enum class HowDistanceWasCalculated : std::uint8_t {
  FromPhoneActivity,             // option 1
  FromSpeedReportedByGps,        // option 2
  AssumingFastestPossibleSpeed,  // option 3
};

// The answer: how many meters, and which option produced that number.
struct DistancePhoneCouldHaveMoved {
  double meters = 0.0;

  HowDistanceWasCalculated howItWasCalculated =
      HowDistanceWasCalculated::AssumingFastestPossibleSpeed;

  // True when we had no information at all and used option 3.
  bool weOnlyAssumedTheWorst() const noexcept {
    return howItWasCalculated == HowDistanceWasCalculated::AssumingFastestPossibleSpeed;
  }
};

// The speed we assume for each activity, in meters per second.
//   StandingStill 0.5    Walking 3    Running 8    Cycling 12
//   InVehicle and Unknown: the fastest possible speed
// The answer is never above the fastest possible speed.
double speedForPhoneActivity(PhoneActivity phoneActivity,
                             double fastestPossibleSpeedMetersPerSecond) noexcept;

// THE MAIN FUNCTION OF THIS FILE.
// How many meters could the phone have moved since this GPS reading?
//
//   reading             the GPS reading we are judging
//   timeInEachActivity  how long the phone spent in each activity since the
//                       reading was taken (the lines added together are the
//                       time since the reading)
//   fastestPossibleSpeedMetersPerSecond
//                       normally 45; the app can change it
DistancePhoneCouldHaveMoved calculateDistancePhoneCouldHaveMoved(
    const LocationReading& reading,
    const TimeInEachPhoneActivity& timeInEachActivity,
    double fastestPossibleSpeedMetersPerSecond =
        FASTEST_POSSIBLE_SPEED_METERS_PER_SECOND) noexcept;

// How far from the GPS reading could the phone be right now?
// It is the GPS error plus the distance the phone could have moved.
//     GPS error of 10 meters + could have moved 15 meters = 25 meters
double calculateHowFarPhoneCouldBeFromReading(
    const LocationReading& reading,
    const DistancePhoneCouldHaveMoved& distanceMoved) noexcept;

}  // namespace lce

#endif  // LCE_UNCERTAINTY_MODEL_HPP
