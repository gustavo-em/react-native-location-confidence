// Deterministic quality index for one candidate fix (model "heuristic-v1").
//
// The result is an integer index from 0 to 100. It is NOT a probability and
// NOT a percentage of certainty: weights and caps are uncalibrated design
// hypotheses. It must never be the sole input of a business decision.
#ifndef LCE_QUALITY_SCORE_HPP
#define LCE_QUALITY_SCORE_HPP

#include <cstddef>
#include <cstdint>

#include "Types.hpp"

namespace lce {

enum class Consistency : std::uint8_t {
  SingleFix,            // one eligible distinct sample
  TwoCoherent,          // two coherent distinct fixes
  ThreeOrMoreCoherent,  // three or more coherent distinct fixes
  Conflicting,          // conflicting evidence, no strong consensus
};

// Duplicated callbacks must already be collapsed: only distinct fixes count.
Consistency consistencyFromDistinctCoherent(std::size_t distinctCoherentFixes,
                                            bool conflictingEvidence) noexcept;

enum class ScoreCap : std::uint8_t {
  Cached = 0,
  ApproximatePermission,
  UnknownMovementNotFresh,
  UncertaintyAtLeast100,
  UncertaintyAtLeast300,
  SingleFix,
  ConflictingEvidence,
  SuspectedJump,
  MockLocation,
  AgeUnreliable,
  NoCandidate,
  Count
};

// Stable snake_case name exposed in scoreBreakdown.appliedCaps.
const char* scoreCapName(ScoreCap cap) noexcept;

// Highest score the cap allows.
int scoreCapLimit(ScoreCap cap) noexcept;

struct ScoreInput {
  bool hasCandidate = false;
  bool ageReliable = false;

  double horizontalAccuracyMeters = 0.0;
  double ageMs = 0.0;
  // Displacement the device may have undergone since the fix (never negative).
  double movementAllowanceMeters = 0.0;
  // True when no evidence bounded the movement and the default bound applied.
  bool movementUnknown = true;

  Consistency consistency = Consistency::SingleFix;
  FixSource source = FixSource::Unavailable;
  bool approximatePermission = false;
  bool suspectedJump = false;
  MockFlag mockFlag = MockFlag::Unknown;
  MockPolicy mockPolicy = MockPolicy::ZeroScore;
};

struct ScoreResult {
  // False when there was nothing to score; factors are then meaningless.
  bool hasBreakdown = false;

  double accuracyFactor = 0.0;
  double freshnessFactor = 0.0;
  double movementFactor = 0.0;
  double consistencyFactor = 0.0;
  double effectiveUncertaintyMeters = 0.0;

  int rawScore = 0;
  int qualityScore = 0;

  // Every cap that applied, not only the binding one.
  std::uint32_t appliedCapsMask = 0;

  bool capApplied(ScoreCap cap) const noexcept {
    return (appliedCapsMask & (std::uint32_t{1} << static_cast<unsigned>(cap))) != 0;
  }
};

// Rounds a non-negative value half up, identically on every platform.
int roundHalfUp(double value) noexcept;

ScoreResult evaluateQualityScore(const ScoreInput& input) noexcept;

}  // namespace lce

#endif  // LCE_QUALITY_SCORE_HPP
