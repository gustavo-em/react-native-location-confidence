#include "GeoMath.hpp"

#include <cmath>

namespace lce {

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kDegToRad = kPi / 180.0;

double clampUnit(double value) noexcept {
  if (value < 0.0) return 0.0;
  if (value > 1.0) return 1.0;
  return value;
}

}  // namespace

bool isValidLatitudeDeg(double value) noexcept {
  return std::isfinite(value) && value >= -90.0 && value <= 90.0;
}

bool isValidLongitudeDeg(double value) noexcept {
  return std::isfinite(value) && value >= -180.0 && value <= 180.0;
}

bool isValidCoordinates(const Coordinates& coordinates) noexcept {
  return isValidLatitudeDeg(coordinates.latitudeDeg) &&
         isValidLongitudeDeg(coordinates.longitudeDeg);
}

Coordinates normalizeCoordinates(const Coordinates& coordinates) noexcept {
  // Under IEEE-754 round-to-nearest, (-0.0) + 0.0 is +0.0.
  return Coordinates{coordinates.latitudeDeg + 0.0, coordinates.longitudeDeg + 0.0};
}

double haversineMeters(const Coordinates& a, const Coordinates& b) noexcept {
  const double phi1 = a.latitudeDeg * kDegToRad;
  const double phi2 = b.latitudeDeg * kDegToRad;
  const double deltaPhi = (b.latitudeDeg - a.latitudeDeg) * kDegToRad;

  // Wrap the longitude difference into [-180, 180] so that +180 and -180 are
  // the same meridian and a short hop across the antimeridian stays short.
  const double deltaLambdaDeg = std::remainder(b.longitudeDeg - a.longitudeDeg, 360.0);
  const double deltaLambda = deltaLambdaDeg * kDegToRad;

  const double sinHalfPhi = std::sin(deltaPhi / 2.0);
  const double sinHalfLambda = std::sin(deltaLambda / 2.0);

  const double h = clampUnit(sinHalfPhi * sinHalfPhi +
                             std::cos(phi1) * std::cos(phi2) * sinHalfLambda * sinHalfLambda);

  const double centralAngle = 2.0 * std::atan2(std::sqrt(h), std::sqrt(1.0 - h));
  return kEarthMeanRadiusMeters * centralAngle;
}

}  // namespace lce
