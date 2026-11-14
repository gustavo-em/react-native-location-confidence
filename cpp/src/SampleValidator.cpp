#include "SampleValidator.hpp"

#include <cmath>

#include "GeoMath.hpp"

namespace lce {

namespace {

SampleCheck reject(ReasonCode reason) noexcept {
  SampleCheck check;
  check.accepted = false;
  check.rejection = reason;
  return check;
}

bool isFiniteNonNegative(double value) noexcept {
  return std::isfinite(value) && value >= 0.0;
}

}  // namespace

SampleCheck validateSample(const RawFix& raw, std::int64_t nowMonotonicMs) noexcept {
  // Coordinates are checked before any arithmetic touches them.
  const Coordinates rawPosition{raw.latitudeDeg, raw.longitudeDeg};
  if (!isValidCoordinates(rawPosition)) {
    return reject(ReasonCode::LocationInvalidCoordinates);
  }

  if (!raw.hasAccuracy) {
    return reject(ReasonCode::LocationAccuracyUnavailable);
  }
  if (!isFiniteNonNegative(raw.horizontalAccuracyMeters)) {
    return reject(ReasonCode::LocationInvalidAccuracy);
  }

  // Without a trustworthy age the fix cannot be ordered, aged or compared,
  // so it is never stored.
  if (!raw.timeReliable) {
    return reject(ReasonCode::LocationAgeUnreliable);
  }
  if (raw.fixTimeMonotonicMs < 0) {
    return reject(ReasonCode::LocationInvalidTimestamp);
  }

  SampleCheck check;
  check.accepted = true;

  std::int64_t fixTime = raw.fixTimeMonotonicMs;
  if (fixTime > nowMonotonicMs) {
    if (fixTime - nowMonotonicMs > kFutureTimestampToleranceMs) {
      return reject(ReasonCode::LocationFutureTimestamp);
    }
    // Small lead: clock jitter. Age becomes 0 and the clamp is reported.
    fixTime = nowMonotonicMs;
    check.notes.add(ReasonCode::LocationFutureTimestamp);
  }

  LocationSample& sample = check.sample;
  sample.position = normalizeCoordinates(rawPosition);
  sample.horizontalAccuracyMeters = raw.horizontalAccuracyMeters;
  sample.fixTimeMonotonicMs = fixTime;
  sample.approximatePermission = raw.approximatePermission;
  sample.provider = raw.provider;
  sample.mockFlag = raw.mockFlag;
  sample.acquiredByRequest = raw.acquiredByRequest;
  sample.phase = raw.phase;

  // Optional metadata: invalid values are dropped, never replaced by zero.
  if (raw.hasSpeed) {
    if (isFiniteNonNegative(raw.speedMps)) {
      sample.speedMps = raw.speedMps;
    } else {
      check.notes.add(ReasonCode::LocationMetadataInconsistent);
    }
  }
  if (raw.hasSpeedAccuracy) {
    if (isFiniteNonNegative(raw.speedAccuracyMps)) {
      sample.speedAccuracyMps = raw.speedAccuracyMps;
    } else {
      check.notes.add(ReasonCode::LocationMetadataInconsistent);
    }
  }
  if (raw.hasCourse) {
    if (std::isfinite(raw.courseDeg) && raw.courseDeg >= 0.0 && raw.courseDeg < 360.0) {
      sample.courseDeg = raw.courseDeg;
    } else if (raw.courseDeg == 360.0) {
      sample.courseDeg = 0.0;
    } else {
      check.notes.add(ReasonCode::LocationMetadataInconsistent);
    }
  }

  if (raw.approximatePermission) {
    check.notes.add(ReasonCode::PrecisionReduced);
  }
  if (raw.mockFlag == MockFlag::Reported) {
    check.notes.add(ReasonCode::PossibleMockLocation);
  }

  return check;
}

}  // namespace lce
