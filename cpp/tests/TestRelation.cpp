#include "third_party/doctest.h"

#include <limits>

#include "ProximityRelation.hpp"
#include "TestSupport.hpp"

using namespace lce;
using lce_test::openGates;

TEST_CASE("GEO-020 inner boundary d + u == r supports inside") {
  CHECK(classifyRelation(70.0, 30.0, 100.0, openGates()) == EvidenceRelation::SupportsInside);
}

TEST_CASE("GEO-021 outer boundary d - u == r stays inconclusive") {
  CHECK(classifyRelation(130.0, 30.0, 100.0, openGates()) == EvidenceRelation::Inconclusive);
}

TEST_CASE("GEO-022 strictly outside supports outside") {
  CHECK(classifyRelation(130.001, 30.0, 100.0, openGates()) == EvidenceRelation::SupportsOutside);
}

TEST_CASE("GEO-023 uncertainty straddling the border is inconclusive") {
  CHECK(classifyRelation(90.0, 30.0, 100.0, openGates()) == EvidenceRelation::Inconclusive);
}

TEST_CASE("SCR-021 supported inside") {
  CHECK(classifyRelation(30.0, 15.0, 100.0, openGates()) == EvidenceRelation::SupportsInside);
}

TEST_CASE("SCR-022 supported outside") {
  CHECK(classifyRelation(250.0, 20.0, 100.0, openGates()) == EvidenceRelation::SupportsOutside);
}

TEST_CASE("SCR-023 undefined proximity") {
  CHECK(classifyRelation(95.0, 20.0, 100.0, openGates()) == EvidenceRelation::Inconclusive);
}

TEST_CASE("SCR-024 a small uncertainty near the border is still inconclusive") {
  // The relation has no score input at all: a high score cannot force it.
  CHECK(classifyRelation(99.0, 5.0, 100.0, openGates()) == EvidenceRelation::Inconclusive);
}

TEST_CASE("SCR-025 geometry inside but integrity gates closed is inconclusive") {
  SUBCASE("age beyond the fallback limit") {
    RelationGates gates = openGates();
    gates.withinFallbackAge = false;
    CHECK(classifyRelation(1.0, 5.0, 100.0, gates) == EvidenceRelation::Inconclusive);
  }
  SUBCASE("age not reliable") {
    RelationGates gates = openGates();
    gates.ageReliable = false;
    CHECK(classifyRelation(1.0, 5.0, 100.0, gates) == EvidenceRelation::Inconclusive);
  }
  SUBCASE("no candidate") {
    RelationGates gates = openGates();
    gates.hasCandidate = false;
    CHECK(classifyRelation(1.0, 5.0, 100.0, gates) == EvidenceRelation::Inconclusive);
  }
  SUBCASE("permission not authorized") {
    RelationGates gates = openGates();
    gates.permissionAuthorized = false;
    CHECK(classifyRelation(1.0, 5.0, 100.0, gates) == EvidenceRelation::Inconclusive);
  }
  SUBCASE("strong jump suspicion") {
    RelationGates gates = openGates();
    gates.strongJumpSuspicion = true;
    CHECK(classifyRelation(1.0, 5.0, 100.0, gates) == EvidenceRelation::Inconclusive);
  }
  SUBCASE("blocked by mock policy") {
    RelationGates gates = openGates();
    gates.blockedByMockPolicy = true;
    CHECK(classifyRelation(1.0, 5.0, 100.0, gates) == EvidenceRelation::Inconclusive);
  }
  SUBCASE("very large uncertainty") {
    CHECK(classifyRelation(1.0, 5000.0, 100.0, openGates()) == EvidenceRelation::Inconclusive);
  }
}

TEST_CASE("SCR-026 tiny radius is not supported by equal coordinates alone") {
  CHECK(classifyRelation(0.0, 10.0, 1.0, openGates()) == EvidenceRelation::Inconclusive);
}

TEST_CASE("SCR-027 wide radius is decided by d, u and r only") {
  CHECK(classifyRelation(50000.0, 20.0, 100000.0, openGates()) == EvidenceRelation::SupportsInside);
  CHECK(classifyRelation(150000.0, 20.0, 100000.0, openGates()) ==
        EvidenceRelation::SupportsOutside);
  // An imprecise fix can still support a very wide radius.
  CHECK(classifyRelation(1000.0, 800.0, 100000.0, openGates()) == EvidenceRelation::SupportsInside);
}

TEST_CASE("fixture far_outside_without_proving_presence") {
  CHECK(classifyRelation(500.0, 20.0, 100.0, openGates()) == EvidenceRelation::SupportsOutside);
}

TEST_CASE("relation: unusable numeric inputs are inconclusive") {
  const double nan = std::numeric_limits<double>::quiet_NaN();
  const double inf = std::numeric_limits<double>::infinity();
  CHECK(classifyRelation(nan, 5.0, 100.0, openGates()) == EvidenceRelation::Inconclusive);
  CHECK(classifyRelation(10.0, inf, 100.0, openGates()) == EvidenceRelation::Inconclusive);
  CHECK(classifyRelation(10.0, -1.0, 100.0, openGates()) == EvidenceRelation::Inconclusive);
  CHECK(classifyRelation(10.0, 5.0, 0.0, openGates()) == EvidenceRelation::Inconclusive);
  CHECK(classifyRelation(-1.0, 5.0, 100.0, openGates()) == EvidenceRelation::Inconclusive);
}

TEST_CASE("relation: uncertainty larger than distance cannot support outside") {
  CHECK(classifyRelation(5.0, 20.0, 100.0, openGates()) == EvidenceRelation::SupportsInside);
  CHECK(classifyRelation(150.0, 200.0, 100.0, openGates()) == EvidenceRelation::Inconclusive);
}
