#include "QualityScore.hpp"

#include <cmath>

namespace lce {

namespace {

// --- The numbers of the "heuristic-v1" rules ---------------------------------
// All of these are design guesses, not calibrated values.

// Where each grade reaches zero:
constexpr double ACCURACY_IS_WORTHLESS_AT_METERS = 100.0;  // 100 m error or worse -> grade 0
constexpr double READING_IS_STALE_AT_MS = 60000.0;         // 1 minute old or more -> grade 0
constexpr double MOVEMENT_IS_TOO_MUCH_AT_METERS = 100.0;   // could have moved 100 m+ -> grade 0

// How much each grade counts in the blend (they add up to 1):
constexpr double ACCURACY_WEIGHT = 0.40;
constexpr double FRESHNESS_WEIGHT = 0.25;
constexpr double MOVEMENT_WEIGHT = 0.20;
constexpr double CONSISTENCY_WEIGHT = 0.15;

// The uncertainty sizes that trigger a limit:
constexpr double WIDE_UNCERTAINTY_METERS = 100.0;
constexpr double VERY_WIDE_UNCERTAINTY_METERS = 300.0;

// Computers store decimals in binary, so a sum that is exactly 91.5 on paper
// can come out as 91.49999999999999. Rounding that naively gives 91 instead
// of 92. Adding this tiny amount before rounding fixes those cases without
// affecting any realistic value.
constexpr double ROUNDING_SAFETY_MARGIN = 1e-9;

// One row per ScoreLimit, in exactly the same order as the ScoreLimit list
// in QualityScore.hpp: the public text and the highest score it allows.
struct LimitRule {
  const char* publicName;
  int highestScoreAllowed;
};

constexpr LimitRule LIMIT_RULES[] = {
    {"cached", 60},
    {"approximate_permission", 30},
    {"unknown_movement_not_fresh", 40},
    {"uncertainty_at_least_100m", 35},
    {"uncertainty_at_least_300m", 15},
    {"single_fix", 75},
    {"conflicting_evidence", 75},
    {"suspected_jump", 20},
    {"mock_location", 0},
    {"age_unreliable", 0},
    {"no_candidate", 0},
};

// The build fails here if a limit is added to the header but not to the
// table above (or the other way around).
static_assert(sizeof(LIMIT_RULES) / sizeof(LIMIT_RULES[0]) == SCORE_LIMIT_COUNT,
              "the table of limit rules does not match the ScoreLimit list");

double keepBetweenZeroAndOne(double value) noexcept {
  if (value < 0.0) return 0.0;
  if (value > 1.0) return 1.0;
  return value;
}

// --- Step 1: the four grades, each from 0 (bad) to 1 (perfect) ----------------

// 0 m of error -> 1.0; 50 m -> 0.5; 100 m or more -> 0.
double gradeForAccuracy(double horizontalAccuracyMeters) noexcept {
  return keepBetweenZeroAndOne(1.0 - horizontalAccuracyMeters / ACCURACY_IS_WORTHLESS_AT_METERS);
}

// Taken right now -> 1.0; 30 seconds ago -> 0.5; 1 minute ago or more -> 0.
double gradeForFreshness(double ageMs) noexcept {
  return keepBetweenZeroAndOne(1.0 - ageMs / READING_IS_STALE_AT_MS);
}

// Could not have moved -> 1.0; could have moved 50 m -> 0.5; 100 m or more -> 0.
double gradeForMovement(double possibleMovementMeters) noexcept {
  return keepBetweenZeroAndOne(1.0 - possibleMovementMeters / MOVEMENT_IS_TOO_MUCH_AT_METERS);
}

// More readings that agree -> higher grade. Readings that disagree -> lowest.
double gradeForConsistency(Consistency consistency) noexcept {
  switch (consistency) {
    case Consistency::OneReadingOnly:
      return 0.60;
    case Consistency::TwoAgree:
      return 0.80;
    case Consistency::ThreeOrMoreAgree:
      return 1.00;
    case Consistency::ReadingsDisagree:
      return 0.30;
  }
  return 0.30;  // unreachable; keeps every compiler happy
}

// --- Step 2: limits -------------------------------------------------------------

// Records that a limit applied and lowers the score if it is above the
// limit's ceiling. A score already below the ceiling is left alone.
void limitScore(ScoreResult& result, ScoreLimit limit) noexcept {
  result.limitWasApplied[static_cast<std::size_t>(limit)] = true;

  const int ceiling = highestScoreAllowedBy(limit);
  if (result.qualityScore > ceiling) {
    result.qualityScore = ceiling;
  }
}

// Used when there is nothing worth scoring: the score is 0 and the result
// records why.
ScoreResult scoreOfZeroBecause(ScoreLimit reason) noexcept {
  ScoreResult result;
  result.limitWasApplied[static_cast<std::size_t>(reason)] = true;
  return result;
}

bool isUsableMeasurement(double value) noexcept {
  return std::isfinite(value) && value >= 0.0;
}

// Looks at each weakness in turn and applies its limit.
void applyLimitsForWeakEvidence(const ScoreInput& input, ScoreResult& result) noexcept {
  // A reading we already had in memory is weaker evidence than one the GPS
  // just gave us. It can never score above 60.
  const bool readingCameFromHistory = (input.source == FixSource::Cached);
  if (readingCameFromHistory) {
    limitScore(result, ScoreLimit::Cached);

    // If we also have no idea whether the phone moved since that reading,
    // the old position may be far from where the courier is now: at most 40.
    if (input.movementIsUnknown) {
      limitScore(result, ScoreLimit::UnknownMovementNotFresh);
    }
  }

  // With approximate permission the system blurs the position on purpose.
  // It must never look like high-quality evidence: at most 30.
  if (input.onlyApproximatePermission) {
    limitScore(result, ScoreLimit::ApproximatePermission);
  }

  // When the phone could be 100 m (or 300 m) away from the reading, the
  // reading says little about a typical 100 m delivery radius.
  if (result.effectiveUncertaintyMeters >= WIDE_UNCERTAINTY_METERS) {
    limitScore(result, ScoreLimit::UncertaintyAtLeast100);
  }
  if (result.effectiveUncertaintyMeters >= VERY_WIDE_UNCERTAINTY_METERS) {
    limitScore(result, ScoreLimit::UncertaintyAtLeast300);
  }

  // A single reading has nothing to confirm it: at most 75.
  if (input.consistency == Consistency::OneReadingOnly) {
    limitScore(result, ScoreLimit::SingleFix);
  }

  // Readings that contradict each other: also at most 75.
  // This limit is not in the original spec. Without it there was a bug in
  // the rules: one good reading scored 75 (single-reading limit), but adding
  // a second, contradicting reading removed that limit and the score went UP
  // to 90. Bad news must never raise the score.
  if (input.consistency == Consistency::ReadingsDisagree) {
    limitScore(result, ScoreLimit::ConflictingEvidence);
  }

  // The reading looks like a GPS glitch (an impossible jump): at most 20.
  if (input.suspectedJump) {
    limitScore(result, ScoreLimit::SuspectedJump);
  }

  // The system says the location is simulated and the app asked us to give
  // such readings a score of 0.
  const bool flaggedAsSimulated = (input.mockSignal == MockSignal::FlaggedByOs);
  const bool appWantsZeroForSimulated = (input.mockPolicy == MockPolicy::ZeroScore);
  if (flaggedAsSimulated && appWantsZeroForSimulated) {
    limitScore(result, ScoreLimit::MockLocation);
  }
}

}  // namespace

Consistency consistencyFromAgreeingReadings(std::size_t differentReadingsThatAgree,
                                            bool readingsContradictEachOther) noexcept {
  if (readingsContradictEachOther) return Consistency::ReadingsDisagree;
  if (differentReadingsThatAgree >= 3) return Consistency::ThreeOrMoreAgree;
  if (differentReadingsThatAgree == 2) return Consistency::TwoAgree;
  return Consistency::OneReadingOnly;
}

const char* scoreLimitName(ScoreLimit limit) noexcept {
  const std::size_t row = static_cast<std::size_t>(limit);
  return row < SCORE_LIMIT_COUNT ? LIMIT_RULES[row].publicName : nullptr;
}

int highestScoreAllowedBy(ScoreLimit limit) noexcept {
  const std::size_t row = static_cast<std::size_t>(limit);
  return row < SCORE_LIMIT_COUNT ? LIMIT_RULES[row].highestScoreAllowed : 0;
}

int roundHalfUp(double value) noexcept {
  return static_cast<int>(std::floor(value + 0.5 + ROUNDING_SAFETY_MARGIN));
}

ScoreResult calculateQualityScore(const ScoreInput& input) noexcept {
  // --- Cases where there is nothing to score: the answer is 0 ---------------
  if (!input.hasCandidate) {
    return scoreOfZeroBecause(ScoreLimit::NoCandidate);
  }
  if (!input.ageReliable) {
    // If we cannot trust how old the reading is, it might be from an hour
    // ago. It is worth nothing.
    return scoreOfZeroBecause(ScoreLimit::AgeUnreliable);
  }

  const bool numbersAreUsable = isUsableMeasurement(input.horizontalAccuracyMeters) &&
                                isUsableMeasurement(input.ageMs) &&
                                isUsableMeasurement(input.possibleMovementMeters);
  if (!numbersAreUsable) {
    // `checkReading` should make this impossible. If broken numbers get here
    // anyway, refuse to produce a score from them.
    return scoreOfZeroBecause(ScoreLimit::NoCandidate);
  }

  ScoreResult result;
  result.hasBreakdown = true;

  // --- Step 1: the four grades and their blend -------------------------------
  result.accuracyFactor = gradeForAccuracy(input.horizontalAccuracyMeters);
  result.freshnessFactor = gradeForFreshness(input.ageMs);
  result.movementFactor = gradeForMovement(input.possibleMovementMeters);
  result.consistencyFactor = gradeForConsistency(input.consistency);

  const double blendedGrade = ACCURACY_WEIGHT * result.accuracyFactor +
                              FRESHNESS_WEIGHT * result.freshnessFactor +
                              MOVEMENT_WEIGHT * result.movementFactor +
                              CONSISTENCY_WEIGHT * result.consistencyFactor;

  int scoreBeforeLimits = roundHalfUp(100.0 * blendedGrade);
  if (scoreBeforeLimits < 0) scoreBeforeLimits = 0;
  if (scoreBeforeLimits > 100) scoreBeforeLimits = 100;

  result.rawScore = scoreBeforeLimits;
  result.qualityScore = scoreBeforeLimits;

  // How far the phone could really be from the reading: the reading's own
  // error plus however far the phone may have travelled since.
  result.effectiveUncertaintyMeters =
      input.horizontalAccuracyMeters + input.possibleMovementMeters;

  // --- Step 2: limits ----------------------------------------------------------
  applyLimitsForWeakEvidence(input, result);

  return result;
}

}  // namespace lce
