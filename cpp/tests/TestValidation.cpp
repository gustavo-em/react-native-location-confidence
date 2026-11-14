#include "third_party/doctest.h"

#include <limits>

#include "SampleValidator.hpp"
#include "TestSupport.hpp"

using namespace lce;
using lce_test::validRawFix;

namespace {

constexpr std::int64_t kNow = 100000;
constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();
constexpr double kInf = std::numeric_limits<double>::infinity();

}  // namespace

TEST_CASE("validation: a well-formed fix is accepted unchanged") {
  const RawFix raw = validRawFix(kNow - 2000);
  const SampleCheck check = validateSample(raw, kNow);
  REQUIRE(check.accepted);
  CHECK(check.notes.empty());
  CHECK(check.sample.position.latitudeDeg == raw.latitudeDeg);
  CHECK(check.sample.position.longitudeDeg == raw.longitudeDeg);
  CHECK(check.sample.horizontalAccuracyMeters == 10.0);
  CHECK(check.sample.fixTimeMonotonicMs == kNow - 2000);
}

TEST_CASE("VAL-001 fix without accuracy is not eligible") {
  RawFix raw = validRawFix(kNow);
  raw.hasAccuracy = false;
  const SampleCheck check = validateSample(raw, kNow);
  CHECK_FALSE(check.accepted);
  CHECK(check.rejection == ReasonCode::LocationAccuracyUnavailable);
}

TEST_CASE("VAL-002 accuracy zero is syntactically valid") {
  RawFix raw = validRawFix(kNow);
  raw.horizontalAccuracyMeters = 0.0;
  const SampleCheck check = validateSample(raw, kNow);
  REQUIRE(check.accepted);
  CHECK(check.sample.horizontalAccuracyMeters == 0.0);
}

TEST_CASE("VAL-003 negative accuracy is discarded") {
  RawFix raw = validRawFix(kNow);
  raw.horizontalAccuracyMeters = -1.0;
  const SampleCheck check = validateSample(raw, kNow);
  CHECK_FALSE(check.accepted);
  CHECK(check.rejection == ReasonCode::LocationInvalidAccuracy);
}

TEST_CASE("VAL-004 non-finite accuracy is discarded before any arithmetic") {
  RawFix raw = validRawFix(kNow);
  raw.horizontalAccuracyMeters = kNaN;
  CHECK_FALSE(validateSample(raw, kNow).accepted);
  CHECK(validateSample(raw, kNow).rejection == ReasonCode::LocationInvalidAccuracy);
  raw.horizontalAccuracyMeters = kInf;
  CHECK_FALSE(validateSample(raw, kNow).accepted);
}

TEST_CASE("VAL-005 latitude 91 is discarded") {
  RawFix raw = validRawFix(kNow);
  raw.latitudeDeg = 91.0;
  const SampleCheck check = validateSample(raw, kNow);
  CHECK_FALSE(check.accepted);
  CHECK(check.rejection == ReasonCode::LocationInvalidCoordinates);
}

TEST_CASE("VAL-006 longitude -181 is discarded") {
  RawFix raw = validRawFix(kNow);
  raw.longitudeDeg = -181.0;
  const SampleCheck check = validateSample(raw, kNow);
  CHECK_FALSE(check.accepted);
  CHECK(check.rejection == ReasonCode::LocationInvalidCoordinates);
}

TEST_CASE("VAL-007 timestamp two minutes in the future is not trusted") {
  const RawFix raw = validRawFix(kNow + 120000);
  const SampleCheck check = validateSample(raw, kNow);
  CHECK_FALSE(check.accepted);
  CHECK(check.rejection == ReasonCode::LocationFutureTimestamp);
}

TEST_CASE("VAL-008 small future timestamp is clamped to age zero with a reason") {
  SUBCASE("inside the tolerance") {
    const SampleCheck check = validateSample(validRawFix(kNow + 500), kNow);
    REQUIRE(check.accepted);
    CHECK(check.sample.fixTimeMonotonicMs == kNow);
    CHECK(ageMs(check.sample, kNow) == 0);
    CHECK(check.notes.contains(ReasonCode::LocationFutureTimestamp));
  }
  SUBCASE("exactly at the tolerance") {
    const SampleCheck check =
        validateSample(validRawFix(kNow + kFutureTimestampToleranceMs), kNow);
    CHECK(check.accepted);
  }
  SUBCASE("one millisecond beyond the tolerance") {
    const SampleCheck check =
        validateSample(validRawFix(kNow + kFutureTimestampToleranceMs + 1), kNow);
    CHECK_FALSE(check.accepted);
  }
}

TEST_CASE("VAL-009 age grows with the monotonic clock (core part)") {
  const SampleCheck check = validateSample(validRawFix(kNow - 2000), kNow);
  REQUIRE(check.accepted);
  CHECK(ageMs(check.sample, kNow) == 2000);
  CHECK(ageMs(check.sample, kNow + 3000) == 5000);
}

TEST_CASE("VAL-011 unreliable or negative time base is rejected") {
  RawFix raw = validRawFix(kNow);
  raw.timeReliable = false;
  SampleCheck check = validateSample(raw, kNow);
  CHECK_FALSE(check.accepted);
  CHECK(check.rejection == ReasonCode::LocationAgeUnreliable);

  raw = validRawFix(-5);
  check = validateSample(raw, kNow);
  CHECK_FALSE(check.accepted);
  CHECK(check.rejection == ReasonCode::LocationInvalidTimestamp);
}

TEST_CASE("VAL-019 approximate permission is carried and reported (validator part)") {
  RawFix raw = validRawFix(kNow);
  raw.approximatePermission = true;
  raw.horizontalAccuracyMeters = 5000.0;
  const SampleCheck check = validateSample(raw, kNow);
  REQUIRE(check.accepted);
  CHECK(check.sample.approximatePermission);
  CHECK(check.notes.contains(ReasonCode::PrecisionReduced));
}

TEST_CASE("VAL-020 negative speed is ignored, fix is kept") {
  RawFix raw = validRawFix(kNow);
  raw.hasSpeed = true;
  raw.speedMps = -1.0;
  const SampleCheck check = validateSample(raw, kNow);
  REQUIRE(check.accepted);
  CHECK_FALSE(check.sample.speedMps.has_value());
  CHECK(check.notes.contains(ReasonCode::LocationMetadataInconsistent));
}

TEST_CASE("VAL-021 invalid course does not break the fix") {
  RawFix raw = validRawFix(kNow);
  raw.hasCourse = true;

  raw.courseDeg = 361.0;
  SampleCheck check = validateSample(raw, kNow);
  REQUIRE(check.accepted);
  CHECK_FALSE(check.sample.courseDeg.has_value());
  CHECK(check.notes.contains(ReasonCode::LocationMetadataInconsistent));

  raw.courseDeg = 360.0;
  check = validateSample(raw, kNow);
  REQUIRE(check.accepted);
  REQUIRE(check.sample.courseDeg.has_value());
  CHECK(*check.sample.courseDeg == 0.0);

  raw.courseDeg = 359.5;
  check = validateSample(raw, kNow);
  REQUIRE(check.sample.courseDeg.has_value());
  CHECK(*check.sample.courseDeg == 359.5);

  raw.courseDeg = kNaN;
  check = validateSample(raw, kNow);
  REQUIRE(check.accepted);
  CHECK_FALSE(check.sample.courseDeg.has_value());
}

TEST_CASE("VAL-022 missing speed and course are not inferred") {
  const SampleCheck check = validateSample(validRawFix(kNow), kNow);
  REQUIRE(check.accepted);
  CHECK_FALSE(check.sample.speedMps.has_value());
  CHECK_FALSE(check.sample.speedAccuracyMps.has_value());
  CHECK_FALSE(check.sample.courseDeg.has_value());
}

TEST_CASE("validation: valid speed metadata is kept, invalid speed accuracy is dropped") {
  RawFix raw = validRawFix(kNow);
  raw.hasSpeed = true;
  raw.speedMps = 1.5;
  raw.hasSpeedAccuracy = true;
  raw.speedAccuracyMps = kNaN;
  const SampleCheck check = validateSample(raw, kNow);
  REQUIRE(check.accepted);
  REQUIRE(check.sample.speedMps.has_value());
  CHECK(*check.sample.speedMps == 1.5);
  CHECK_FALSE(check.sample.speedAccuracyMps.has_value());
}

TEST_CASE("VAL-023 mock flag reported is carried with a reason (validator part)") {
  RawFix raw = validRawFix(kNow);
  raw.mockFlag = MockFlag::Reported;
  const SampleCheck check = validateSample(raw, kNow);
  REQUIRE(check.accepted);
  CHECK(check.sample.mockFlag == MockFlag::Reported);
  CHECK(check.notes.contains(ReasonCode::PossibleMockLocation));
}

TEST_CASE("VAL-024 unknown mock flag stays unknown (validator part)") {
  RawFix raw = validRawFix(kNow);
  raw.mockFlag = MockFlag::Unknown;
  const SampleCheck check = validateSample(raw, kNow);
  REQUIRE(check.accepted);
  CHECK(check.sample.mockFlag == MockFlag::Unknown);
  CHECK_FALSE(check.notes.contains(ReasonCode::PossibleMockLocation));
}

TEST_CASE("VAL-026 a real fix at (0,0) keeps its position") {
  RawFix raw = validRawFix(kNow);
  raw.latitudeDeg = 0.0;
  raw.longitudeDeg = 0.0;
  const SampleCheck check = validateSample(raw, kNow);
  REQUIRE(check.accepted);
  CHECK(check.sample.position.latitudeDeg == 0.0);
  CHECK(check.sample.position.longitudeDeg == 0.0);
}
