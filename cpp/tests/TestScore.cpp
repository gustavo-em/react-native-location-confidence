#include "third_party/doctest.h"

#include <cstring>

#include "QualityScore.hpp"
#include "TestSupport.hpp"

using namespace lce;
using lce_test::perfectScoreInput;

TEST_CASE("SCR-001 no candidate gives score zero and no breakdown") {
  ScoreInput input;  // hasCandidate defaults to false
  const ScoreResult result = evaluateQualityScore(input);
  CHECK(result.qualityScore == 0);
  CHECK_FALSE(result.hasBreakdown);
  CHECK(result.capApplied(ScoreCap::NoCandidate));
}

TEST_CASE("SCR-002 / fixture score_perfect_three_fixes") {
  const ScoreResult result = evaluateQualityScore(perfectScoreInput());
  REQUIRE(result.hasBreakdown);
  CHECK(result.accuracyFactor == 1.0);
  CHECK(result.freshnessFactor == 1.0);
  CHECK(result.movementFactor == 1.0);
  CHECK(result.consistencyFactor == 1.0);
  CHECK(result.rawScore == 100);
  CHECK(result.qualityScore == 100);
  CHECK(result.appliedCapsMask == 0);
}

TEST_CASE("SCR-003 / fixture score_single_fix_default") {
  ScoreInput input = perfectScoreInput();
  input.horizontalAccuracyMeters = 10.0;
  input.ageMs = 2000.0;
  input.movementAllowanceMeters = 1.0;  // stationary bound 0.5 m/s over 2 s
  input.consistency = Consistency::SingleFix;

  const ScoreResult result = evaluateQualityScore(input);
  REQUIRE(result.hasBreakdown);
  CHECK(result.accuracyFactor == doctest::Approx(0.90));
  CHECK(result.freshnessFactor == doctest::Approx(0.96666667));
  CHECK(result.movementFactor == doctest::Approx(0.99));
  CHECK(result.consistencyFactor == doctest::Approx(0.60));
  CHECK(result.rawScore == 89);
  CHECK(result.qualityScore == 75);
  CHECK(result.capApplied(ScoreCap::SingleFix));
  CHECK(std::strcmp(scoreCapName(ScoreCap::SingleFix), "single_fix") == 0);
}

TEST_CASE("fixture score_cached_vehicle") {
  ScoreInput input = perfectScoreInput();
  input.source = FixSource::Cached;
  input.horizontalAccuracyMeters = 10.0;
  input.ageMs = 10000.0;
  input.movementAllowanceMeters = 450.0;  // vehicle bound 45 m/s over 10 s
  input.movementUnknown = false;          // movement is covered by evidence

  const ScoreResult result = evaluateQualityScore(input);
  CHECK(result.effectiveUncertaintyMeters == doctest::Approx(460.0));
  CHECK(result.movementFactor == 0.0);
  CHECK(result.rawScore == 72);
  CHECK(result.qualityScore == 15);
  CHECK(result.capApplied(ScoreCap::Cached));
  CHECK(result.capApplied(ScoreCap::UncertaintyAtLeast100));
  CHECK(result.capApplied(ScoreCap::UncertaintyAtLeast300));
  CHECK_FALSE(result.capApplied(ScoreCap::UnknownMovementNotFresh));
  CHECK_FALSE(result.capApplied(ScoreCap::SingleFix));
}

TEST_CASE("SCR-004 score is an integer between 0 and 100") {
  const double accuracies[] = {0.0, 3.0, 25.0, 99.0, 100.0, 5000.0};
  const double ages[] = {0.0, 1500.0, 30000.0, 60000.0, 600000.0};
  const double allowances[] = {0.0, 10.0, 99.0, 100.0, 1000.0};
  const Consistency consistencies[] = {Consistency::SingleFix, Consistency::TwoCoherent,
                                       Consistency::ThreeOrMoreCoherent, Consistency::Conflicting};
  for (double accuracy : accuracies) {
    for (double age : ages) {
      for (double allowance : allowances) {
        for (Consistency consistency : consistencies) {
          ScoreInput input = perfectScoreInput();
          input.horizontalAccuracyMeters = accuracy;
          input.ageMs = age;
          input.movementAllowanceMeters = allowance;
          input.consistency = consistency;
          const ScoreResult result = evaluateQualityScore(input);
          CHECK(result.qualityScore >= 0);
          CHECK(result.qualityScore <= 100);
          CHECK(result.rawScore >= 0);
          CHECK(result.rawScore <= 100);
          CHECK(result.qualityScore <= result.rawScore);
        }
      }
    }
  }
}

TEST_CASE("SCR-011 approximate permission caps the score at 30") {
  ScoreInput input = perfectScoreInput();
  input.approximatePermission = true;
  const ScoreResult result = evaluateQualityScore(input);
  CHECK(result.rawScore >= 95);
  CHECK(result.qualityScore == 30);
  CHECK(result.capApplied(ScoreCap::ApproximatePermission));
}

TEST_CASE("SCR-012 cached source caps the score at 60") {
  ScoreInput input = perfectScoreInput();
  input.source = FixSource::Cached;
  input.movementUnknown = false;
  const ScoreResult result = evaluateQualityScore(input);
  CHECK(result.rawScore >= 95);
  CHECK(result.qualityScore == 60);
  CHECK(result.capApplied(ScoreCap::Cached));
}

TEST_CASE("SCR-013 cached fix with unknown movement caps the score at 40") {
  ScoreInput input = perfectScoreInput();
  input.source = FixSource::Cached;
  input.movementUnknown = true;
  const ScoreResult result = evaluateQualityScore(input);
  CHECK(result.qualityScore == 40);
  CHECK(result.capApplied(ScoreCap::Cached));
  CHECK(result.capApplied(ScoreCap::UnknownMovementNotFresh));

  SUBCASE("unknown movement alone does not cap a fresh fix") {
    ScoreInput fresh = perfectScoreInput();
    fresh.movementUnknown = true;
    CHECK_FALSE(evaluateQualityScore(fresh).capApplied(ScoreCap::UnknownMovementNotFresh));
  }
}

TEST_CASE("SCR-014 effective uncertainty of 100 m caps the score at 35") {
  ScoreInput input = perfectScoreInput();
  input.horizontalAccuracyMeters = 50.0;
  input.movementAllowanceMeters = 50.0;
  const ScoreResult result = evaluateQualityScore(input);
  CHECK(result.effectiveUncertaintyMeters == 100.0);
  CHECK(result.rawScore == 70);
  CHECK(result.qualityScore == 35);
  CHECK(result.capApplied(ScoreCap::UncertaintyAtLeast100));
  CHECK_FALSE(result.capApplied(ScoreCap::UncertaintyAtLeast300));

  SUBCASE("just below 100 m the cap does not apply") {
    input.movementAllowanceMeters = 49.9;
    CHECK_FALSE(evaluateQualityScore(input).capApplied(ScoreCap::UncertaintyAtLeast100));
  }
}

TEST_CASE("SCR-015 effective uncertainty of 300 m caps the score at 15") {
  ScoreInput input = perfectScoreInput();
  input.horizontalAccuracyMeters = 100.0;
  input.movementAllowanceMeters = 200.0;
  const ScoreResult result = evaluateQualityScore(input);
  CHECK(result.effectiveUncertaintyMeters == 300.0);
  CHECK(result.rawScore == 40);
  CHECK(result.qualityScore == 15);
  CHECK(result.capApplied(ScoreCap::UncertaintyAtLeast300));
}

TEST_CASE("SCR-016 simulated location under the zero-score policy") {
  ScoreInput input = perfectScoreInput();
  input.mockFlag = MockFlag::Reported;

  input.mockPolicy = MockPolicy::ZeroScore;
  ScoreResult result = evaluateQualityScore(input);
  CHECK(result.qualityScore == 0);
  CHECK(result.capApplied(ScoreCap::MockLocation));

  input.mockPolicy = MockPolicy::FlagOnly;
  result = evaluateQualityScore(input);
  CHECK(result.qualityScore == 100);
  CHECK_FALSE(result.capApplied(ScoreCap::MockLocation));
}

TEST_CASE("SCR-017 several caps: the lowest wins and all are recorded") {
  ScoreInput input = perfectScoreInput();
  input.source = FixSource::Cached;
  input.movementUnknown = false;
  input.approximatePermission = true;
  input.horizontalAccuracyMeters = 100.0;
  input.movementAllowanceMeters = 200.0;
  const ScoreResult result = evaluateQualityScore(input);
  CHECK(result.qualityScore == 15);
  CHECK(result.capApplied(ScoreCap::Cached));
  CHECK(result.capApplied(ScoreCap::ApproximatePermission));
  CHECK(result.capApplied(ScoreCap::UncertaintyAtLeast100));
  CHECK(result.capApplied(ScoreCap::UncertaintyAtLeast300));
}

TEST_CASE("SCR-018 no cap is applied when no limiting condition holds") {
  ScoreInput input = perfectScoreInput();
  input.horizontalAccuracyMeters = 12.0;
  input.ageMs = 3000.0;
  input.movementAllowanceMeters = 4.0;
  const ScoreResult result = evaluateQualityScore(input);
  CHECK(result.appliedCapsMask == 0);
  CHECK(result.qualityScore == result.rawScore);
}

TEST_CASE("SCR-019 unreliable age gives score zero") {
  ScoreInput input = perfectScoreInput();
  input.ageReliable = false;
  const ScoreResult result = evaluateQualityScore(input);
  CHECK(result.qualityScore == 0);
  CHECK_FALSE(result.hasBreakdown);
  CHECK(result.capApplied(ScoreCap::AgeUnreliable));
}

TEST_CASE("SCR-029 worse accuracy widens the effective uncertainty by the same amount") {
  ScoreInput input = perfectScoreInput();
  input.movementAllowanceMeters = 7.0;
  input.horizontalAccuracyMeters = 10.0;
  const double before = evaluateQualityScore(input).effectiveUncertaintyMeters;
  input.horizontalAccuracyMeters = 50.0;
  const double after = evaluateQualityScore(input).effectiveUncertaintyMeters;
  CHECK(after - before == doctest::Approx(40.0));
}

TEST_CASE("SCR-031 values ending in .5 round half up") {
  CHECK(roundHalfUp(0.0) == 0);
  CHECK(roundHalfUp(0.5) == 1);
  CHECK(roundHalfUp(88.49) == 88);
  CHECK(roundHalfUp(88.5) == 89);
  CHECK(roundHalfUp(99.5) == 100);

  // 40 * (1 - 1.25/100) = 39.5, so the weighted sum is exactly 93.5 in real
  // arithmetic but not in binary floating point.
  ScoreInput input = perfectScoreInput();
  input.consistency = Consistency::SingleFix;
  input.horizontalAccuracyMeters = 1.25;
  CHECK(evaluateQualityScore(input).rawScore == 94);

  // 40 * (1 - 3.75/100) = 38.5 -> 92.5 -> 93.
  input.horizontalAccuracyMeters = 3.75;
  CHECK(evaluateQualityScore(input).rawScore == 93);

  // Just below the half: 40 * (1 - 1.26/100) = 39.496 -> 93.496 -> 93.
  input.horizontalAccuracyMeters = 1.26;
  CHECK(evaluateQualityScore(input).rawScore == 93);

  // Exact halves that binary floating point evaluates to x.49999999999999.
  // Plain floor(x + 0.5) would round these down.
  input.horizontalAccuracyMeters = 0.0;
  input.ageMs = 6000.0;  // 40 + 22.5 + 20 + 9 = 91.5
  CHECK(evaluateQualityScore(input).rawScore == 92);

  input.ageMs = 3600.0;
  input.movementAllowanceMeters = 5.0;  // 40 + 23.5 + 19 + 9 = 91.5
  CHECK(evaluateQualityScore(input).rawScore == 92);

  input.ageMs = 8400.0;
  input.movementAllowanceMeters = 5.0;  // 40 + 21.5 + 19 + 9 = 89.5
  CHECK(evaluateQualityScore(input).rawScore == 90);
}

TEST_CASE("SCR-032 score model is versioned") {
  CHECK(std::strcmp(kScoreModelVersion, "heuristic-v1") == 0);
}

TEST_CASE("VAL-018 full-accuracy permission does not rescue a 500 m fix") {
  ScoreInput input = perfectScoreInput();
  input.approximatePermission = false;
  input.horizontalAccuracyMeters = 500.0;
  const ScoreResult result = evaluateQualityScore(input);
  CHECK(result.accuracyFactor == 0.0);
  CHECK(result.qualityScore <= 15);
  CHECK(result.capApplied(ScoreCap::UncertaintyAtLeast300));
}

TEST_CASE("VAL-019 approximate permission with 5 km accuracy is capped") {
  ScoreInput input = perfectScoreInput();
  input.approximatePermission = true;
  input.horizontalAccuracyMeters = 5000.0;
  const ScoreResult result = evaluateQualityScore(input);
  CHECK(result.qualityScore <= 15);
  CHECK(result.capApplied(ScoreCap::ApproximatePermission));
}

TEST_CASE("VAL-023 / VAL-024 mock flag policy at score level") {
  ScoreInput input = perfectScoreInput();

  input.mockFlag = MockFlag::Reported;
  CHECK(evaluateQualityScore(input).qualityScore == 0);

  // Unknown is neither treated as simulated nor as a guarantee of anything.
  input.mockFlag = MockFlag::Unknown;
  const ScoreResult unknown = evaluateQualityScore(input);
  CHECK_FALSE(unknown.capApplied(ScoreCap::MockLocation));
  CHECK(unknown.qualityScore == 100);
}

TEST_CASE("score: suspected jump caps at 20") {
  ScoreInput input = perfectScoreInput();
  input.suspectedJump = true;
  const ScoreResult result = evaluateQualityScore(input);
  CHECK(result.qualityScore == 20);
  CHECK(result.capApplied(ScoreCap::SuspectedJump));
}

TEST_CASE("score: conflicting evidence never beats a single fix") {
  // Spec defect guard: the spec table has no cap for conflicting evidence.
  // Without one, a single fix (cap 75) plus a conflicting reading would
  // score up to 90.
  ScoreInput single = perfectScoreInput();
  single.consistency = Consistency::SingleFix;
  ScoreInput conflicting = single;
  conflicting.consistency = Consistency::Conflicting;

  const ScoreResult singleResult = evaluateQualityScore(single);
  const ScoreResult conflictingResult = evaluateQualityScore(conflicting);
  CHECK(singleResult.qualityScore == 75);
  CHECK(conflictingResult.rawScore == 90);
  CHECK(conflictingResult.qualityScore <= singleResult.qualityScore);
  CHECK(conflictingResult.capApplied(ScoreCap::ConflictingEvidence));
}

TEST_CASE("score: non-finite inputs never produce a positive score") {
  ScoreInput input = perfectScoreInput();
  input.horizontalAccuracyMeters = -1.0;
  CHECK(evaluateQualityScore(input).qualityScore == 0);
}
