#include "ReadingChecker.hpp"

#include <cmath>

#include "GeoMath.hpp"

namespace lce {

namespace {

ReadingCheck rejectedBecause(ReasonCode reason) noexcept {
  ReadingCheck result;
  result.accepted = false;
  result.rejectionReason = reason;
  return result;
}

// A measurement like accuracy or speed must be a normal number (not infinity,
// not NaN) and cannot be negative.
bool isUsableMeasurement(double value) noexcept {
  return std::isfinite(value) && value >= 0.0;
}

// Copies the optional extras (speed, speed accuracy, heading) into the
// cleaned-up reading.
//
// Rule: a broken extra is dropped, and the reading is still accepted. We
// never replace a broken value with 0, because 0 has a meaning of its own
// ("standing still", "heading north") that would be a lie.
void copyOptionalExtras(const UncheckedReading& unchecked, ReadingCheck& result) noexcept {
  LocationReading& reading = result.reading;

  if (unchecked.hasSpeed) {
    if (isUsableMeasurement(unchecked.speedMetersPerSecond)) {
      reading.speedMetersPerSecond = unchecked.speedMetersPerSecond;
    } else {
      result.warnings.add(ReasonCode::LocationMetadataInconsistent);
    }
  }

  if (unchecked.hasSpeedAccuracy) {
    if (isUsableMeasurement(unchecked.speedAccuracyMetersPerSecond)) {
      reading.speedAccuracyMetersPerSecond = unchecked.speedAccuracyMetersPerSecond;
    } else {
      result.warnings.add(ReasonCode::LocationMetadataInconsistent);
    }
  }

  if (unchecked.hasHeading) {
    const double heading = unchecked.headingDegrees;
    const bool headingIsInRange = std::isfinite(heading) && heading >= 0.0 && heading < 360.0;
    const bool headingIsFullCircle = (heading == 360.0);  // some phones report 360 for north

    if (headingIsInRange) {
      reading.headingDegrees = heading;
    } else if (headingIsFullCircle) {
      reading.headingDegrees = 0.0;
    } else {
      result.warnings.add(ReasonCode::LocationMetadataInconsistent);
    }
  }
}

}  // namespace

ReadingCheck checkReading(const UncheckedReading& unchecked, std::int64_t nowMs) noexcept {
  // --- Check 1: is the position a real place on Earth? ---------------------
  // This runs first so that no calculation ever touches a broken coordinate.
  const Coordinates reportedPosition{unchecked.latitudeDegrees, unchecked.longitudeDegrees};
  if (!isValidCoordinates(reportedPosition)) {
    return rejectedBecause(ReasonCode::LocationInvalidCoordinates);
  }

  // --- Check 2: do we know how precise the reading is? ---------------------
  // Without an accuracy value we cannot tell a 5 m reading from a 5 km one,
  // so the position is useless for deciding anything.
  if (!unchecked.hasAccuracy) {
    return rejectedBecause(ReasonCode::LocationAccuracyUnavailable);
  }
  if (!isUsableMeasurement(unchecked.horizontalAccuracyMeters)) {
    return rejectedBecause(ReasonCode::LocationInvalidAccuracy);
  }

  // --- Check 3: do we know when the reading was taken? ---------------------
  // Age is central to every rule ("is this still where the phone is?").
  // A reading whose time cannot be trusted cannot be aged, sorted or
  // compared with others, so it is thrown away.
  if (!unchecked.timeReliable) {
    return rejectedBecause(ReasonCode::LocationAgeUnreliable);
  }
  if (unchecked.capturedAtMs < 0) {
    return rejectedBecause(ReasonCode::LocationInvalidTimestamp);
  }

  ReadingCheck result;
  result.accepted = true;

  std::int64_t capturedAtMs = unchecked.capturedAtMs;
  const bool claimsToBeFromTheFuture = capturedAtMs > nowMs;
  if (claimsToBeFromTheFuture) {
    const std::int64_t howFarAheadMs = capturedAtMs - nowMs;
    if (howFarAheadMs > CLOCK_JITTER_TOLERANCE_MS) {
      // Too far ahead to be a small clock disagreement: the clock is broken.
      return rejectedBecause(ReasonCode::LocationFutureTimestamp);
    }
    // A few milliseconds ahead is normal. Treat it as "taken right now" and
    // leave a note that we adjusted it.
    capturedAtMs = nowMs;
    result.warnings.add(ReasonCode::LocationFutureTimestamp);
  }

  // --- The reading is well-formed: build the cleaned-up copy ---------------
  LocationReading& reading = result.reading;
  reading.position = normalizeCoordinates(reportedPosition);
  reading.horizontalAccuracyMeters = unchecked.horizontalAccuracyMeters;
  reading.capturedAtMs = capturedAtMs;
  reading.onlyApproximatePermission = unchecked.onlyApproximatePermission;
  reading.provider = unchecked.provider;
  reading.mockSignal = unchecked.mockSignal;
  reading.obtainedByRequestId = unchecked.obtainedByRequestId;
  reading.obtainedDuring = unchecked.obtainedDuring;

  copyOptionalExtras(unchecked, result);

  // --- Notes for the final report ------------------------------------------
  if (unchecked.onlyApproximatePermission) {
    result.warnings.add(ReasonCode::PrecisionReduced);
  }
  if (unchecked.mockSignal == MockSignal::FlaggedByOs) {
    result.warnings.add(ReasonCode::PossibleMockLocation);
  }

  return result;
}

}  // namespace lce
