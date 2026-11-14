#include "third_party/doctest.h"

#include <cmath>
#include <limits>

#include "GeoMath.hpp"
#include "ProximityRelation.hpp"

using namespace lce;

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kMetersPerDegree = kEarthMeanRadiusMeters * kPi / 180.0;
constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();
constexpr double kInf = std::numeric_limits<double>::infinity();

}  // namespace

TEST_CASE("GEO-001 identical coordinates give exactly zero and (0,0) is accepted") {
  const Coordinates origin{0.0, 0.0};
  CHECK(isValidCoordinates(origin));
  CHECK(haversineMeters(origin, origin) == 0.0);
}

TEST_CASE("GEO-002 one degree of latitude at the equator") {
  const double d = haversineMeters({0.0, 0.0}, {1.0, 0.0});
  CHECK(std::abs(d - 111195.08) <= 2.0);
  CHECK(d == doctest::Approx(kMetersPerDegree).epsilon(1e-9));
}

TEST_CASE("GEO-003 short hop across the antimeridian") {
  const double d = haversineMeters({0.0, 179.999}, {0.0, -179.999});
  CHECK(d == doctest::Approx(0.002 * kMetersPerDegree).epsilon(1e-6));
  CHECK(d < 1000.0);
}

TEST_CASE("GEO-004 longitude +180 and -180 are the same meridian") {
  CHECK(haversineMeters({0.0, 180.0}, {0.0, -180.0}) == 0.0);
  CHECK(haversineMeters({0.0, -180.0}, {0.0, 180.0}) == 0.0);
}

TEST_CASE("GEO-005 near the North Pole the distance stays small and finite") {
  const double d = haversineMeters({89.999, 0.0}, {89.999, 180.0});
  CHECK(std::isfinite(d));
  CHECK(d == doctest::Approx(0.002 * kMetersPerDegree).epsilon(1e-6));
}

TEST_CASE("GEO-006 antipodes are finite and close to pi * R") {
  const double d = haversineMeters({0.0, 0.0}, {0.0, 180.0});
  CHECK(std::isfinite(d));
  CHECK(d == doctest::Approx(kPi * kEarthMeanRadiusMeters).epsilon(1e-12));
}

TEST_CASE("GEO-007 symmetric latitudes give a symmetric result") {
  const Coordinates a{10.0, 20.0};
  const Coordinates b{-10.0, 20.0};
  CHECK(haversineMeters(a, b) == haversineMeters(b, a));
  CHECK(haversineMeters(a, b) == doctest::Approx(20.0 * kMetersPerDegree).epsilon(1e-9));
}

TEST_CASE("GEO-008 same longitude") {
  const double d = haversineMeters({20.0, 30.0}, {21.0, 30.0});
  CHECK(std::isfinite(d));
  CHECK(d > 0.0);
  CHECK(d == doctest::Approx(kMetersPerDegree).epsilon(1e-9));
}

TEST_CASE("GEO-009 same latitude") {
  const double d = haversineMeters({20.0, 30.0}, {20.0, 31.0});
  CHECK(std::isfinite(d));
  CHECK(d > 0.0);
  // Shorter than a degree of latitude because meridians converge.
  CHECK(d < kMetersPerDegree);
  CHECK(d == doctest::Approx(kMetersPerDegree * std::cos(20.0 * kPi / 180.0)).epsilon(1e-4));
}

TEST_CASE("GEO-010 latitude +90 is accepted") {
  CHECK(isValidCoordinates({90.0, 0.0}));
  const double d = haversineMeters({90.0, 0.0}, {89.0, 0.0});
  CHECK(std::isfinite(d));
  CHECK(d == doctest::Approx(kMetersPerDegree).epsilon(1e-9));
}

TEST_CASE("GEO-011 latitude -90 is accepted") {
  CHECK(isValidCoordinates({-90.0, 0.0}));
  const double d = haversineMeters({-90.0, 0.0}, {-89.0, 0.0});
  CHECK(std::isfinite(d));
  CHECK(d == doctest::Approx(kMetersPerDegree).epsilon(1e-9));
}

TEST_CASE("GEO-012 latitude 90.00001 is rejected (core defensive check)") {
  CHECK_FALSE(isValidCoordinates({90.00001, 0.0}));
  CHECK_FALSE(isValidCoordinates({-90.00001, 0.0}));
}

TEST_CASE("GEO-013 longitude 180.00001 is rejected (core defensive check)") {
  CHECK_FALSE(isValidCoordinates({0.0, 180.00001}));
  CHECK_FALSE(isValidCoordinates({0.0, -180.00001}));
  CHECK(isValidCoordinates({0.0, 180.0}));
  CHECK(isValidCoordinates({0.0, -180.0}));
}

TEST_CASE("GEO-014 NaN latitude is rejected (core defensive check)") {
  CHECK_FALSE(isValidCoordinates({kNaN, 0.0}));
  CHECK_FALSE(isValidCoordinates({0.0, kNaN}));
  CHECK_FALSE(isValidCoordinates({kInf, 0.0}));
}

TEST_CASE("GEO-015 infinite radius is rejected (core defensive check)") {
  CHECK_FALSE(isValidRadiusMeters(kInf));
  CHECK_FALSE(isValidRadiusMeters(kNaN));
}

TEST_CASE("GEO-016 radius zero is rejected") {
  CHECK_FALSE(isValidRadiusMeters(0.0));
}

TEST_CASE("GEO-017 negative radius is rejected") {
  CHECK_FALSE(isValidRadiusMeters(-1.0));
}

TEST_CASE("GEO-018 radius 100000 is accepted") {
  CHECK(isValidRadiusMeters(100000.0));
  CHECK(isValidRadiusMeters(1.0));
}

TEST_CASE("GEO-019 radius 100001 is rejected") {
  CHECK_FALSE(isValidRadiusMeters(100001.0));
}

TEST_CASE("GEO-026 negative zero is accepted and normalized to (0,0)") {
  const Coordinates negativeZero{-0.0, -0.0};
  REQUIRE(isValidCoordinates(negativeZero));
  const Coordinates normalized = normalizeCoordinates(negativeZero);
  CHECK(normalized.latitudeDeg == 0.0);
  CHECK(normalized.longitudeDeg == 0.0);
  CHECK_FALSE(std::signbit(normalized.latitudeDeg));
  CHECK_FALSE(std::signbit(normalized.longitudeDeg));
  CHECK(haversineMeters(negativeZero, {0.0, 0.0}) == 0.0);
}
