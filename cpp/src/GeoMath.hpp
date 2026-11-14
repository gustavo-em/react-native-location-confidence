// Map math: checking coordinates and measuring the distance between two points.
//
// The distance is "as the crow flies" over a perfectly round Earth. That is
// accurate enough to tell whether a courier is near an address. It is not the
// distance by road and it ignores height.
#ifndef LCE_GEO_MATH_HPP
#define LCE_GEO_MATH_HPP

#include "Types.hpp"

namespace lce {

// Average radius of the Earth, in meters.
inline constexpr double EARTH_RADIUS_METERS = 6371008.8;

// Is this a real latitude? (a normal number from -90 to +90, limits included)
bool isValidLatitude(double latitudeDegrees) noexcept;

// Is this a real longitude? (a normal number from -180 to +180, limits included)
bool isValidLongitude(double longitudeDegrees) noexcept;

// Are both latitude and longitude real?
// Note: the point (0, 0) is a real place in the ocean and is valid. It must
// never be treated as "location missing".
bool isValidCoordinates(const Coordinates& coordinates) noexcept;

// Computers can store "negative zero" (-0.0). It means the same as 0 but can
// confuse comparisons. This returns the same point with any -0.0 turned
// into 0.0. Only call it with valid coordinates.
Coordinates normalizeCoordinates(const Coordinates& coordinates) noexcept;

// How many meters separate these two points?
// Only call it with valid coordinates. The answer is never negative and is
// the same in both directions (A to B equals B to A).
double distanceBetweenMeters(const Coordinates& from, const Coordinates& to) noexcept;

}  // namespace lce

#endif  // LCE_GEO_MATH_HPP
