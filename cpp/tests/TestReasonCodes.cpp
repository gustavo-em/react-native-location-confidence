#include "third_party/doctest.h"

#include <cstring>
#include <set>
#include <string>

#include "QualityScore.hpp"
#include "ReasonCodes.hpp"

using namespace lce;

namespace {

bool isSnakeCase(const char* name) {
  if (name == nullptr || *name == '\0') return false;
  for (const char* c = name; *c != '\0'; ++c) {
    const bool ok = (*c >= 'a' && *c <= 'z') || (*c >= '0' && *c <= '9') || *c == '_';
    if (!ok) return false;
  }
  return true;
}

}  // namespace

TEST_CASE("reason codes: every code has a unique snake_case name") {
  std::set<std::string> seen;
  for (std::size_t i = 0; i < kReasonCodeCount; ++i) {
    const char* name = reasonCodeName(static_cast<ReasonCode>(i));
    REQUIRE(name != nullptr);
    CHECK(isSnakeCase(name));
    CHECK(seen.insert(name).second);
  }
  CHECK(seen.size() == 43);
  CHECK(reasonCodeName(ReasonCode::Count) == nullptr);
}

TEST_CASE("reason codes: catalogue names are stable") {
  CHECK(std::strcmp(reasonCodeName(ReasonCode::PermissionNotDetermined),
                    "permission_not_determined") == 0);
  CHECK(std::strcmp(reasonCodeName(ReasonCode::NoUsableFix), "no_usable_fix") == 0);
  CHECK(std::strcmp(reasonCodeName(ReasonCode::NativeInternalError), "native_internal_error") == 0);
  CHECK(std::strcmp(reasonCodeName(ReasonCode::LocationInvalidAccuracy),
                    "location_invalid_accuracy") == 0);
  CHECK(std::strcmp(reasonCodeName(ReasonCode::UncertaintyModelHeuristic),
                    "uncertainty_model_heuristic") == 0);
}

TEST_CASE("reason set: add, contains and merge") {
  ReasonSet set;
  CHECK(set.empty());
  set.add(ReasonCode::SamplesDuplicate);
  CHECK(set.contains(ReasonCode::SamplesDuplicate));
  CHECK_FALSE(set.contains(ReasonCode::Busy));

  ReasonSet other;
  other.add(ReasonCode::UncertaintyModelHeuristic);
  set.merge(other);
  CHECK(set.contains(ReasonCode::UncertaintyModelHeuristic));
  CHECK(set.contains(ReasonCode::SamplesDuplicate));
}

TEST_CASE("score caps: every cap has a unique snake_case name") {
  std::set<std::string> seen;
  for (std::size_t i = 0; i < static_cast<std::size_t>(ScoreCap::Count); ++i) {
    const char* name = scoreCapName(static_cast<ScoreCap>(i));
    REQUIRE(name != nullptr);
    CHECK(isSnakeCase(name));
    CHECK(seen.insert(name).second);
  }
  CHECK(std::strcmp(scoreCapName(ScoreCap::SingleFix), "single_fix") == 0);
  CHECK(scoreCapName(ScoreCap::Count) == nullptr);
}
