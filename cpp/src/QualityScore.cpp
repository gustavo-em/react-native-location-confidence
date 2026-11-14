#include "QualityScore.hpp"

#include <cmath>

namespace lce {

namespace {

// heuristic-v1 parameters. Hypotheses, not calibrated values.
constexpr double kAccuracyScaleMeters = 100.0;
constexpr double kFreshnessScaleMs = 60000.0;
constexpr double kMovementScaleMeters = 100.0;

constexpr double kAccuracyWeight = 0.40;
constexpr double kFreshnessWeight = 0.25;
constexpr double kMovementWeight = 0.20;
constexpr double kConsistencyWeight = 0.15;

// Values that are mathematically x.5 can land a few ulps below it in binary
// floating point. This bias makes them round up consistently.
constexpr double kRoundingBias = 1e-9;

constexpr int kCapLimits[] = {
    60,  // Cached
    30,  // ApproximatePermission
    40,  // UnknownMovementNotFresh
    35,  // UncertaintyAtLeast100
    15,  // UncertaintyAtLeast300
    75,  // SingleFix
    75,  // ConflictingEvidence
    20,  // SuspectedJump
    0,   // MockLocation (ZeroScore policy)
    0,   // AgeUnreliable
    0,   // NoCandidate
};

constexpr const char* kCapNames[] = {
    "cached",
    "approximate_permission",
    "unknown_movement_not_fresh",
    "uncertainty_at_least_100m",
    "uncertainty_at_least_300m",
    "single_fix",
    "conflicting_evidence",
    "suspected_jump",
    "mock_location",
    "age_unreliable",
    "no_candidate",
};

constexpr std::size_t kCapCount = static_cast<std::size_t>(ScoreCap::Count);

static_assert(sizeof(kCapLimits) / sizeof(kCapLimits[0]) == kCapCount, "cap limits out of sync");
static_assert(sizeof(kCapNames) / sizeof(kCapNames[0]) == kCapCount, "cap names out of sync");

double clampUnit(double value) noexcept {
  if (value < 0.0) return 0.0;
  if (value > 1.0) return 1.0;
  return value;
}

double consistencyFactor(Consistency consistency) noexcept {
  switch (consistency) {
    case Consistency::SingleFix:
      return 0.60;
    case Consistency::TwoCoherent:
      return 0.80;
    case Consistency::ThreeOrMoreCoherent:
      return 1.00;
    case Consistency::Conflicting:
      return 0.30;
  }
  return 0.30;
}

void applyCap(ScoreResult& result, ScoreCap cap) noexcept {
  result.appliedCapsMask |= std::uint32_t{1} << static_cast<unsigned>(cap);
  const int limit = scoreCapLimit(cap);
  if (result.qualityScore > limit) {
    result.qualityScore = limit;
  }
}

ScoreResult zeroScore(ScoreCap cap) noexcept {
  ScoreResult result;
  result.appliedCapsMask = std::uint32_t{1} << static_cast<unsigned>(cap);
  return result;
}

}  // namespace

Consistency consistencyFromDistinctCoherent(std::size_t distinctCoherentFixes,
                                            bool conflictingEvidence) noexcept {
  if (conflictingEvidence) return Consistency::Conflicting;
  if (distinctCoherentFixes >= 3) return Consistency::ThreeOrMoreCoherent;
  if (distinctCoherentFixes == 2) return Consistency::TwoCoherent;
  return Consistency::SingleFix;
}

const char* scoreCapName(ScoreCap cap) noexcept {
  const auto index = static_cast<std::size_t>(cap);
  return index < kCapCount ? kCapNames[index] : nullptr;
}

int scoreCapLimit(ScoreCap cap) noexcept {
  const auto index = static_cast<std::size_t>(cap);
  return index < kCapCount ? kCapLimits[index] : 0;
}

int roundHalfUp(double value) noexcept {
  return static_cast<int>(std::floor(value + 0.5 + kRoundingBias));
}

ScoreResult evaluateQualityScore(const ScoreInput& input) noexcept {
  if (!input.hasCandidate) {
    return zeroScore(ScoreCap::NoCandidate);
  }
  if (!input.ageReliable) {
    return zeroScore(ScoreCap::AgeUnreliable);
  }

  const bool inputsUsable =
      std::isfinite(input.horizontalAccuracyMeters) && input.horizontalAccuracyMeters >= 0.0 &&
      std::isfinite(input.ageMs) && input.ageMs >= 0.0 &&
      std::isfinite(input.movementAllowanceMeters) && input.movementAllowanceMeters >= 0.0;
  if (!inputsUsable) {
    // Validation upstream should make this unreachable; never score garbage.
    return zeroScore(ScoreCap::NoCandidate);
  }

  ScoreResult result;
  result.hasBreakdown = true;
  result.accuracyFactor = clampUnit(1.0 - input.horizontalAccuracyMeters / kAccuracyScaleMeters);
  result.freshnessFactor = clampUnit(1.0 - input.ageMs / kFreshnessScaleMs);
  result.movementFactor = clampUnit(1.0 - input.movementAllowanceMeters / kMovementScaleMeters);
  result.consistencyFactor = consistencyFactor(input.consistency);
  result.effectiveUncertaintyMeters =
      input.horizontalAccuracyMeters + input.movementAllowanceMeters;

  const double weighted = kAccuracyWeight * result.accuracyFactor +
                          kFreshnessWeight * result.freshnessFactor +
                          kMovementWeight * result.movementFactor +
                          kConsistencyWeight * result.consistencyFactor;

  int raw = roundHalfUp(100.0 * weighted);
  if (raw < 0) raw = 0;
  if (raw > 100) raw = 100;
  result.rawScore = raw;
  result.qualityScore = raw;

  // The lowest applicable cap wins; every applicable cap is recorded.
  if (input.source == FixSource::Cached) {
    applyCap(result, ScoreCap::Cached);
    if (input.movementUnknown) {
      applyCap(result, ScoreCap::UnknownMovementNotFresh);
    }
  }
  if (input.approximatePermission) {
    applyCap(result, ScoreCap::ApproximatePermission);
  }
  if (result.effectiveUncertaintyMeters >= 100.0) {
    applyCap(result, ScoreCap::UncertaintyAtLeast100);
  }
  if (result.effectiveUncertaintyMeters >= 300.0) {
    applyCap(result, ScoreCap::UncertaintyAtLeast300);
  }
  if (input.consistency == Consistency::SingleFix) {
    applyCap(result, ScoreCap::SingleFix);
  }
  if (input.consistency == Consistency::Conflicting) {
    // Not in the spec table. Without it, adding a conflicting reading to a
    // single good fix would lift the 75 cap and raise the score, which the
    // spec forbids (an outlier must never increase the score).
    applyCap(result, ScoreCap::ConflictingEvidence);
  }
  if (input.suspectedJump) {
    applyCap(result, ScoreCap::SuspectedJump);
  }
  if (input.mockFlag == MockFlag::Reported && input.mockPolicy == MockPolicy::ZeroScore) {
    applyCap(result, ScoreCap::MockLocation);
  }

  return result;
}

}  // namespace lce
