#include "UncertaintyModel.hpp"

#include <cmath>

namespace lce {

namespace {

// --- Typical top speeds per activity, in meters per second ---------------------
// Design guesses. "Stationary" is not zero on purpose: a phone that reports
// "not moving" can still drift a little (someone shifting in a seat).
constexpr double STATIONARY_DRIFT_SPEED = 0.5;
constexpr double WALKING_TOP_SPEED = 3.0;   // about 11 km/h
constexpr double RUNNING_TOP_SPEED = 8.0;   // about 29 km/h
constexpr double CYCLING_TOP_SPEED = 12.0;  // about 43 km/h

// --- Numbers used with the speed reported by the GPS ----------------------------

// The GPS reports a speed and how wrong that speed might be. To stay on the
// safe side we assume the real speed was higher than reported by twice that
// error. Example: reported 0.3 m/s, error 0.5 m/s -> we assume 1.3 m/s.
constexpr double SPEED_ERROR_SAFETY_FACTOR = 2.0;

// The speed was measured when the reading was taken. Since then the phone
// may have sped up (a courier who was stopped gets on the motorbike). We
// assume it could have accelerated this hard the whole time: 2.5 m/s every
// second, roughly a car pulling away briskly.
constexpr double ASSUMED_ACCELERATION_METERS_PER_SECOND_SQUARED = 2.5;

double smallerOf(double first, double second) noexcept {
  return first < second ? first : second;
}

// An invalid setting from the app (zero, negative, not a number) falls back
// to the default instead of producing a nonsense answer.
double usableFastestSpeed(double requestedSpeed) noexcept {
  const bool isUsable = std::isfinite(requestedSpeed) && requestedSpeed > 0.0;
  return isUsable ? requestedSpeed : DEFAULT_FASTEST_PLAUSIBLE_SPEED_METERS_PER_SECOND;
}

// Way 2: distance the phone could have covered, judging by the speed the GPS
// reported with the reading.
//
// Two parts are added together:
//   - distance at the (pessimistic) reported speed:  speed x time
//   - extra distance from speeding up the whole time: acceleration x time^2 / 2
//
// Example: reported 0.3 m/s with 0.5 m/s error, 3 seconds ago.
//   pessimistic speed = 0.3 + 2 x 0.5 = 1.3 m/s
//   at that speed     = 1.3 x 3       = 3.9 m
//   from speeding up  = 2.5 x 3 x 3 / 2 = 11.25 m
//   total             = 15.15 m       (the worst case would say 135 m)
double distanceFromReportedSpeed(double reportedSpeed,
                                 double reportedSpeedError,
                                 double secondsSinceReading) noexcept {
  const double pessimisticSpeed = reportedSpeed + SPEED_ERROR_SAFETY_FACTOR * reportedSpeedError;
  const double distanceAtThatSpeed = pessimisticSpeed * secondsSinceReading;

  const double extraDistanceFromSpeedingUp =
      ASSUMED_ACCELERATION_METERS_PER_SECOND_SQUARED * secondsSinceReading * secondsSinceReading /
      2.0;

  return distanceAtThatSpeed + extraDistanceFromSpeedingUp;
}

}  // namespace

double topSpeedFor(MovementKind kind, double fastestPlausibleSpeedMetersPerSecond) noexcept {
  const double fastestPlausibleSpeed = usableFastestSpeed(fastestPlausibleSpeedMetersPerSecond);

  switch (kind) {
    case MovementKind::Stationary:
      return STATIONARY_DRIFT_SPEED;
    case MovementKind::Walking:
      return WALKING_TOP_SPEED;
    case MovementKind::Running:
      return RUNNING_TOP_SPEED;
    case MovementKind::Cycling:
      return CYCLING_TOP_SPEED;
    case MovementKind::Vehicle:
    case MovementKind::Unknown:
      return fastestPlausibleSpeed;
  }
  return fastestPlausibleSpeed;  // unreachable; keeps every compiler happy
}

MovementEstimate estimatePossibleMovement(
    const LocationReading& reading,
    std::int64_t ageOfReadingMs,
    MovementKind knownMovement,
    double fastestPlausibleSpeedMetersPerSecond) noexcept {
  const double fastestPlausibleSpeed = usableFastestSpeed(fastestPlausibleSpeedMetersPerSecond);

  // A negative age makes no sense; treat it as "taken right now".
  const double secondsSinceReading =
      ageOfReadingMs > 0 ? static_cast<double>(ageOfReadingMs) / 1000.0 : 0.0;

  // The ceiling for every answer: nothing travels farther than the fastest
  // plausible speed allows.
  const double worstCaseDistance = fastestPlausibleSpeed * secondsSinceReading;

  MovementEstimate estimate;

  // --- Way 1: we know what the phone was doing -------------------------------
  const bool activityIsKnown = (knownMovement != MovementKind::Unknown);
  if (activityIsKnown) {
    const double distanceForActivity =
        topSpeedFor(knownMovement, fastestPlausibleSpeed) * secondsSinceReading;

    estimate.possibleMovementMeters = smallerOf(distanceForActivity, worstCaseDistance);
    estimate.basis = MovementEstimateBasis::KnownActivity;
    return estimate;
  }

  // --- Way 2: the GPS told us the speed at the moment of the reading ---------
  // We need both the speed and how wrong it might be. A speed without its
  // error cannot be trusted enough to shrink the estimate.
  const bool gpsReportedSpeedAndItsError =
      reading.speedMetersPerSecond.has_value() && reading.speedAccuracyMetersPerSecond.has_value();
  if (gpsReportedSpeedAndItsError) {
    const double distanceJudgingBySpeed = distanceFromReportedSpeed(
        *reading.speedMetersPerSecond, *reading.speedAccuracyMetersPerSecond, secondsSinceReading);

    estimate.possibleMovementMeters = smallerOf(distanceJudgingBySpeed, worstCaseDistance);
    estimate.basis = MovementEstimateBasis::SpeedReportedByGps;
    return estimate;
  }

  // --- Way 3: we know nothing -------------------------------------------------
  estimate.possibleMovementMeters = worstCaseDistance;
  estimate.basis = MovementEstimateBasis::WorstCase;
  return estimate;
}

double totalUncertaintyMeters(const LocationReading& reading,
                              const MovementEstimate& movement) noexcept {
  return reading.horizontalAccuracyMeters + movement.possibleMovementMeters;
}

}  // namespace lce
