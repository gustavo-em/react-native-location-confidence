// Answers one question: how far could the phone have travelled since a
// location reading was taken?
//
// Why it matters: a reading says where the phone WAS. If it is 10 seconds
// old, the courier may have walked 30 meters since, or driven 450. The older
// the reading and the faster the possible movement, the less it tells us
// about where the phone is now.
//
// There are three ways to answer, from best to worst:
//
//   1. We know what the phone was doing (walking, in a vehicle...).
//      Use the typical top speed of that activity.
//          walking, 5 seconds  ->  3 m/s x 5 s  =  15 m
//
//   2. We do not know the activity, but the GPS reported a speed together
//      with the reading. Use that speed, plus a safety margin, and assume
//      the phone may have sped up since.
//          almost standing still, 3 seconds  ->  about 15 m
//
//   3. We know nothing. Assume the worst: the fastest plausible speed.
//          3 seconds  ->  45 m/s x 3 s  =  135 m
//
// The engine never moves the reading's position. It only widens the circle
// of "where the phone might be now" around it.
//
// All speeds below are design guesses, not physical limits. A phone can be
// carried faster than any of them.
#ifndef LCE_UNCERTAINTY_MODEL_HPP
#define LCE_UNCERTAINTY_MODEL_HPP

#include <cstdint>

#include "Types.hpp"

namespace lce {

// What the phone was doing during the time we care about.
enum class MovementKind : std::uint8_t {
  Stationary,  // not moving (a small drift is still allowed)
  Walking,
  Running,
  Cycling,
  Vehicle,
  Unknown,  // we have no information
};

// Fastest speed we consider plausible for a delivery courier: 45 m/s,
// which is 162 km/h. Used when we know nothing, and for vehicles.
inline constexpr double DEFAULT_FASTEST_PLAUSIBLE_SPEED_METERS_PER_SECOND = 45.0;

// Which of the three ways produced the estimate.
enum class MovementEstimateBasis : std::uint8_t {
  KnownActivity,       // way 1: we knew what the phone was doing
  SpeedReportedByGps,  // way 2: we used the speed that came with the reading
  WorstCase,           // way 3: we knew nothing
};

struct MovementEstimate {
  // How far the phone could have travelled, in meters. Never negative.
  double possibleMovementMeters = 0.0;

  MovementEstimateBasis basis = MovementEstimateBasis::WorstCase;

  // True when the number above is only the worst-case guess.
  bool movementIsUnknown() const noexcept { return basis == MovementEstimateBasis::WorstCase; }
};

// Top speed assumed for an activity, in meters per second.
//   Stationary 0.5   Walking 3   Running 8   Cycling 12
//   Vehicle and Unknown: `fastestPlausibleSpeed`
double topSpeedFor(MovementKind kind, double fastestPlausibleSpeedMetersPerSecond) noexcept;

// How far could the phone have travelled since this reading was taken?
//
//   reading       the location reading being judged
//   ageOfReadingMs  how long ago it was taken
//   knownMovement   what the phone was doing since then, or Unknown
//   fastestPlausibleSpeedMetersPerSecond
//                   the app may lower or raise the worst-case speed
//
// The answer never gets smaller as the reading gets older, and it is never
// larger than the worst case.
MovementEstimate estimatePossibleMovement(
    const LocationReading& reading,
    std::int64_t ageOfReadingMs,
    MovementKind knownMovement,
    double fastestPlausibleSpeedMetersPerSecond =
        DEFAULT_FASTEST_PLAUSIBLE_SPEED_METERS_PER_SECOND) noexcept;

// How far the phone could really be from the reading's position right now:
// the reading's own error plus the possible movement since.
//     10 m accuracy + 15 m possible movement = 25 m
double totalUncertaintyMeters(const LocationReading& reading,
                              const MovementEstimate& movement) noexcept;

}  // namespace lce

#endif  // LCE_UNCERTAINTY_MODEL_HPP
