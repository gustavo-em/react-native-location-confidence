// How far could the phone have moved since a GPS reading was taken?
//
// A GPS reading tells us where the phone WAS at one moment. If that moment
// was 10 seconds ago, the courier may have walked 30 meters since then, or
// ridden 450 meters on a motorbike. This file calculates that distance.
//
// There are three options, tried in this order:
//
//   Option 1 - We know what the phone was doing (walking, in a vehicle...).
//              distance = speed of that activity x seconds
//              walking for 5 seconds: 3 x 5 = 15 meters
//
//   Option 2 - We do not know the activity, but the GPS reading came with
//              a speed. We use that speed, a little higher to be safe, and
//              we also assume the phone may have sped up afterwards.
//              almost stopped, 3 seconds ago: about 15 meters
//
//   Option 3 - We know nothing. We assume the fastest speed possible.
//              3 seconds ago: 45 x 3 = 135 meters
//
// One rule applies to all three: the answer is never bigger than Option 3.
//
// A picture of the order of the calls, with four examples worked step by
// step, is at the top of UncertaintyModel.cpp.
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

// If the app gave a fastest speed that makes no sense (zero, negative, or not
// a real number), this returns 45 instead. Otherwise it returns the same speed.
// The jump detector (JumpDetector.cpp) uses it too.
double replaceInvalidSpeedWithDefault(double speed) noexcept;

// Which of the three options was used to calculate the distance.
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
double speedForPhoneActivity(PhoneActivity phoneActivity,
                             double fastestPossibleSpeedMetersPerSecond) noexcept;

// THE MAIN FUNCTION OF THIS FILE.
// How many meters could the phone have moved since this GPS reading?
//
//   reading                   the GPS reading we are judging
//   millisecondsSinceReading  how long ago the reading was taken
//   phoneActivity             what the phone was doing since then, or Unknown
//   fastestPossibleSpeedMetersPerSecond
//                             normally 45; the app can change it
DistancePhoneCouldHaveMoved calculateDistancePhoneCouldHaveMoved(
    const LocationReading& reading,
    std::int64_t millisecondsSinceReading,
    PhoneActivity phoneActivity,
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
