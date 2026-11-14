// Geodesy helpers. Distances are great-circle distances on a sphere of mean
// Earth radius: adequate for delivery-scale proximity, not a route distance.
#ifndef LCE_GEO_MATH_HPP
#define LCE_GEO_MATH_HPP

#include "Types.hpp"

namespace lce {

inline constexpr double kEarthMeanRadiusMeters = 6371008.8;

bool isValidLatitudeDeg(double value) noexcept;
bool isValidLongitudeDeg(double value) noexcept;

// Finite and inside the WGS-84 ranges, bounds included. (0, 0) is valid.
bool isValidCoordinates(const Coordinates& coordinates) noexcept;

// Turns negative zero into positive zero. Requires valid coordinates.
Coordinates normalizeCoordinates(const Coordinates& coordinates) noexcept;

// Haversine distance in meters. Requires valid coordinates; the result is
// finite, non-negative and symmetric, and handles the antimeridian.
double haversineMeters(const Coordinates& a, const Coordinates& b) noexcept;

}  // namespace lce

#endif  // LCE_GEO_MATH_HPP
