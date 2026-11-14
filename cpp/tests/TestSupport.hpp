// Shared helpers for host tests. Synthetic coordinates only.
#ifndef LCE_TEST_SUPPORT_HPP
#define LCE_TEST_SUPPORT_HPP

#include <cstdint>
#include <random>

#include "ProximityRelation.hpp"
#include "QualityScore.hpp"
#include "SampleValidator.hpp"
#include "Types.hpp"

namespace lce_test {

// A raw fix that passes validation unless a test breaks one field.
inline lce::RawFix validRawFix(std::int64_t fixTimeMs = 1000) {
  lce::RawFix raw;
  raw.latitudeDeg = -20.8117;
  raw.longitudeDeg = -49.3768;
  raw.hasAccuracy = true;
  raw.horizontalAccuracyMeters = 10.0;
  raw.fixTimeMonotonicMs = fixTimeMs;
  raw.timeReliable = true;
  return raw;
}

inline lce::LocationSample sampleAt(std::int64_t fixTimeMs,
                                    double latitudeDeg = 0.0,
                                    double longitudeDeg = 0.0,
                                    double accuracyMeters = 10.0) {
  lce::LocationSample sample;
  sample.position = lce::Coordinates{latitudeDeg, longitudeDeg};
  sample.horizontalAccuracyMeters = accuracyMeters;
  sample.fixTimeMonotonicMs = fixTimeMs;
  return sample;
}

// Best possible candidate: every factor at 1, no cap applies.
inline lce::ScoreInput perfectScoreInput() {
  lce::ScoreInput input;
  input.hasCandidate = true;
  input.ageReliable = true;
  input.horizontalAccuracyMeters = 0.0;
  input.ageMs = 0.0;
  input.movementAllowanceMeters = 0.0;
  input.movementUnknown = false;
  input.consistency = lce::Consistency::ThreeOrMoreCoherent;
  input.source = lce::FixSource::Fresh;
  input.approximatePermission = false;
  input.suspectedJump = false;
  input.mockFlag = lce::MockFlag::NotReported;
  input.mockPolicy = lce::MockPolicy::ZeroScore;
  return input;
}

inline lce::RelationGates openGates() {
  lce::RelationGates gates;
  gates.hasCandidate = true;
  gates.ageReliable = true;
  gates.withinFallbackAge = true;
  gates.permissionAuthorized = true;
  gates.strongJumpSuspicion = false;
  gates.blockedByMockPolicy = false;
  return gates;
}

// Deterministic generator. std::mt19937_64 is fully specified by the
// standard; the standard distributions are not, so the mapping to a range
// is done here to get the same sequence on every standard library.
class DeterministicRng {
 public:
  explicit DeterministicRng(std::uint64_t seed) : engine_(seed) {}

  // Uniform in [0, 1).
  double unit() { return static_cast<double>(engine_() >> 11) * (1.0 / 9007199254740992.0); }

  double uniform(double low, double high) { return low + (high - low) * unit(); }

  std::uint64_t below(std::uint64_t bound) { return engine_() % bound; }

 private:
  std::mt19937_64 engine_;
};

}  // namespace lce_test

#endif  // LCE_TEST_SUPPORT_HPP
