// Syntactic validation and normalization of fixes coming from the platform.
//
// A fix that fails here is counted but never stored and never used. A fix
// that passes is only "syntactically valid": whether it is good enough for a
// decision is judged later (age, accuracy, consistency).
#ifndef LCE_SAMPLE_VALIDATOR_HPP
#define LCE_SAMPLE_VALIDATOR_HPP

#include <cstdint>

#include "ReasonCodes.hpp"
#include "Types.hpp"

namespace lce {

// A fix as delivered by a platform adapter, before any trust is placed in it.
struct RawFix {
  double latitudeDeg = 0.0;
  double longitudeDeg = 0.0;

  bool hasAccuracy = false;
  double horizontalAccuracyMeters = 0.0;

  std::int64_t fixTimeMonotonicMs = 0;
  // False when the adapter could not map the fix time onto the monotonic
  // clock reliably (wall clock change, reboot, inconsistent source).
  bool timeReliable = true;

  bool approximatePermission = false;

  bool hasSpeed = false;
  double speedMps = 0.0;
  bool hasSpeedAccuracy = false;
  double speedAccuracyMps = 0.0;
  bool hasCourse = false;
  double courseDeg = 0.0;

  Provider provider = Provider::Fused;
  MockFlag mockFlag = MockFlag::Unknown;

  std::uint32_t acquiredByRequest = 0;
  AcquisitionPhase phase = AcquisitionPhase::Monitoring;
};

struct SampleCheck {
  bool accepted = false;
  // Why the fix was rejected. Meaningful only when `accepted` is false.
  ReasonCode rejection = ReasonCode::NativeInternalError;
  // Normalized sample. Meaningful only when `accepted` is true.
  LocationSample sample;
  // Non-fatal observations: dropped metadata, clamped timestamp, mock flag.
  ReasonSet notes;
};

SampleCheck validateSample(const RawFix& raw, std::int64_t nowMonotonicMs) noexcept;

}  // namespace lce

#endif  // LCE_SAMPLE_VALIDATOR_HPP
