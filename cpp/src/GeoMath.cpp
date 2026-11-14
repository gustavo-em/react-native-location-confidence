#include "GeoMath.hpp"

#include <cmath>

namespace lce {

namespace {

constexpr double PI = 3.14159265358979323846;
constexpr double DEGREES_TO_RADIANS = PI / 180.0;

// The math functions (sin, cos) expect angles in radians; GPS gives degrees.
double toRadians(double degrees) noexcept {
  return degrees * DEGREES_TO_RADIANS;
}

// Tiny rounding errors can push a value that should be between 0 and 1 a
// hair outside that range. This brings it back.
double keepBetweenZeroAndOne(double value) noexcept {
  if (value < 0.0) return 0.0;
  if (value > 1.0) return 1.0;
  return value;
}

// "Normal number" means: not infinity and not NaN (the "not a number" value
// that broken calculations produce).
bool isNormalNumber(double value) noexcept {
  return std::isfinite(value);
}

}  // namespace

bool isValidLatitude(double latitudeDegrees) noexcept {
  return isNormalNumber(latitudeDegrees) && latitudeDegrees >= -90.0 && latitudeDegrees <= 90.0;
}

bool isValidLongitude(double longitudeDegrees) noexcept {
  return isNormalNumber(longitudeDegrees) && longitudeDegrees >= -180.0 &&
         longitudeDegrees <= 180.0;
}

bool isValidCoordinates(const Coordinates& coordinates) noexcept {
  return isValidLatitude(coordinates.latitudeDegrees) &&
         isValidLongitude(coordinates.longitudeDegrees);
}

Coordinates normalizeCoordinates(const Coordinates& coordinates) noexcept {
  // Adding 0.0 to -0.0 gives +0.0, and does not change any other number.
  return Coordinates{coordinates.latitudeDegrees + 0.0, coordinates.longitudeDegrees + 0.0};
}

double distanceBetweenMeters(const Coordinates& from, const Coordinates& to) noexcept {
  const double fromLatitude = toRadians(from.latitudeDegrees);
  const double toLatitude = toRadians(to.latitudeDegrees);
  const double latitudeDifference = toRadians(to.latitudeDegrees - from.latitudeDegrees);

  // Longitude wraps around the planet: +180 and -180 are the same line.
  // Going from longitude 179.999 to -179.999 is a short hop of 0.002 degrees,
  // not a trip of 359.998 degrees around the world. `remainder` picks the
  // short way.
  const double longitudeDifferenceDegrees =
      std::remainder(to.longitudeDegrees - from.longitudeDegrees, 360.0);
  const double longitudeDifference = toRadians(longitudeDifferenceDegrees);

  // The "haversine" formula, the standard way to measure distance on a
  // sphere. It produces a number from 0 (same point) to 1 (opposite sides of
  // the planet).
  const double sineOfHalfLatitudeDifference = std::sin(latitudeDifference / 2.0);
  const double sineOfHalfLongitudeDifference = std::sin(longitudeDifference / 2.0);

  const double haversine = keepBetweenZeroAndOne(
      sineOfHalfLatitudeDifference * sineOfHalfLatitudeDifference +
      std::cos(fromLatitude) * std::cos(toLatitude) * sineOfHalfLongitudeDifference *
          sineOfHalfLongitudeDifference);

  // Turn that number into the angle between the two points as seen from the
  // center of the Earth, then into meters along the surface.
  const double angleBetweenPoints =
      2.0 * std::atan2(std::sqrt(haversine), std::sqrt(1.0 - haversine));

  return EARTH_RADIUS_METERS * angleBetweenPoints;
}

}  // namespace lce
