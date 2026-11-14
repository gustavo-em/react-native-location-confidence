// The entrance check for location readings.
//
// The phone's GPS service can hand us broken data: a latitude of 91, a
// negative accuracy, a timestamp from the future. Before any reading is
// stored or used, it passes through `checkReading`.
//
// A reading that fails is counted and thrown away.
// A reading that passes is only "well-formed". Whether it is GOOD enough to
// support a decision (recent, precise, consistent) is judged later.
#ifndef LCE_READING_CHECKER_HPP
#define LCE_READING_CHECKER_HPP

#include <cstdint>

#include "ReasonCodes.hpp"
#include "Types.hpp"

namespace lce {

// A reading exactly as the platform code delivered it. Nothing in it has
// been verified yet, so every field is treated with suspicion.
//
// The "has..." switches exist because plain numbers cannot say "missing":
// `hasSpeed = false` means the phone did not report a speed at all.
struct UncheckedReading {
  double latitudeDegrees = 0.0;
  double longitudeDegrees = 0.0;

  bool hasAccuracy = false;
  double horizontalAccuracyMeters = 0.0;

  // When the reading was taken, on the phone's monotonic clock.
  std::int64_t capturedAtMs = 0;
  // False when the platform code could not work out the capture time safely
  // (for example the user changed the clock, or the phone restarted).
  bool timeReliable = true;

  bool onlyApproximatePermission = false;

  bool hasSpeed = false;
  double speedMetersPerSecond = 0.0;
  bool hasSpeedAccuracy = false;
  double speedAccuracyMetersPerSecond = 0.0;
  bool hasHeading = false;
  double headingDegrees = 0.0;

  Provider provider = Provider::Fused;
  MockSignal mockSignal = MockSignal::Unknown;

  std::uint32_t obtainedByRequestId = 0;
  AcquisitionPhase obtainedDuring = AcquisitionPhase::Monitoring;
};

// The outcome of the entrance check.
struct ReadingCheck {
  // True: the reading is well-formed and `reading` can be used.
  // False: the reading was thrown away and `rejectionReason` says why.
  bool accepted = false;

  // Only meaningful when `accepted` is false.
  ReasonCode rejectionReason = ReasonCode::NativeInternalError;

  // The cleaned-up reading. Only meaningful when `accepted` is true.
  LocationReading reading;

  // Things worth reporting that did not stop the reading from being
  // accepted, for example "the speed value was invalid and was ignored".
  ReasonSet warnings;
};

// Is this reading well-formed? If yes, return a cleaned-up copy.
// `nowMs` is the current time on the same monotonic clock as `capturedAtMs`.
ReadingCheck checkReading(const UncheckedReading& unchecked, std::int64_t nowMs) noexcept;

}  // namespace lce

#endif  // LCE_READING_CHECKER_HPP
