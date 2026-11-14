#include "third_party/doctest.h"

#include "LocationHistory.hpp"
#include "QualityScore.hpp"
#include "ReadingChecker.hpp"
#include "TestSupport.hpp"

using namespace lce;
using lce_test::readingAt;
using lce_test::validUncheckedReading;

using Result = LocationHistory::StoreResult;

TEST_CASE("HIS-001 new history is empty with capacity 10") {
  LocationHistory history;
  CHECK(history.empty());
  CHECK(history.size() == 0);
  CHECK(LocationHistory::capacity() == 10);
  CHECK(history.newest() == nullptr);
}

TEST_CASE("HIS-002 first fix") {
  LocationHistory history;
  CHECK(history.store(readingAt(1000), 1000) == Result::Stored);
  CHECK(history.size() == 1);
  CHECK(history.at(0).arrivalNumber != 0);
}

TEST_CASE("HIS-003 ten fixes with unique timestamps") {
  LocationHistory history;
  for (int i = 1; i <= 10; ++i) {
    CHECK(history.store(readingAt(i * 1000), i * 1000) == Result::Stored);
  }
  CHECK(history.size() == 10);
}

TEST_CASE("HIS-004 eleventh fix evicts the oldest") {
  LocationHistory history;
  for (int i = 1; i <= 11; ++i) {
    history.store(readingAt(i * 1000), i * 1000);
  }
  CHECK(history.size() == 10);
  CHECK(history.at(0).capturedAtMs == 2000);
  CHECK(history.newest()->capturedAtMs == 11000);
  CHECK(history.stats().removedToMakeRoom == 1);
}

TEST_CASE("HIS-005 fixes older than 120 s are pruned") {
  LocationHistory history;
  history.store(readingAt(0), 0);
  history.store(readingAt(60000), 60000);

  history.forgetOldReadings(120000);  // exactly at the limit: kept
  CHECK(history.size() == 2);

  history.forgetOldReadings(120001);
  REQUIRE(history.size() == 1);
  CHECK(history.at(0).capturedAtMs == 60000);
  CHECK(history.stats().removedForAge == 1);

  SUBCASE("insertion prunes too") {
    history.store(readingAt(200000), 200000);
    REQUIRE(history.size() == 1);
    CHECK(history.at(0).capturedAtMs == 200000);
  }
  SUBCASE("a fix that is already expired on arrival is not stored") {
    CHECK(history.store(readingAt(1000), 130000) == Result::IgnoredTooOld);
    CHECK(history.size() == 1);
  }
}

TEST_CASE("HIS-006 identical duplicated callback is stored once") {
  LocationHistory history;
  const LocationReading sample = readingAt(1000, -20.8117, -49.3768, 8.0);
  CHECK(history.store(sample, 1000) == Result::Stored);
  CHECK(history.store(sample, 1001) == Result::IgnoredDuplicate);
  CHECK(history.size() == 1);
  CHECK(history.stats().duplicates == 1);
  CHECK(history.stats().received == 2);
}

TEST_CASE("HIS-007 same place at different timestamps stays distinct") {
  LocationHistory history;
  history.store(readingAt(1000, -20.8117, -49.3768), 1000);
  history.store(readingAt(2000, -20.8117, -49.3768), 2000);
  CHECK(history.size() == 2);
  CHECK(history.stats().duplicates == 0);
}

TEST_CASE("HIS-008 an invalid fix is counted by validation and never stored") {
  LocationHistory history;
  UncheckedReading raw = validUncheckedReading(1000);
  raw.latitudeDegrees = 91.0;
  const ReadingCheck check = checkReading(raw, 1000);
  REQUIRE_FALSE(check.accepted);
  // The engine only inserts accepted samples.
  CHECK(history.empty());
}

TEST_CASE("HIS-009 an imprecise but valid fix is kept for diagnostics") {
  LocationHistory history;
  UncheckedReading raw = validUncheckedReading(1000);
  raw.horizontalAccuracyMeters = 500.0;
  const ReadingCheck check = checkReading(raw, 1000);
  REQUIRE(check.accepted);
  CHECK(history.store(check.reading, 1000) == Result::Stored);
  CHECK(history.size() == 1);
  CHECK(history.at(0).horizontalAccuracyMeters == 500.0);
}

TEST_CASE("HIS-010 two histories do not share fixes") {
  LocationHistory a;
  LocationHistory b;
  a.store(readingAt(1000, 10.0, 10.0), 1000);
  CHECK(a.size() == 1);
  CHECK(b.empty());
}

TEST_CASE("HIS-012 clearing twice leaves nothing behind") {
  LocationHistory history;
  history.store(readingAt(1000, 12.5, -45.25), 1000);
  history.store(readingAt(2000, 12.5, -45.25), 2000);
  history.clear();
  CHECK(history.empty());
  CHECK(history.newest() == nullptr);
  history.clear();
  CHECK(history.empty());
}

TEST_CASE("HIS-014 a history is never refilled from a previous one") {
  LocationHistory first;
  first.store(readingAt(1000), 1000);
  const std::uint64_t firstSequence = first.at(0).arrivalNumber;

  LocationHistory second;
  CHECK(second.empty());

  // Within one history, sequences are not reused after a clear.
  first.clear();
  first.store(readingAt(2000), 2000);
  CHECK(first.at(0).arrivalNumber > firstSequence);
}

TEST_CASE("HIS-017 history is ordered by capture time, not arrival order") {
  LocationHistory history;
  CHECK(history.store(readingAt(10000), 10000) == Result::Stored);
  CHECK(history.store(readingAt(8000), 10001) == Result::StoredOutOfOrder);
  CHECK(history.store(readingAt(9000), 10002) == Result::StoredOutOfOrder);
  REQUIRE(history.size() == 3);
  CHECK(history.at(0).capturedAtMs == 8000);
  CHECK(history.at(1).capturedAtMs == 9000);
  CHECK(history.at(2).capturedAtMs == 10000);
}

TEST_CASE("VAL-014 an old callback after a newer fix does not become the newest") {
  LocationHistory history;
  history.store(readingAt(10000, 1.0, 1.0), 10000);
  history.store(readingAt(4000, 2.0, 2.0), 10500);
  REQUIRE(history.newest() != nullptr);
  CHECK(history.newest()->capturedAtMs == 10000);
  CHECK(history.newest()->position.latitudeDegrees == 1.0);
}

TEST_CASE("VAL-013 equal timestamps with different positions are both kept in arrival order") {
  LocationHistory history;
  history.store(readingAt(5000, 1.0, 1.0), 5000);
  history.store(readingAt(5000, 1.001, 1.0), 5000);
  REQUIRE(history.size() == 2);
  CHECK(history.at(0).position.latitudeDegrees == 1.0);
  CHECK(history.at(1).position.latitudeDegrees == 1.001);
  CHECK(history.at(0).arrivalNumber < history.at(1).arrivalNumber);
}

TEST_CASE("HIS-021 one fix gives single-fix consistency") {
  LocationHistory history;
  history.store(readingAt(1000), 1000);
  CHECK(consistencyFromAgreeingReadings(history.size(), false) == Consistency::OneReadingOnly);
}

TEST_CASE("HIS-022 two distinct fixes give two-coherent consistency") {
  LocationHistory history;
  history.store(readingAt(1000), 1000);
  history.store(readingAt(2000), 2000);
  CHECK(consistencyFromAgreeingReadings(history.size(), false) == Consistency::TwoAgree);
}

TEST_CASE("HIS-023 three distinct fixes give full consistency") {
  LocationHistory history;
  history.store(readingAt(1000), 1000);
  history.store(readingAt(2000), 2000);
  history.store(readingAt(3000), 3000);
  CHECK(consistencyFromAgreeingReadings(history.size(), false) ==
        Consistency::ThreeOrMoreAgree);
  CHECK(consistencyFromAgreeingReadings(history.size(), true) == Consistency::ReadingsDisagree);
}

TEST_CASE("HIS-024 the same fix delivered three times is still a single fix") {
  LocationHistory history;
  const LocationReading sample = readingAt(1000, 5.0, 5.0, 6.0);
  history.store(sample, 1000);
  history.store(sample, 1001);
  history.store(sample, 1002);
  CHECK(history.size() == 1);
  CHECK(consistencyFromAgreeingReadings(history.size(), false) == Consistency::OneReadingOnly);
}

TEST_CASE("HIS-025 more than ten thousand callbacks keep memory bounded") {
  LocationHistory history;
  const int total = 10500;
  for (int i = 0; i < total; ++i) {
    const std::int64_t time = static_cast<std::int64_t>(i) * 250;
    history.store(readingAt(time, 0.0001 * (i % 7), 0.0), time);
    REQUIRE(history.size() <= LocationHistory::capacity());
  }
  CHECK(history.size() == 10);
  CHECK(history.stats().received == static_cast<std::uint64_t>(total));
  CHECK(history.newest()->capturedAtMs == static_cast<std::int64_t>(total - 1) * 250);
}

TEST_CASE("HIS-027 eviction protects the current best candidate") {
  LocationHistory history;
  for (int i = 1; i <= 10; ++i) {
    history.store(readingAt(i * 1000), i * 1000);
  }
  const std::uint64_t best = history.at(0).arrivalNumber;  // the oldest fix is the best one

  SUBCASE("the oldest unprotected fix is evicted instead") {
    CHECK(history.store(readingAt(11000), 11000, best) == Result::Stored);
    REQUIRE(history.size() == 10);
    CHECK(history.at(0).arrivalNumber == best);
    CHECK(history.at(0).capturedAtMs == 1000);
    CHECK(history.at(1).capturedAtMs == 3000);
  }
  SUBCASE("without protection the oldest goes") {
    history.store(readingAt(11000), 11000);
    CHECK(history.at(0).capturedAtMs == 2000);
  }
  SUBCASE("a new fix older than everything is dropped when the buffer is full") {
    CHECK(history.store(readingAt(500), 11000) == Result::IgnoredOlderThanAllStored);
    CHECK(history.size() == 10);
    CHECK(history.at(0).capturedAtMs == 1000);
  }
  SUBCASE("a new fix right after the protected one is itself the victim") {
    CHECK(history.store(readingAt(1500), 11000, best) == Result::IgnoredOlderThanAllStored);
    CHECK(history.size() == 10);
    CHECK(history.at(1).capturedAtMs == 2000);
  }
}
