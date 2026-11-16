// Tests for the phone activity timeline: how long the phone spent in each
// activity since a GPS reading was taken.
#include "third_party/doctest.h"

#include <array>
#include <cstdint>
#include <initializer_list>
#include <limits>

#include "PhoneActivityTimeline.hpp"
#include "TestSupport.hpp"
#include "UncertaintyModel.hpp"

using namespace lce;
using lce_test::DeterministicRng;
using lce_test::readingAt;

using Confidence = PhoneActivityConfidence;

namespace {

// One report from the phone. Times in seconds. High unless said otherwise.
PhoneActivityReport report(PhoneActivity activity,
                           double fromSeconds,
                           double toSeconds,
                           Confidence confidence = Confidence::High) {
  PhoneActivityReport result;
  result.startedAtMs = static_cast<std::int64_t>(fromSeconds * 1000.0);
  result.endedAtMs = static_cast<std::int64_t>(toSeconds * 1000.0);
  result.activity = activity;
  result.confidence = confidence;
  return result;
}

PhoneActivityHistory historyWith(std::initializer_list<PhoneActivityReport> reports) {
  PhoneActivityHistory history;
  for (const PhoneActivityReport& oneReport : reports) {
    history.store(oneReport);
  }
  return history;
}

// The table for a reading taken at second 0 and judged at `secondsNow`.
TimeInEachPhoneActivity timeSinceSecondZero(const PhoneActivityHistory& history,
                                            double secondsNow) {
  return calculateTimeInEachPhoneActivity(history, 0,
                                          static_cast<std::int64_t>(secondsNow * 1000.0));
}

// What the movement model makes of a table, for a reading with no GPS speed.
double metersFor(const TimeInEachPhoneActivity& timeInEachActivity) {
  return calculateDistancePhoneCouldHaveMoved(readingAt(0), timeInEachActivity).meters;
}

bool sameTable(const TimeInEachPhoneActivity& one, const TimeInEachPhoneActivity& other) {
  return one.millisecondsStandingStill == other.millisecondsStandingStill &&
         one.millisecondsWalking == other.millisecondsWalking &&
         one.millisecondsRunning == other.millisecondsRunning &&
         one.millisecondsCycling == other.millisecondsCycling &&
         one.millisecondsInVehicle == other.millisecondsInVehicle &&
         one.millisecondsUnknown == other.millisecondsUnknown &&
         one.twoReportsDisagreed == other.twoReportsDisagreed;
}

// A known activity chosen at random (never Unknown).
PhoneActivity randomKnownActivity(DeterministicRng& rng) {
  return static_cast<PhoneActivity>(rng.below(5));
}

// Up to 6 reports, High, that never overlap each other, inside 0 to 30 seconds.
struct ReportList {
  std::array<PhoneActivityReport, 6> reports{};
  std::size_t howMany = 0;
};

ReportList randomReportsThatDoNotOverlap(DeterministicRng& rng) {
  ReportList list;
  std::int64_t cursorMs = static_cast<std::int64_t>(rng.below(4000));
  while (list.howMany < list.reports.size() && cursorMs < 30000) {
    PhoneActivityReport oneReport;
    oneReport.startedAtMs = cursorMs;
    oneReport.endedAtMs = cursorMs + 1 + static_cast<std::int64_t>(rng.below(8000));
    oneReport.activity = randomKnownActivity(rng);
    oneReport.confidence = Confidence::High;
    list.reports[list.howMany] = oneReport;
    list.howMany += 1;

    // The next report starts where this one ended, or after a hole.
    const bool leaveAHole = rng.below(2) == 0;
    cursorMs = oneReport.endedAtMs + (leaveAHole ? static_cast<std::int64_t>(rng.below(5000)) : 0);
  }
  return list;
}

PhoneActivityHistory historyOf(const ReportList& list, std::size_t positionToLeaveOut) {
  PhoneActivityHistory history;
  for (std::size_t position = 0; position < list.howMany; ++position) {
    if (position != positionToLeaveOut) {
      history.store(list.reports[position]);
    }
  }
  return history;
}

const std::size_t LEAVE_NOTHING_OUT = 1000;

}  // namespace

// --- The examples at the top of PhoneActivityTimeline.cpp ---------------------------

TEST_CASE("example 1: two reports with a hole between them") {
  const PhoneActivityHistory history = historyWith({
      report(PhoneActivity::StandingStill, 0, 10),
      report(PhoneActivity::Walking, 15, 30),
  });
  const TimeInEachPhoneActivity timeInEachActivity = timeSinceSecondZero(history, 30);

  CHECK(timeInEachActivity.millisecondsStandingStill == 10000);
  CHECK(timeInEachActivity.millisecondsWalking == 15000);
  CHECK(timeInEachActivity.millisecondsUnknown == 5000);
  CHECK(timeInEachActivity.totalMilliseconds() == 30000);
  CHECK_FALSE(timeInEachActivity.twoReportsDisagreed);

  CHECK(metersFor(timeInEachActivity) == doctest::Approx(275.0));  // 5 + 45 + 225
}

TEST_CASE("no reports at all: the whole time is unknown") {
  const TimeInEachPhoneActivity timeInEachActivity =
      timeSinceSecondZero(PhoneActivityHistory{}, 5);

  CHECK(timeInEachActivity.millisecondsUnknown == 5000);
  CHECK(timeInEachActivity.weKnowNothing());
  CHECK(metersFor(timeInEachActivity) == doctest::Approx(225.0));  // MOT-009: 45 x 5
}

// --- The tests listed in the spec ----------------------------------------------------

TEST_CASE("MOT-001 'standing still' right now does not cover the last minute") {
  // Example 2 at the top of PhoneActivityTimeline.cpp.
  const PhoneActivityHistory history =
      historyWith({report(PhoneActivity::StandingStill, 59, 60)});
  const TimeInEachPhoneActivity timeInEachActivity = timeSinceSecondZero(history, 60);

  CHECK(timeInEachActivity.millisecondsStandingStill == 1000);
  CHECK(timeInEachActivity.millisecondsUnknown == 59000);
  CHECK_FALSE(timeInEachActivity.standingStillTheWholeTime());
  CHECK(metersFor(timeInEachActivity) == doctest::Approx(2655.5));  // 0.5 x 1 + 45 x 59
}

TEST_CASE("MOT-002 MOT-031 one report of standing still that covers the whole time") {
  // The report started before the reading was taken and reaches until now.
  // There is no other report in between: one is enough.
  const PhoneActivityHistory history =
      historyWith({report(PhoneActivity::StandingStill, -5, 30)});
  const TimeInEachPhoneActivity timeInEachActivity = timeSinceSecondZero(history, 30);

  CHECK(timeInEachActivity.millisecondsStandingStill == 30000);
  CHECK(timeInEachActivity.millisecondsUnknown == 0);
  CHECK(timeInEachActivity.standingStillTheWholeTime());

  const DistancePhoneCouldHaveMoved moved =
      calculateDistancePhoneCouldHaveMoved(readingAt(0), timeInEachActivity);
  CHECK(moved.meters == doctest::Approx(15.0));  // 0.5 x 30
  CHECK(moved.howItWasCalculated == HowDistanceWasCalculated::FromPhoneActivity);

  SUBCASE("a report that stops one second before now leaves that second unknown") {
    const TimeInEachPhoneActivity stoppedEarly = timeSinceSecondZero(
        historyWith({report(PhoneActivity::StandingStill, -5, 29)}), 30);
    CHECK(stoppedEarly.millisecondsStandingStill == 29000);
    CHECK(stoppedEarly.millisecondsUnknown == 1000);
    CHECK_FALSE(stoppedEarly.standingStillTheWholeTime());
    CHECK(metersFor(stoppedEarly) == doctest::Approx(59.5));  // 0.5 x 29 + 45 x 1
  }
}

TEST_CASE("MOT-003 MOT-023 a hole between two reports is unknown") {
  const PhoneActivityHistory history = historyWith({
      report(PhoneActivity::StandingStill, 0, 10),
      report(PhoneActivity::StandingStill, 15, 30),
  });
  const TimeInEachPhoneActivity timeInEachActivity = timeSinceSecondZero(history, 30);

  CHECK(timeInEachActivity.millisecondsStandingStill == 25000);
  CHECK(timeInEachActivity.millisecondsUnknown == 5000);
  CHECK_FALSE(timeInEachActivity.standingStillTheWholeTime());
  CHECK(metersFor(timeInEachActivity) == doctest::Approx(237.5));  // 0.5 x 25 + 45 x 5

  SUBCASE("a hole of one second counts too") {
    const TimeInEachPhoneActivity smallHole = timeSinceSecondZero(
        historyWith({
            report(PhoneActivity::StandingStill, 0, 10),
            report(PhoneActivity::StandingStill, 11, 30),
        }),
        30);
    CHECK(smallHole.millisecondsUnknown == 1000);
    CHECK(smallHole.millisecondsStandingStill == 29000);
  }
  SUBCASE("a report that starts after the reading leaves the beginning unknown") {
    const TimeInEachPhoneActivity lateStart = timeSinceSecondZero(
        historyWith({report(PhoneActivity::StandingStill, 12, 30)}), 30);
    CHECK(lateStart.millisecondsUnknown == 12000);
    CHECK(lateStart.millisecondsStandingStill == 18000);
  }
}

TEST_CASE("MOT-004 standing still until 10 seconds ago, then walking") {
  const PhoneActivityHistory history = historyWith({
      report(PhoneActivity::StandingStill, 0, 20),
      report(PhoneActivity::Walking, 20, 30),
  });
  const TimeInEachPhoneActivity timeInEachActivity = timeSinceSecondZero(history, 30);

  // Two reports that touch leave no hole, and they do not disagree: one
  // ends at the same moment the other starts.
  CHECK(timeInEachActivity.millisecondsStandingStill == 20000);
  CHECK(timeInEachActivity.millisecondsWalking == 10000);
  CHECK(timeInEachActivity.millisecondsUnknown == 0);
  CHECK_FALSE(timeInEachActivity.twoReportsDisagreed);

  // The reading is not treated as if the phone had stood still all along.
  CHECK_FALSE(timeInEachActivity.standingStillTheWholeTime());
  CHECK(metersFor(timeInEachActivity) == doctest::Approx(40.0));  // 0.5 x 20 + 3 x 10
}

TEST_CASE("MOT-005 to MOT-008 one report that covers the whole time") {
  CHECK(metersFor(timeSinceSecondZero(historyWith({report(PhoneActivity::InVehicle, 0, 10)}),
                                      10)) == doctest::Approx(450.0));
  CHECK(metersFor(timeSinceSecondZero(historyWith({report(PhoneActivity::Walking, 0, 5)}), 5)) ==
        doctest::Approx(15.0));
  CHECK(metersFor(timeSinceSecondZero(historyWith({report(PhoneActivity::Running, 0, 5)}), 5)) ==
        doctest::Approx(40.0));
  CHECK(metersFor(timeSinceSecondZero(historyWith({report(PhoneActivity::Cycling, 0, 5)}), 5)) ==
        doctest::Approx(60.0));
}

TEST_CASE("MOT-013 the order in which the reports arrive does not matter") {
  const PhoneActivityReport first = report(PhoneActivity::StandingStill, 0, 10);
  const PhoneActivityReport second = report(PhoneActivity::Walking, 10, 18);
  const PhoneActivityReport third = report(PhoneActivity::InVehicle, 22, 30);

  const TimeInEachPhoneActivity inOrder =
      timeSinceSecondZero(historyWith({first, second, third}), 30);
  const TimeInEachPhoneActivity backwards =
      timeSinceSecondZero(historyWith({third, second, first}), 30);
  const TimeInEachPhoneActivity mixed =
      timeSinceSecondZero(historyWith({second, third, first}), 30);

  CHECK(inOrder.millisecondsStandingStill == 10000);
  CHECK(inOrder.millisecondsWalking == 8000);
  CHECK(inOrder.millisecondsInVehicle == 8000);
  CHECK(inOrder.millisecondsUnknown == 4000);
  CHECK(sameTable(inOrder, backwards));
  CHECK(sameTable(inOrder, mixed));
}

TEST_CASE("MOT-014 two reports that say different things about the same time") {
  // Example 3 at the top of PhoneActivityTimeline.cpp.
  const PhoneActivityHistory history = historyWith({
      report(PhoneActivity::StandingStill, 0, 30),
      report(PhoneActivity::Walking, 10, 20),
  });
  const TimeInEachPhoneActivity timeInEachActivity = timeSinceSecondZero(history, 30);

  CHECK(timeInEachActivity.millisecondsStandingStill == 20000);
  CHECK(timeInEachActivity.millisecondsWalking == 0);
  CHECK(timeInEachActivity.millisecondsUnknown == 10000);
  CHECK(timeInEachActivity.twoReportsDisagreed);

  // Worse than believing either of the two reports.
  CHECK(metersFor(timeInEachActivity) == doctest::Approx(460.0));  // 0.5 x 20 + 45 x 10

  SUBCASE("standing still and in a vehicle") {
    const TimeInEachPhoneActivity table = timeSinceSecondZero(
        historyWith({
            report(PhoneActivity::StandingStill, 0, 30),
            report(PhoneActivity::InVehicle, 0, 30),
        }),
        30);
    CHECK(table.millisecondsUnknown == 30000);
    CHECK(table.twoReportsDisagreed);
    CHECK(table.weKnowNothing());
  }
  SUBCASE("two reports that say the same thing do not disagree") {
    const TimeInEachPhoneActivity table = timeSinceSecondZero(
        historyWith({
            report(PhoneActivity::StandingStill, 0, 20),
            report(PhoneActivity::StandingStill, 10, 30),
        }),
        30);
    CHECK(table.millisecondsStandingStill == 30000);
    CHECK_FALSE(table.twoReportsDisagreed);
    CHECK(table.standingStillTheWholeTime());
  }
  SUBCASE("three reports: two agree, the third does not") {
    const TimeInEachPhoneActivity table = timeSinceSecondZero(
        historyWith({
            report(PhoneActivity::StandingStill, 0, 30),
            report(PhoneActivity::StandingStill, 5, 25),
            report(PhoneActivity::Running, 10, 20),
        }),
        30);
    CHECK(table.millisecondsStandingStill == 20000);
    CHECK(table.millisecondsUnknown == 10000);
    CHECK(table.twoReportsDisagreed);
  }
}

TEST_CASE("MOT-015 a report that ended long ago is not stretched until now") {
  // The phone said "walking" for 5 seconds and then said nothing for 30.
  const PhoneActivityHistory history = historyWith({report(PhoneActivity::Walking, 0, 5)});
  const TimeInEachPhoneActivity timeInEachActivity = timeSinceSecondZero(history, 35);

  CHECK(timeInEachActivity.millisecondsWalking == 5000);
  CHECK(timeInEachActivity.millisecondsUnknown == 30000);
  CHECK(metersFor(timeInEachActivity) == doctest::Approx(1365.0));  // 3 x 5 + 45 x 30
}

TEST_CASE("MOT-020 a vehicle that parked 2 seconds ago") {
  const PhoneActivityHistory history = historyWith({
      report(PhoneActivity::InVehicle, 0, 28),
      report(PhoneActivity::StandingStill, 28, 30),
  });
  const TimeInEachPhoneActivity timeInEachActivity = timeSinceSecondZero(history, 30);

  CHECK(timeInEachActivity.millisecondsInVehicle == 28000);
  CHECK(timeInEachActivity.millisecondsStandingStill == 2000);
  CHECK_FALSE(timeInEachActivity.standingStillTheWholeTime());

  // The 28 seconds in the vehicle are not forgotten because it stopped.
  CHECK(metersFor(timeInEachActivity) == doctest::Approx(1261.0));  // 45 x 28 + 0.5 x 2
}

TEST_CASE("MOT-022 a report the phone is not very sure about is ignored") {
  // Example 4 at the top of PhoneActivityTimeline.cpp.
  for (const Confidence confidence : {Confidence::Medium, Confidence::Low, Confidence::Unknown}) {
    const TimeInEachPhoneActivity timeInEachActivity = timeSinceSecondZero(
        historyWith({report(PhoneActivity::StandingStill, 0, 30, confidence)}), 30);
    CHECK(timeInEachActivity.millisecondsUnknown == 30000);
    CHECK(timeInEachActivity.millisecondsStandingStill == 0);
    CHECK(timeInEachActivity.weKnowNothing());
  }

  SUBCASE("a weak report does not disagree with a strong one either") {
    const TimeInEachPhoneActivity table = timeSinceSecondZero(
        historyWith({
            report(PhoneActivity::StandingStill, 0, 30),
            report(PhoneActivity::InVehicle, 0, 30, Confidence::Low),
        }),
        30);
    CHECK(table.millisecondsStandingStill == 30000);
    CHECK_FALSE(table.twoReportsDisagreed);
  }
}

// --- Reports that cannot be used, and the edges of time --------------------------------

TEST_CASE("a report of 'unknown' activity tells us nothing") {
  const TimeInEachPhoneActivity timeInEachActivity =
      timeSinceSecondZero(historyWith({report(PhoneActivity::Unknown, 0, 30)}), 30);
  CHECK(timeInEachActivity.millisecondsUnknown == 30000);
  CHECK_FALSE(timeInEachActivity.twoReportsDisagreed);

  SUBCASE("and it does not disagree with a report of a real activity") {
    const TimeInEachPhoneActivity table = timeSinceSecondZero(
        historyWith({
            report(PhoneActivity::StandingStill, 0, 30),
            report(PhoneActivity::Unknown, 0, 30),
        }),
        30);
    CHECK(table.millisecondsStandingStill == 30000);
    CHECK_FALSE(table.twoReportsDisagreed);
  }
}

TEST_CASE("a report that ends before it starts, or lasts no time, is ignored") {
  const TimeInEachPhoneActivity timeInEachActivity = timeSinceSecondZero(
      historyWith({
          report(PhoneActivity::StandingStill, 20, 10),
          report(PhoneActivity::Walking, 15, 15),
      }),
      30);
  CHECK(timeInEachActivity.millisecondsUnknown == 30000);
  CHECK(timeInEachActivity.weKnowNothing());
}

TEST_CASE("only the part of a report between the reading and now counts") {
  SUBCASE("a report that starts before the reading and ends after now") {
    const TimeInEachPhoneActivity table = timeSinceSecondZero(
        historyWith({report(PhoneActivity::Walking, -100, 500)}), 30);
    CHECK(table.millisecondsWalking == 30000);
    CHECK(table.totalMilliseconds() == 30000);
  }
  SUBCASE("a report that ended before the reading was taken") {
    const TimeInEachPhoneActivity table = timeSinceSecondZero(
        historyWith({report(PhoneActivity::StandingStill, -60, -1)}), 30);
    CHECK(table.millisecondsUnknown == 30000);
  }
  SUBCASE("a report about the future") {
    const TimeInEachPhoneActivity table = timeSinceSecondZero(
        historyWith({report(PhoneActivity::StandingStill, 31, 90)}), 30);
    CHECK(table.millisecondsUnknown == 30000);
  }
  SUBCASE("a report that ends exactly when the reading was taken") {
    const TimeInEachPhoneActivity table = timeSinceSecondZero(
        historyWith({report(PhoneActivity::StandingStill, -60, 0)}), 30);
    CHECK(table.millisecondsUnknown == 30000);
  }
}

TEST_CASE("the reading does not have to be at second zero") {
  // Reading taken at second 100, judged at second 130.
  const PhoneActivityHistory history = historyWith({
      report(PhoneActivity::StandingStill, 90, 110),
      report(PhoneActivity::Walking, 110, 125),
  });
  const TimeInEachPhoneActivity timeInEachActivity =
      calculateTimeInEachPhoneActivity(history, 100000, 130000);

  CHECK(timeInEachActivity.millisecondsStandingStill == 10000);
  CHECK(timeInEachActivity.millisecondsWalking == 15000);
  CHECK(timeInEachActivity.millisecondsUnknown == 5000);
}

TEST_CASE("when now is not after the reading, no time has passed") {
  const PhoneActivityHistory history =
      historyWith({report(PhoneActivity::StandingStill, 0, 30)});

  CHECK(calculateTimeInEachPhoneActivity(history, 5000, 5000).totalMilliseconds() == 0);
  CHECK(calculateTimeInEachPhoneActivity(history, 9000, 5000).totalMilliseconds() == 0);
  CHECK(calculateTimeInEachPhoneActivity(history, 5000, 5000).weKnowNothing());
}

TEST_CASE("extreme times do not break the calculation") {
  const std::int64_t earliest = std::numeric_limits<std::int64_t>::min();
  const std::int64_t latest = std::numeric_limits<std::int64_t>::max();

  // A time before zero is treated as zero.
  const TimeInEachPhoneActivity fromBeforeZero =
      calculateTimeInEachPhoneActivity(PhoneActivityHistory{}, -5000, 3000);
  CHECK(fromBeforeZero.millisecondsUnknown == 3000);

  const PhoneActivityHistory history = historyWith({report(PhoneActivity::StandingStill, 1, 2)});
  const TimeInEachPhoneActivity everything =
      calculateTimeInEachPhoneActivity(history, earliest, latest);
  CHECK(everything.millisecondsStandingStill == 1000);
  CHECK(everything.totalMilliseconds() == latest);

  PhoneActivityHistory extremeReports;
  PhoneActivityReport huge;
  huge.startedAtMs = earliest;
  huge.endedAtMs = latest;
  huge.activity = PhoneActivity::Walking;
  huge.confidence = Confidence::High;
  extremeReports.store(huge);
  CHECK(calculateTimeInEachPhoneActivity(extremeReports, 0, 30000).millisecondsWalking == 30000);

  CHECK(calculateTimeInEachPhoneActivity(history, latest, earliest).totalMilliseconds() == 0);
}

TEST_CASE("the history keeps at most 32 reports") {
  PhoneActivityHistory history;
  CHECK(history.size() == 0);

  // 32 reports of one second each, one after the other.
  for (int second = 0; second < 32; ++second) {
    const PhoneActivity activity =
        (second % 2 == 0) ? PhoneActivity::StandingStill : PhoneActivity::Walking;
    CHECK(history.store(report(activity, second, second + 1)));
  }
  CHECK(history.size() == MAX_STORED_ACTIVITY_REPORTS);

  // There is no room for one more.
  CHECK_FALSE(history.store(report(PhoneActivity::InVehicle, 0, 32)));
  CHECK(history.size() == 32);

  // All 32 are used, and the one that did not fit is not.
  const TimeInEachPhoneActivity timeInEachActivity = timeSinceSecondZero(history, 32);
  CHECK(timeInEachActivity.millisecondsStandingStill == 16000);
  CHECK(timeInEachActivity.millisecondsWalking == 16000);
  CHECK(timeInEachActivity.millisecondsUnknown == 0);
  CHECK_FALSE(timeInEachActivity.twoReportsDisagreed);

  history.clear();
  CHECK(history.size() == 0);
  CHECK(timeSinceSecondZero(history, 32).weKnowNothing());
}

// --- Rules that must hold for any set of reports ------------------------------------------

TEST_CASE("property: the lines always add up to the time since the reading") {
  DeterministicRng rng(20261116);

  for (int round = 0; round < 3000; ++round) {
    // Reports of any kind: overlapping, weak, backwards, outside the window.
    PhoneActivityHistory history;
    const std::size_t howMany = static_cast<std::size_t>(rng.below(12));
    for (std::size_t index = 0; index < howMany; ++index) {
      PhoneActivityReport oneReport;
      oneReport.startedAtMs = static_cast<std::int64_t>(rng.below(50000)) - 10000;
      oneReport.endedAtMs = oneReport.startedAtMs + static_cast<std::int64_t>(rng.below(20000)) -
                            2000;
      oneReport.activity = static_cast<PhoneActivity>(rng.below(6));
      oneReport.confidence = static_cast<Confidence>(rng.below(4));
      history.store(oneReport);
    }

    const std::int64_t readingTakenAtMs = static_cast<std::int64_t>(rng.below(20000));
    const std::int64_t nowMs = readingTakenAtMs + static_cast<std::int64_t>(rng.below(30000));

    const TimeInEachPhoneActivity timeInEachActivity =
        calculateTimeInEachPhoneActivity(history, readingTakenAtMs, nowMs);

    REQUIRE(timeInEachActivity.totalMilliseconds() == nowMs - readingTakenAtMs);
    REQUIRE(timeInEachActivity.millisecondsStandingStill >= 0);
    REQUIRE(timeInEachActivity.millisecondsWalking >= 0);
    REQUIRE(timeInEachActivity.millisecondsRunning >= 0);
    REQUIRE(timeInEachActivity.millisecondsCycling >= 0);
    REQUIRE(timeInEachActivity.millisecondsInVehicle >= 0);
    REQUIRE(timeInEachActivity.millisecondsUnknown >= 0);
  }
}

TEST_CASE("property: MOT-013 shuffling the reports changes nothing") {
  DeterministicRng rng(0x4D4F543133ULL);

  for (int round = 0; round < 2000; ++round) {
    std::array<PhoneActivityReport, 8> reports{};
    const std::size_t howMany = 1 + static_cast<std::size_t>(rng.below(8));
    for (std::size_t index = 0; index < howMany; ++index) {
      reports[index].startedAtMs = static_cast<std::int64_t>(rng.below(30000));
      reports[index].endedAtMs =
          reports[index].startedAtMs + static_cast<std::int64_t>(rng.below(15000));
      reports[index].activity = static_cast<PhoneActivity>(rng.below(6));
      reports[index].confidence = (rng.below(4) == 0) ? Confidence::Low : Confidence::High;
    }

    PhoneActivityHistory inOrder;
    for (std::size_t index = 0; index < howMany; ++index) {
      inOrder.store(reports[index]);
    }

    // Swap the reports around at random.
    for (std::size_t index = howMany - 1; index > 0; --index) {
      const std::size_t other = static_cast<std::size_t>(rng.below(index + 1));
      const PhoneActivityReport kept = reports[index];
      reports[index] = reports[other];
      reports[other] = kept;
    }
    PhoneActivityHistory shuffled;
    for (std::size_t index = 0; index < howMany; ++index) {
      shuffled.store(reports[index]);
    }

    REQUIRE(sameTable(calculateTimeInEachPhoneActivity(inOrder, 0, 30000),
                      calculateTimeInEachPhoneActivity(shuffled, 0, 30000)));
  }
}

TEST_CASE("property: INV-08 losing a report never lowers the distance") {
  DeterministicRng rng(0x4C4F5354ULL);

  for (int round = 0; round < 4000; ++round) {
    const ReportList list = randomReportsThatDoNotOverlap(rng);

    LocationReading reading = readingAt(0);
    const bool gpsReportedSpeed = rng.below(2) == 0;
    if (gpsReportedSpeed) {
      reading.speedMetersPerSecond = rng.uniform(0.0, 40.0);
      reading.speedAccuracyMetersPerSecond = rng.uniform(0.0, 5.0);
    }

    const TimeInEachPhoneActivity withAllReports =
        calculateTimeInEachPhoneActivity(historyOf(list, LEAVE_NOTHING_OUT), 0, 30000);
    REQUIRE_FALSE(withAllReports.twoReportsDisagreed);
    const double metersWithAllReports =
        calculateDistancePhoneCouldHaveMoved(reading, withAllReports).meters;

    // Take away each report in turn.
    for (std::size_t lost = 0; lost < list.howMany; ++lost) {
      const TimeInEachPhoneActivity withOneLess =
          calculateTimeInEachPhoneActivity(historyOf(list, lost), 0, 30000);

      REQUIRE(withOneLess.millisecondsUnknown >= withAllReports.millisecondsUnknown);

      // A nanometer of slack. "In a vehicle" and "unknown" both count at
      // 45 m/s, so losing a vehicle report changes nothing in theory. In
      // practice the same sum is made in a different grouping and can differ
      // in the 13th decimal place.
      const double oneNanometer = 1e-9;
      REQUIRE(calculateDistancePhoneCouldHaveMoved(reading, withOneLess).meters >=
              metersWithAllReports - oneNanometer);
    }
  }
}

TEST_CASE("property: MOT-029 more time never gives a shorter distance") {
  DeterministicRng rng(0x54494D45ULL);

  for (int round = 0; round < 4000; ++round) {
    const ReportList list = randomReportsThatDoNotOverlap(rng);
    const PhoneActivityHistory history = historyOf(list, LEAVE_NOTHING_OUT);

    const std::int64_t readingTakenAtMs = 5000 + static_cast<std::int64_t>(rng.below(10000));
    const std::int64_t nowMs = readingTakenAtMs + static_cast<std::int64_t>(rng.below(15000));

    const double meters = metersFor(
        calculateTimeInEachPhoneActivity(history, readingTakenAtMs, nowMs));

    // The same reports with an older reading.
    const std::int64_t olderReadingMs =
        readingTakenAtMs - static_cast<std::int64_t>(rng.below(5000));
    REQUIRE(metersFor(calculateTimeInEachPhoneActivity(history, olderReadingMs, nowMs)) >=
            meters);

    // The same reports judged later.
    const std::int64_t laterMs = nowMs + static_cast<std::int64_t>(rng.below(5000));
    REQUIRE(metersFor(calculateTimeInEachPhoneActivity(history, readingTakenAtMs, laterMs)) >=
            meters);
  }
}

TEST_CASE("property: cutting a report in two pieces that touch changes nothing") {
  DeterministicRng rng(0x435554ULL);

  for (int round = 0; round < 2000; ++round) {
    const std::int64_t startMs = static_cast<std::int64_t>(rng.below(20000));
    const std::int64_t endMs = startMs + 2 + static_cast<std::int64_t>(rng.below(20000));
    const std::uint64_t placesToCut = static_cast<std::uint64_t>(endMs - startMs - 1);
    const std::int64_t cutMs = startMs + 1 + static_cast<std::int64_t>(rng.below(placesToCut));
    const PhoneActivity activity = randomKnownActivity(rng);

    PhoneActivityReport whole;
    whole.startedAtMs = startMs;
    whole.endedAtMs = endMs;
    whole.activity = activity;
    whole.confidence = Confidence::High;

    PhoneActivityReport firstPiece = whole;
    firstPiece.endedAtMs = cutMs;
    PhoneActivityReport secondPiece = whole;
    secondPiece.startedAtMs = cutMs;

    PhoneActivityHistory oneReport;
    oneReport.store(whole);
    PhoneActivityHistory twoReports;
    twoReports.store(firstPiece);
    twoReports.store(secondPiece);

    REQUIRE(sameTable(calculateTimeInEachPhoneActivity(oneReport, 0, 30000),
                      calculateTimeInEachPhoneActivity(twoReports, 0, 30000)));
  }
}
