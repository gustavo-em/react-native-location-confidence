// Property and metamorphic tests with a fixed seed.
#include "third_party/doctest.h"

#include <algorithm>
#include <cmath>

#include "GeoMath.hpp"
#include "QualityScore.hpp"
#include "TestSupport.hpp"

using namespace lce;
using lce_test::DeterministicRng;

namespace {

constexpr int kIterations = 5000;

ScoreInput randomScoreInput(DeterministicRng& rng) {
  ScoreInput input;
  input.hasCandidate = true;
  input.ageReliable = true;
  // Half of the inputs are high-quality candidates with no limiting flags, so
  // the uncapped region (where an ordering bug would show) is well covered.
  const bool highQuality = rng.below(2) == 0;
  if (highQuality) {
    input.horizontalAccuracyMeters = rng.uniform(0.0, 20.0);
    input.ageMs = rng.uniform(0.0, 5000.0);
    input.movementAllowanceMeters = rng.uniform(0.0, 10.0);
    input.movementUnknown = false;
    input.consistency = static_cast<Consistency>(rng.below(4));
    input.source = static_cast<FixSource>(rng.below(2));  // Fresh, Recovered
    input.approximatePermission = false;
    input.suspectedJump = false;
    input.mockFlag = MockFlag::NotReported;
    input.mockPolicy = MockPolicy::ZeroScore;
    return input;
  }
  input.horizontalAccuracyMeters = rng.uniform(0.0, 400.0);
  input.ageMs = rng.uniform(0.0, 90000.0);
  input.movementAllowanceMeters = rng.uniform(0.0, 500.0);
  input.movementUnknown = rng.below(2) == 0;
  input.consistency = static_cast<Consistency>(rng.below(4));
  input.source = static_cast<FixSource>(rng.below(3));  // Fresh, Recovered, Cached
  input.approximatePermission = rng.below(4) == 0;
  input.suspectedJump = rng.below(5) == 0;
  input.mockFlag = static_cast<MockFlag>(rng.below(3));
  input.mockPolicy = static_cast<MockPolicy>(rng.below(2));
  return input;
}

}  // namespace

TEST_CASE("GEO-024 distance is symmetric for 1000 random pairs") {
  DeterministicRng rng(0x47454F3234ULL);
  for (int i = 0; i < 1000; ++i) {
    const Coordinates a{rng.uniform(-90.0, 90.0), rng.uniform(-180.0, 180.0)};
    const Coordinates b{rng.uniform(-90.0, 90.0), rng.uniform(-180.0, 180.0)};
    const double ab = haversineMeters(a, b);
    const double ba = haversineMeters(b, a);
    REQUIRE(std::isfinite(ab));
    REQUIRE(ab >= 0.0);
    REQUIRE(std::abs(ab - ba) <= 1e-7 * std::max(ab, 1.0));
  }
}

TEST_CASE("GEO-025 distance changes continuously as the destination moves") {
  const Coordinates fix{-20.8117, -49.3768};
  const double stepDeg = 1e-5;  // about 1.11 m along the meridian
  const double expectedStep = stepDeg * kEarthMeanRadiusMeters * 3.14159265358979323846 / 180.0;

  double previous = haversineMeters(fix, fix);
  CHECK(previous == 0.0);
  for (int i = 1; i <= 1000; ++i) {
    const Coordinates destination{fix.latitudeDeg + i * stepDeg, fix.longitudeDeg};
    const double current = haversineMeters(fix, destination);
    REQUIRE(current > previous);
    REQUIRE(std::abs((current - previous) - expectedStep) < 1e-3);
    previous = current;
  }
}

TEST_CASE("SCR-006 score never increases when only the age grows") {
  DeterministicRng rng(0x5343523036ULL);
  for (int i = 0; i < kIterations; ++i) {
    const ScoreInput base = randomScoreInput(rng);
    ScoreInput older = base;
    older.ageMs = base.ageMs + rng.uniform(0.0, 60000.0);
    REQUIRE(evaluateQualityScore(older).qualityScore <= evaluateQualityScore(base).qualityScore);
  }
}

TEST_CASE("SCR-007 score never increases when only the accuracy worsens") {
  DeterministicRng rng(0x5343523037ULL);
  for (int i = 0; i < kIterations; ++i) {
    const ScoreInput base = randomScoreInput(rng);
    ScoreInput worse = base;
    worse.horizontalAccuracyMeters = base.horizontalAccuracyMeters + rng.uniform(0.0, 300.0);
    REQUIRE(evaluateQualityScore(worse).qualityScore <= evaluateQualityScore(base).qualityScore);
  }
}

TEST_CASE("SCR-008 losing movement evidence never increases the score (score level)") {
  DeterministicRng rng(0x5343523038ULL);
  for (int i = 0; i < kIterations; ++i) {
    ScoreInput covered = randomScoreInput(rng);
    covered.movementUnknown = false;
    ScoreInput unknown = covered;
    unknown.movementUnknown = true;
    // Without evidence the bound can only be wider.
    unknown.movementAllowanceMeters = covered.movementAllowanceMeters + rng.uniform(0.0, 400.0);
    REQUIRE(evaluateQualityScore(unknown).qualityScore <=
            evaluateQualityScore(covered).qualityScore);
  }
}

TEST_CASE("SCR-010 conflicting evidence or a jump flag never increases the score (score level)") {
  DeterministicRng rng(0x5343523130ULL);
  for (int i = 0; i < kIterations; ++i) {
    ScoreInput base = randomScoreInput(rng);
    base.suspectedJump = false;
    if (base.consistency == Consistency::Conflicting) {
      base.consistency = Consistency::SingleFix;
    }
    const int baseScore = evaluateQualityScore(base).qualityScore;

    ScoreInput conflicting = base;
    conflicting.consistency = Consistency::Conflicting;
    REQUIRE(evaluateQualityScore(conflicting).qualityScore <= baseScore);

    ScoreInput jump = base;
    jump.suspectedJump = true;
    REQUIRE(evaluateQualityScore(jump).qualityScore <= baseScore);
  }
}

TEST_CASE("score property: the result never exceeds any applied cap") {
  DeterministicRng rng(0x4341505321ULL);
  for (int i = 0; i < kIterations; ++i) {
    const ScoreResult result = evaluateQualityScore(randomScoreInput(rng));
    REQUIRE(result.qualityScore >= 0);
    REQUIRE(result.qualityScore <= 100);
    for (std::size_t cap = 0; cap < static_cast<std::size_t>(ScoreCap::Count); ++cap) {
      if (result.capApplied(static_cast<ScoreCap>(cap))) {
        REQUIRE(result.qualityScore <= scoreCapLimit(static_cast<ScoreCap>(cap)));
      }
    }
  }
}

TEST_CASE("score property: same input gives the same output") {
  DeterministicRng rng(0x5245503239ULL);
  for (int i = 0; i < 500; ++i) {
    const ScoreInput input = randomScoreInput(rng);
    const ScoreResult first = evaluateQualityScore(input);
    const ScoreResult second = evaluateQualityScore(input);
    REQUIRE(first.qualityScore == second.qualityScore);
    REQUIRE(first.rawScore == second.rawScore);
    REQUIRE(first.appliedCapsMask == second.appliedCapsMask);
  }
}
