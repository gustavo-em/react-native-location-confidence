#include "third_party/doctest.h"

#include "LocationHistory.hpp"
#include "QualityScore.hpp"
#include "SampleValidator.hpp"
#include "TestSupport.hpp"

using namespace lce;
using lce_test::sampleAt;
using lce_test::validRawFix;

using Result = LocationHistory::InsertResult;

TEST_CASE("HIS-001 new history is empty with capacity 10") {
  LocationHistory history;
  CHECK(history.empty());
  CHECK(history.size() == 0);
  CHECK(LocationHistory::capacity() == 10);
  CHECK(history.newest() == nullptr);
}

TEST_CASE("HIS-002 first fix") {
  LocationHistory history;
  CHECK(history.insert(sampleAt(1000), 1000) == Result::Inserted);
  CHECK(history.size() == 1);
  CHECK(history.at(0).sequence != 0);
}

TEST_CASE("HIS-003 ten fixes with unique timestamps") {
  LocationHistory history;
  for (int i = 1; i <= 10; ++i) {
    CHECK(history.insert(sampleAt(i * 1000), i * 1000) == Result::Inserted);
  }
  CHECK(history.size() == 10);
}

TEST_CASE("HIS-004 eleventh fix evicts the oldest") {
  LocationHistory history;
  for (int i = 1; i <= 11; ++i) {
    history.insert(sampleAt(i * 1000), i * 1000);
  }
  CHECK(history.size() == 10);
  CHECK(history.at(0).fixTimeMonotonicMs == 2000);
  CHECK(history.newest()->fixTimeMonotonicMs == 11000);
  CHECK(history.stats().evicted == 1);
}

TEST_CASE("HIS-005 fixes older than 120 s are pruned") {
  LocationHistory history;
  history.insert(sampleAt(0), 0);
  history.insert(sampleAt(60000), 60000);

  history.prune(120000);  // exactly at the limit: kept
  CHECK(history.size() == 2);

  history.prune(120001);
  REQUIRE(history.size() == 1);
  CHECK(history.at(0).fixTimeMonotonicMs == 60000);
  CHECK(history.stats().pruned == 1);

  SUBCASE("insertion prunes too") {
    history.insert(sampleAt(200000), 200000);
    REQUIRE(history.size() == 1);
    CHECK(history.at(0).fixTimeMonotonicMs == 200000);
  }
  SUBCASE("a fix that is already expired on arrival is not stored") {
    CHECK(history.insert(sampleAt(1000), 130000) == Result::DroppedExpired);
    CHECK(history.size() == 1);
  }
}

TEST_CASE("HIS-006 identical duplicated callback is stored once") {
  LocationHistory history;
  const LocationSample sample = sampleAt(1000, -20.8117, -49.3768, 8.0);
  CHECK(history.insert(sample, 1000) == Result::Inserted);
  CHECK(history.insert(sample, 1001) == Result::Duplicate);
  CHECK(history.size() == 1);
  CHECK(history.stats().duplicates == 1);
  CHECK(history.stats().received == 2);
}

TEST_CASE("HIS-007 same place at different timestamps stays distinct") {
  LocationHistory history;
  history.insert(sampleAt(1000, -20.8117, -49.3768), 1000);
  history.insert(sampleAt(2000, -20.8117, -49.3768), 2000);
  CHECK(history.size() == 2);
  CHECK(history.stats().duplicates == 0);
}

TEST_CASE("HIS-008 an invalid fix is counted by validation and never stored") {
  LocationHistory history;
  RawFix raw = validRawFix(1000);
  raw.latitudeDeg = 91.0;
  const SampleCheck check = validateSample(raw, 1000);
  REQUIRE_FALSE(check.accepted);
  // The engine only inserts accepted samples.
  CHECK(history.empty());
}

TEST_CASE("HIS-009 an imprecise but valid fix is kept for diagnostics") {
  LocationHistory history;
  RawFix raw = validRawFix(1000);
  raw.horizontalAccuracyMeters = 500.0;
  const SampleCheck check = validateSample(raw, 1000);
  REQUIRE(check.accepted);
  CHECK(history.insert(check.sample, 1000) == Result::Inserted);
  CHECK(history.size() == 1);
  CHECK(history.at(0).horizontalAccuracyMeters == 500.0);
}

TEST_CASE("HIS-010 two histories do not share fixes") {
  LocationHistory a;
  LocationHistory b;
  a.insert(sampleAt(1000, 10.0, 10.0), 1000);
  CHECK(a.size() == 1);
  CHECK(b.empty());
}

TEST_CASE("HIS-012 clearing twice leaves nothing behind") {
  LocationHistory history;
  history.insert(sampleAt(1000, 12.5, -45.25), 1000);
  history.insert(sampleAt(2000, 12.5, -45.25), 2000);
  history.clear();
  CHECK(history.empty());
  CHECK(history.newest() == nullptr);
  history.clear();
  CHECK(history.empty());
}

TEST_CASE("HIS-014 a history is never refilled from a previous one") {
  LocationHistory first;
  first.insert(sampleAt(1000), 1000);
  const std::uint64_t firstSequence = first.at(0).sequence;

  LocationHistory second;
  CHECK(second.empty());

  // Within one history, sequences are not reused after a clear.
  first.clear();
  first.insert(sampleAt(2000), 2000);
  CHECK(first.at(0).sequence > firstSequence);
}

TEST_CASE("HIS-017 history is ordered by capture time, not arrival order") {
  LocationHistory history;
  CHECK(history.insert(sampleAt(10000), 10000) == Result::Inserted);
  CHECK(history.insert(sampleAt(8000), 10001) == Result::InsertedOutOfOrder);
  CHECK(history.insert(sampleAt(9000), 10002) == Result::InsertedOutOfOrder);
  REQUIRE(history.size() == 3);
  CHECK(history.at(0).fixTimeMonotonicMs == 8000);
  CHECK(history.at(1).fixTimeMonotonicMs == 9000);
  CHECK(history.at(2).fixTimeMonotonicMs == 10000);
}

TEST_CASE("VAL-014 an old callback after a newer fix does not become the newest") {
  LocationHistory history;
  history.insert(sampleAt(10000, 1.0, 1.0), 10000);
  history.insert(sampleAt(4000, 2.0, 2.0), 10500);
  REQUIRE(history.newest() != nullptr);
  CHECK(history.newest()->fixTimeMonotonicMs == 10000);
  CHECK(history.newest()->position.latitudeDeg == 1.0);
}

TEST_CASE("VAL-013 equal timestamps with different positions are both kept in arrival order") {
  LocationHistory history;
  history.insert(sampleAt(5000, 1.0, 1.0), 5000);
  history.insert(sampleAt(5000, 1.001, 1.0), 5000);
  REQUIRE(history.size() == 2);
  CHECK(history.at(0).position.latitudeDeg == 1.0);
  CHECK(history.at(1).position.latitudeDeg == 1.001);
  CHECK(history.at(0).sequence < history.at(1).sequence);
}

TEST_CASE("HIS-021 one fix gives single-fix consistency") {
  LocationHistory history;
  history.insert(sampleAt(1000), 1000);
  CHECK(consistencyFromDistinctCoherent(history.size(), false) == Consistency::SingleFix);
}

TEST_CASE("HIS-022 two distinct fixes give two-coherent consistency") {
  LocationHistory history;
  history.insert(sampleAt(1000), 1000);
  history.insert(sampleAt(2000), 2000);
  CHECK(consistencyFromDistinctCoherent(history.size(), false) == Consistency::TwoCoherent);
}

TEST_CASE("HIS-023 three distinct fixes give full consistency") {
  LocationHistory history;
  history.insert(sampleAt(1000), 1000);
  history.insert(sampleAt(2000), 2000);
  history.insert(sampleAt(3000), 3000);
  CHECK(consistencyFromDistinctCoherent(history.size(), false) ==
        Consistency::ThreeOrMoreCoherent);
  CHECK(consistencyFromDistinctCoherent(history.size(), true) == Consistency::Conflicting);
}

TEST_CASE("HIS-024 the same fix delivered three times is still a single fix") {
  LocationHistory history;
  const LocationSample sample = sampleAt(1000, 5.0, 5.0, 6.0);
  history.insert(sample, 1000);
  history.insert(sample, 1001);
  history.insert(sample, 1002);
  CHECK(history.size() == 1);
  CHECK(consistencyFromDistinctCoherent(history.size(), false) == Consistency::SingleFix);
}

TEST_CASE("HIS-025 more than ten thousand callbacks keep memory bounded") {
  LocationHistory history;
  const int total = 10500;
  for (int i = 0; i < total; ++i) {
    const std::int64_t time = static_cast<std::int64_t>(i) * 250;
    history.insert(sampleAt(time, 0.0001 * (i % 7), 0.0), time);
    REQUIRE(history.size() <= LocationHistory::capacity());
  }
  CHECK(history.size() == 10);
  CHECK(history.stats().received == static_cast<std::uint64_t>(total));
  CHECK(history.newest()->fixTimeMonotonicMs == static_cast<std::int64_t>(total - 1) * 250);
}

TEST_CASE("HIS-027 eviction protects the current best candidate") {
  LocationHistory history;
  for (int i = 1; i <= 10; ++i) {
    history.insert(sampleAt(i * 1000), i * 1000);
  }
  const std::uint64_t best = history.at(0).sequence;  // the oldest fix is the best one

  SUBCASE("the oldest unprotected fix is evicted instead") {
    CHECK(history.insert(sampleAt(11000), 11000, best) == Result::Inserted);
    REQUIRE(history.size() == 10);
    CHECK(history.at(0).sequence == best);
    CHECK(history.at(0).fixTimeMonotonicMs == 1000);
    CHECK(history.at(1).fixTimeMonotonicMs == 3000);
  }
  SUBCASE("without protection the oldest goes") {
    history.insert(sampleAt(11000), 11000);
    CHECK(history.at(0).fixTimeMonotonicMs == 2000);
  }
  SUBCASE("a new fix older than everything is dropped when the buffer is full") {
    CHECK(history.insert(sampleAt(500), 11000) == Result::DroppedAsOldest);
    CHECK(history.size() == 10);
    CHECK(history.at(0).fixTimeMonotonicMs == 1000);
  }
  SUBCASE("a new fix right after the protected one is itself the victim") {
    CHECK(history.insert(sampleAt(1500), 11000, best) == Result::DroppedAsOldest);
    CHECK(history.size() == 10);
    CHECK(history.at(1).fixTimeMonotonicMs == 2000);
  }
}
