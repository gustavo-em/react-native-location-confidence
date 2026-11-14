// The quality score: one number from 0 to 100 that says how good the
// location evidence is.
//
// VERY IMPORTANT: this is an index, like a grade. It is NOT a probability.
// A score of 78 does not mean "78% sure the courier is there". The weights
// and limits below are design guesses that were never calibrated against
// real-world data. The score must never be the only thing an app looks at.
//
// How the score is built, in two steps:
//
//   Step 1 - Blend four grades, each from 0 (bad) to 1 (perfect):
//       accuracy     how precise the phone says the reading is      weight 40%
//       freshness    how recently the reading was taken             weight 25%
//       movement     how little the phone could have moved since    weight 20%
//       consistency  how many readings agree with each other        weight 15%
//
//   Step 2 - Apply limits ("caps"). Some weaknesses are serious enough that
//   the score cannot go above a ceiling, no matter how good the rest is.
//   Example: a reading taken from the history can never score above 60.
//   When several limits apply, the lowest ceiling wins.
#ifndef LCE_QUALITY_SCORE_HPP
#define LCE_QUALITY_SCORE_HPP

#include <array>
#include <cstddef>
#include <cstdint>

#include "Types.hpp"

namespace lce {

// How much the readings we have agree with each other.
enum class Consistency : std::uint8_t {
  OneReadingOnly,    // a single reading: nothing to compare it with
  TwoAgree,          // two different readings that tell the same story
  ThreeOrMoreAgree,  // three or more different readings that tell the same story
  ReadingsDisagree,  // the readings contradict each other
};

// Turns "how many different readings agree" into a Consistency value.
// The same reading delivered twice counts once: repeats must already have
// been removed (the history does that).
Consistency consistencyFromAgreeingReadings(std::size_t differentReadingsThatAgree,
                                            bool readingsContradictEachOther) noexcept;

// Every ceiling that can be put on the score, and why.
enum class ScoreLimit : std::uint8_t {
  Cached = 0,               // the reading came from the history, not from the GPS just now
  ApproximatePermission,    // the user only allowed approximate location
  UnknownMovementNotFresh,  // old reading AND we cannot tell whether the phone moved
  UncertaintyAtLeast100,    // the phone could be 100 m or more from the reading
  UncertaintyAtLeast300,    // the phone could be 300 m or more from the reading
  SingleFix,                // only one reading: nothing confirms it
  ConflictingEvidence,      // the readings contradict each other
  SuspectedJump,            // the reading looks like a GPS glitch
  MockLocation,             // flagged as simulated and the app asked for score 0
  AgeUnreliable,            // we do not trust how old the reading is
  NoCandidate,              // there is no reading to score
  Count                     // not a limit: only tells how many limits exist
};

inline constexpr std::size_t SCORE_LIMIT_COUNT = static_cast<std::size_t>(ScoreLimit::Count);

// Public text of a limit, shown to apps in `scoreBreakdown.appliedCaps`,
// for example "single_fix".
const char* scoreLimitName(ScoreLimit limit) noexcept;

// The highest score this limit allows, for example 60 for Cached.
int highestScoreAllowedBy(ScoreLimit limit) noexcept;

// Everything the score needs to know about the reading being judged.
struct ScoreInput {
  bool hasCandidate = false;  // is there a reading at all?
  bool ageReliable = false;   // do we trust how old it is?

  double horizontalAccuracyMeters = 0.0;  // the phone's own error estimate
  double ageMs = 0.0;                     // how old the reading is

  // How far the phone could have travelled since the reading was taken.
  // Example: 10 seconds ago, at walking speed (3 m/s) -> 30 meters.
  double possibleMovementMeters = 0.0;

  // True when nothing told us how the phone was moving, so the number above
  // is only a worst-case guess.
  bool movementIsUnknown = true;

  Consistency consistency = Consistency::OneReadingOnly;
  FixSource source = FixSource::Unavailable;
  bool onlyApproximatePermission = false;
  bool suspectedJump = false;
  MockSignal mockSignal = MockSignal::Unknown;
  MockPolicy mockPolicy = MockPolicy::ZeroScore;
};

struct ScoreResult {
  // False when there was nothing to score. The four grades are then unused.
  bool hasBreakdown = false;

  // The four grades from step 1, each from 0 to 1.
  double accuracyFactor = 0.0;
  double freshnessFactor = 0.0;
  double movementFactor = 0.0;
  double consistencyFactor = 0.0;

  // How far the phone could really be from the reading:
  // accuracy error + possible movement since then.
  double effectiveUncertaintyMeters = 0.0;

  int rawScore = 0;      // the blend from step 1, before any limit
  int qualityScore = 0;  // the final score, after limits

  // One yes/no per ScoreLimit: was this limit applied?
  // Every limit that applied is recorded, not only the lowest one.
  std::array<bool, SCORE_LIMIT_COUNT> limitWasApplied{};

  bool wasLimitedBy(ScoreLimit limit) const noexcept {
    return limitWasApplied[static_cast<std::size_t>(limit)];
  }

  bool anyLimitApplied() const noexcept {
    for (const bool applied : limitWasApplied) {
      if (applied) return true;
    }
    return false;
  }
};

// Rounds to the nearest whole number; exactly .5 goes up (88.5 -> 89).
// Only for values that are zero or positive.
int roundHalfUp(double value) noexcept;

// Calculates the quality score for one reading.
ScoreResult calculateQualityScore(const ScoreInput& input) noexcept;

}  // namespace lce

#endif  // LCE_QUALITY_SCORE_HPP
