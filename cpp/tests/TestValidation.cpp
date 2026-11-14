#include "third_party/doctest.h"

#include <limits>

#include "ReadingChecker.hpp"
#include "TestSupport.hpp"

using namespace lce;
using lce_test::validUncheckedReading;

namespace {

constexpr std::int64_t kNow = 100000;
constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();
constexpr double kInf = std::numeric_limits<double>::infinity();

}  // namespace

TEST_CASE("validation: a well-formed fix is accepted unchanged") {
  const UncheckedReading raw = validUncheckedReading(kNow - 2000);
  const ReadingCheck check = checkReading(raw, kNow);
  REQUIRE(check.accepted);
  CHECK(check.warnings.empty());
  CHECK(check.reading.position.latitudeDegrees == raw.latitudeDegrees);
  CHECK(check.reading.position.longitudeDegrees == raw.longitudeDegrees);
  CHECK(check.reading.horizontalAccuracyMeters == 10.0);
  CHECK(check.reading.capturedAtMs == kNow - 2000);
}

TEST_CASE("VAL-001 fix without accuracy is not eligible") {
  UncheckedReading raw = validUncheckedReading(kNow);
  raw.hasAccuracy = false;
  const ReadingCheck check = checkReading(raw, kNow);
  CHECK_FALSE(check.accepted);
  CHECK(check.rejectionReason == ReasonCode::LocationAccuracyUnavailable);
}

TEST_CASE("VAL-002 accuracy zero is syntactically valid") {
  UncheckedReading raw = validUncheckedReading(kNow);
  raw.horizontalAccuracyMeters = 0.0;
  const ReadingCheck check = checkReading(raw, kNow);
  REQUIRE(check.accepted);
  CHECK(check.reading.horizontalAccuracyMeters == 0.0);
}

TEST_CASE("VAL-003 negative accuracy is discarded") {
  UncheckedReading raw = validUncheckedReading(kNow);
  raw.horizontalAccuracyMeters = -1.0;
  const ReadingCheck check = checkReading(raw, kNow);
  CHECK_FALSE(check.accepted);
  CHECK(check.rejectionReason == ReasonCode::LocationInvalidAccuracy);
}

TEST_CASE("VAL-004 non-finite accuracy is discarded before any arithmetic") {
  UncheckedReading raw = validUncheckedReading(kNow);
  raw.horizontalAccuracyMeters = kNaN;
  CHECK_FALSE(checkReading(raw, kNow).accepted);
  CHECK(checkReading(raw, kNow).rejectionReason == ReasonCode::LocationInvalidAccuracy);
  raw.horizontalAccuracyMeters = kInf;
  CHECK_FALSE(checkReading(raw, kNow).accepted);
}

TEST_CASE("VAL-005 latitude 91 is discarded") {
  UncheckedReading raw = validUncheckedReading(kNow);
  raw.latitudeDegrees = 91.0;
  const ReadingCheck check = checkReading(raw, kNow);
  CHECK_FALSE(check.accepted);
  CHECK(check.rejectionReason == ReasonCode::LocationInvalidCoordinates);
}

TEST_CASE("VAL-006 longitude -181 is discarded") {
  UncheckedReading raw = validUncheckedReading(kNow);
  raw.longitudeDegrees = -181.0;
  const ReadingCheck check = checkReading(raw, kNow);
  CHECK_FALSE(check.accepted);
  CHECK(check.rejectionReason == ReasonCode::LocationInvalidCoordinates);
}

TEST_CASE("VAL-007 timestamp two minutes in the future is not trusted") {
  const UncheckedReading raw = validUncheckedReading(kNow + 120000);
  const ReadingCheck check = checkReading(raw, kNow);
  CHECK_FALSE(check.accepted);
  CHECK(check.rejectionReason == ReasonCode::LocationFutureTimestamp);
}

TEST_CASE("VAL-008 small future timestamp is clamped to age zero with a reason") {
  SUBCASE("inside the tolerance") {
    const ReadingCheck check = checkReading(validUncheckedReading(kNow + 500), kNow);
    REQUIRE(check.accepted);
    CHECK(check.reading.capturedAtMs == kNow);
    CHECK(ageInMs(check.reading, kNow) == 0);
    CHECK(check.warnings.contains(ReasonCode::LocationFutureTimestamp));
  }
  SUBCASE("exactly at the tolerance") {
    const ReadingCheck check =
        checkReading(validUncheckedReading(kNow + CLOCK_JITTER_TOLERANCE_MS), kNow);
    CHECK(check.accepted);
  }
  SUBCASE("one millisecond beyond the tolerance") {
    const ReadingCheck check =
        checkReading(validUncheckedReading(kNow + CLOCK_JITTER_TOLERANCE_MS + 1), kNow);
    CHECK_FALSE(check.accepted);
  }
}

TEST_CASE("VAL-009 age grows with the monotonic clock (core part)") {
  const ReadingCheck check = checkReading(validUncheckedReading(kNow - 2000), kNow);
  REQUIRE(check.accepted);
  CHECK(ageInMs(check.reading, kNow) == 2000);
  CHECK(ageInMs(check.reading, kNow + 3000) == 5000);
}

TEST_CASE("VAL-011 unreliable or negative time base is rejected") {
  UncheckedReading raw = validUncheckedReading(kNow);
  raw.timeReliable = false;
  ReadingCheck check = checkReading(raw, kNow);
  CHECK_FALSE(check.accepted);
  CHECK(check.rejectionReason == ReasonCode::LocationAgeUnreliable);

  raw = validUncheckedReading(-5);
  check = checkReading(raw, kNow);
  CHECK_FALSE(check.accepted);
  CHECK(check.rejectionReason == ReasonCode::LocationInvalidTimestamp);
}

TEST_CASE("VAL-019 approximate permission is carried and reported (validator part)") {
  UncheckedReading raw = validUncheckedReading(kNow);
  raw.onlyApproximatePermission = true;
  raw.horizontalAccuracyMeters = 5000.0;
  const ReadingCheck check = checkReading(raw, kNow);
  REQUIRE(check.accepted);
  CHECK(check.reading.onlyApproximatePermission);
  CHECK(check.warnings.contains(ReasonCode::PrecisionReduced));
}

TEST_CASE("VAL-020 negative speed is ignored, fix is kept") {
  UncheckedReading raw = validUncheckedReading(kNow);
  raw.hasSpeed = true;
  raw.speedMetersPerSecond = -1.0;
  const ReadingCheck check = checkReading(raw, kNow);
  REQUIRE(check.accepted);
  CHECK_FALSE(check.reading.speedMetersPerSecond.has_value());
  CHECK(check.warnings.contains(ReasonCode::LocationMetadataInconsistent));
}

TEST_CASE("VAL-021 invalid course does not break the fix") {
  UncheckedReading raw = validUncheckedReading(kNow);
  raw.hasHeading = true;

  raw.headingDegrees = 361.0;
  ReadingCheck check = checkReading(raw, kNow);
  REQUIRE(check.accepted);
  CHECK_FALSE(check.reading.headingDegrees.has_value());
  CHECK(check.warnings.contains(ReasonCode::LocationMetadataInconsistent));

  raw.headingDegrees = 360.0;
  check = checkReading(raw, kNow);
  REQUIRE(check.accepted);
  REQUIRE(check.reading.headingDegrees.has_value());
  CHECK(*check.reading.headingDegrees == 0.0);

  raw.headingDegrees = 359.5;
  check = checkReading(raw, kNow);
  REQUIRE(check.reading.headingDegrees.has_value());
  CHECK(*check.reading.headingDegrees == 359.5);

  raw.headingDegrees = kNaN;
  check = checkReading(raw, kNow);
  REQUIRE(check.accepted);
  CHECK_FALSE(check.reading.headingDegrees.has_value());
}

TEST_CASE("VAL-022 missing speed and course are not inferred") {
  const ReadingCheck check = checkReading(validUncheckedReading(kNow), kNow);
  REQUIRE(check.accepted);
  CHECK_FALSE(check.reading.speedMetersPerSecond.has_value());
  CHECK_FALSE(check.reading.speedAccuracyMetersPerSecond.has_value());
  CHECK_FALSE(check.reading.headingDegrees.has_value());
}

TEST_CASE("validation: valid speed metadata is kept, invalid speed accuracy is dropped") {
  UncheckedReading raw = validUncheckedReading(kNow);
  raw.hasSpeed = true;
  raw.speedMetersPerSecond = 1.5;
  raw.hasSpeedAccuracy = true;
  raw.speedAccuracyMetersPerSecond = kNaN;
  const ReadingCheck check = checkReading(raw, kNow);
  REQUIRE(check.accepted);
  REQUIRE(check.reading.speedMetersPerSecond.has_value());
  CHECK(*check.reading.speedMetersPerSecond == 1.5);
  CHECK_FALSE(check.reading.speedAccuracyMetersPerSecond.has_value());
}

TEST_CASE("VAL-023 mock flag reported is carried with a reason (validator part)") {
  UncheckedReading raw = validUncheckedReading(kNow);
  raw.mockSignal = MockSignal::FlaggedByOs;
  const ReadingCheck check = checkReading(raw, kNow);
  REQUIRE(check.accepted);
  CHECK(check.reading.mockSignal == MockSignal::FlaggedByOs);
  CHECK(check.warnings.contains(ReasonCode::PossibleMockLocation));
}

TEST_CASE("VAL-024 unknown mock flag stays unknown (validator part)") {
  UncheckedReading raw = validUncheckedReading(kNow);
  raw.mockSignal = MockSignal::Unknown;
  const ReadingCheck check = checkReading(raw, kNow);
  REQUIRE(check.accepted);
  CHECK(check.reading.mockSignal == MockSignal::Unknown);
  CHECK_FALSE(check.warnings.contains(ReasonCode::PossibleMockLocation));
}

TEST_CASE("VAL-026 a real fix at (0,0) keeps its position") {
  UncheckedReading raw = validUncheckedReading(kNow);
  raw.latitudeDegrees = 0.0;
  raw.longitudeDegrees = 0.0;
  const ReadingCheck check = checkReading(raw, kNow);
  REQUIRE(check.accepted);
  CHECK(check.reading.position.latitudeDegrees == 0.0);
  CHECK(check.reading.position.longitudeDegrees == 0.0);
}
