// =============================================================================
// THE ORDER OF THE CALLS IN THIS FILE
// =============================================================================
//
//   calculateTimeInEachPhoneActivity(history, readingTakenAtMs, nowMs)
//     |                                                    <- everything starts here
//     |-- if no time has passed: returns the table with every line at zero
//     |
//     |-- findMomentsOfChange()                            STEP 1
//     |      |-- reportCanBeUsed()                         once for each report
//     |      `-- puts the moments in time order
//     |
//     `-- for each slice of time between two neighbor moments
//            |-- findActivityDuringSlice()                 STEP 2
//            |      `-- reportCanBeUsed()                  once for each report
//            `-- timeInEachActivity.add()                  STEP 3
//                                                          (lives in UncertaintyModel.cpp)
//
// =============================================================================
// EXAMPLES, STEP BY STEP
// Times are written in seconds here. The code works in milliseconds.
// All reports are High (the phone is very sure) unless the example says so.
// =============================================================================
//
// EXAMPLE 1 - two reports with a hole between them
//
//   reading taken at second 0, now is second 30
//   report A: standing still, from second 0 to second 10
//   report B: walking,        from second 15 to second 30
//
//   calculateTimeInEachPhoneActivity(history, 0, 30000)
//     findMomentsOfChange()                 -> 0, 10000, 15000, 30000
//     slice 0 to 10000
//       findActivityDuringSlice()           -> StandingStill   (report A covers it)
//       add(StandingStill, 10000)
//     slice 10000 to 15000
//       findActivityDuringSlice()           -> Unknown         (no report covers it)
//       add(Unknown, 5000)
//     slice 15000 to 30000
//       findActivityDuringSlice()           -> Walking         (report B covers it)
//       add(Walking, 15000)
//     returns   standing still 10000,   walking 15000,   unknown 5000
//
//   What the movement model does with that table:
//     0.5 x 10 + 3 x 15 + 45 x 5 = 5 + 45 + 225 = 275 meters
//
// EXAMPLE 2 - "standing still right now" says nothing about the past
//
//   reading taken at second 0, now is second 60
//   the only report: standing still, from second 59 to second 60
//
//     findMomentsOfChange()                 -> 0, 59000, 60000
//     slice 0 to 59000                      -> Unknown         add(Unknown, 59000)
//     slice 59000 to 60000                  -> StandingStill   add(StandingStill, 1000)
//     returns   standing still 1000,   unknown 59000
//
//   Movement model: 0.5 x 1 + 45 x 59 = 2655.5 meters.
//   One second of "standing still" does not save a reading that is one
//   minute old.
//
// EXAMPLE 3 - two reports disagree
//
//   reading taken at second 0, now is second 30
//   report A: standing still, from second 0 to second 30
//   report B: walking,        from second 10 to second 20
//
//     findMomentsOfChange()                 -> 0, 10000, 20000, 30000
//     slice 0 to 10000        only A covers it          -> StandingStill
//     slice 10000 to 20000    A says standing still, B says walking
//                                                       -> Unknown
//                                                          twoReportsDisagreed = true
//     slice 20000 to 30000    only A covers it          -> StandingStill
//     returns   standing still 20000,   unknown 10000,   twoReportsDisagreed
//
//   Movement model: 0.5 x 20 + 45 x 10 = 460 meters.
//   Not 15 (believing A) and not 40 (believing B for its 10 seconds).
//
// EXAMPLE 4 - the phone is not very sure
//
//   reading taken at second 0, now is second 30
//   the only report: standing still, from second 0 to second 30, Medium
//
//     findMomentsOfChange()                 -> 0, 30000        (the report is not used)
//     slice 0 to 30000                      -> Unknown         add(Unknown, 30000)
//     returns   unknown 30000
//
// =============================================================================
// WHAT THE TABLE IS USED FOR
// =============================================================================
//
// calculateDistancePhoneCouldHaveMoved() in UncertaintyModel.cpp receives the
// table and turns it into meters: for each activity, the speed of that
// activity x the seconds spent in it. Time in the "unknown" line counts at
// the fastest speed, so every hole in the reports makes the distance bigger.
//
// The candidate selector, which is the next piece to be written, will call
// this file once for each reading it wants to judge.
// =============================================================================

#include "PhoneActivityTimeline.hpp"

#include <algorithm>

namespace lce {

// =============================================================================
// HELPERS - used only inside this file
// =============================================================================
namespace {

// The most moments there can be: the reading, now, and the start and the end
// of each of the 32 reports.
constexpr std::size_t MAX_MOMENTS_OF_CHANGE = 2 + 2 * MAX_STORED_ACTIVITY_REPORTS;

// A list of moments, in milliseconds. Only the first `howMany` are used.
struct MomentsOfChange {
  std::array<std::int64_t, MAX_MOMENTS_OF_CHANGE> moments{};
  std::size_t howMany = 0;

  void add(std::int64_t momentMs) noexcept {
    moments[howMany] = momentMs;
    howMany += 1;
  }
};

// What we found for one slice of time.
struct ActivityDuringSlice {
  PhoneActivity activity = PhoneActivity::Unknown;
  bool twoReportsDisagree = false;
};

// Can this report be used at all?
// Yes when the phone is very sure (High), the activity is a real one (not
// Unknown) and the report ends after it starts.
bool reportCanBeUsed(const PhoneActivityReport& report) noexcept {
  const bool phoneIsVerySure = (report.confidence == PhoneActivityConfidence::High);
  const bool activityIsKnown = (report.activity != PhoneActivity::Unknown);
  const bool endsAfterItStarts = (report.endedAtMs > report.startedAtMs);

  return phoneIsVerySure && activityIsKnown && endsAfterItStarts;
}

// Step 1: the moments where something changes, in time order.
// They are: when the reading was taken, now, and every start or end of a
// report that falls between those two.
MomentsOfChange findMomentsOfChange(const PhoneActivityHistory& history,
                                    std::int64_t readingTakenAtMs,
                                    std::int64_t nowMs) noexcept {
  MomentsOfChange result;
  result.add(readingTakenAtMs);
  result.add(nowMs);

  for (std::size_t position = 0; position < history.size(); ++position) {
    const PhoneActivityReport& report = history.at(position);

    if (!reportCanBeUsed(report)) {
      continue;  // this report is ignored
    }

    const bool startIsBetweenReadingAndNow =
        (report.startedAtMs > readingTakenAtMs && report.startedAtMs < nowMs);
    if (startIsBetweenReadingAndNow) {
      result.add(report.startedAtMs);
    }

    const bool endIsBetweenReadingAndNow =
        (report.endedAtMs > readingTakenAtMs && report.endedAtMs < nowMs);
    if (endIsBetweenReadingAndNow) {
      result.add(report.endedAtMs);
    }
  }

  // std::sort puts the first `howMany` moments in time order, earliest first.
  std::sort(result.moments.begin(), result.moments.begin() + result.howMany);

  return result;
}

// Step 2: what was the phone doing during one slice of time?
//
// A report "covers" the slice when it starts at or before the start of the
// slice and ends at or after the end of the slice. Because the slices were
// cut at every start and end of a report, a report either covers a whole
// slice or does not touch it at all.
//
//   no report covers the slice             -> Unknown
//   the reports that cover it all agree    -> that activity
//   two of them say different things       -> Unknown, and we remember that
//                                             two reports disagreed
ActivityDuringSlice findActivityDuringSlice(const PhoneActivityHistory& history,
                                            std::int64_t sliceStartedAtMs,
                                            std::int64_t sliceEndedAtMs) noexcept {
  ActivityDuringSlice result;
  bool someReportCoversTheSlice = false;

  for (std::size_t position = 0; position < history.size(); ++position) {
    const PhoneActivityReport& report = history.at(position);

    const bool reportCoversTheSlice = reportCanBeUsed(report) &&
                                      report.startedAtMs <= sliceStartedAtMs &&
                                      report.endedAtMs >= sliceEndedAtMs;
    if (!reportCoversTheSlice) {
      continue;
    }

    if (!someReportCoversTheSlice) {
      // The first report that covers this slice.
      result.activity = report.activity;
      someReportCoversTheSlice = true;

    } else if (report.activity != result.activity) {
      // Another report covers the same slice and says something different.
      result.twoReportsDisagree = true;
    }
  }

  if (result.twoReportsDisagree) {
    result.activity = PhoneActivity::Unknown;
  }

  return result;
}

}  // namespace

// =============================================================================
// FUNCTIONS OTHER FILES CAN CALL
//
// Only one is announced in PhoneActivityTimeline.hpp.
// =============================================================================

TimeInEachPhoneActivity calculateTimeInEachPhoneActivity(const PhoneActivityHistory& history,
                                                         std::int64_t readingTakenAtMs,
                                                         std::int64_t nowMs) noexcept {
  TimeInEachPhoneActivity timeInEachActivity;

  // The clock of the phone starts at zero, so a time before zero makes no
  // sense. It is treated as zero. (readingTakenAtMs is a copy, so changing it
  // here does not change anything for whoever called this function.)
  if (readingTakenAtMs < 0) {
    readingTakenAtMs = 0;
  }

  const bool noTimeHasPassed = (nowMs <= readingTakenAtMs);
  if (noTimeHasPassed) {
    return timeInEachActivity;
  }

  // Step 1: the moments where something changes, in time order.
  const MomentsOfChange momentsOfChange = findMomentsOfChange(history, readingTakenAtMs, nowMs);

  // Each pair of neighbor moments is one slice of time.
  for (std::size_t position = 0; position + 1 < momentsOfChange.howMany; ++position) {
    const std::int64_t sliceStartedAtMs = momentsOfChange.moments[position];
    const std::int64_t sliceEndedAtMs = momentsOfChange.moments[position + 1];
    const std::int64_t millisecondsOfSlice = sliceEndedAtMs - sliceStartedAtMs;

    // Two reports can change at the same moment. The slice between those two
    // equal moments has no time in it.
    const bool sliceIsEmpty = (millisecondsOfSlice == 0);
    if (sliceIsEmpty) {
      continue;
    }

    // Step 2: what was the phone doing during this slice?
    const ActivityDuringSlice found =
        findActivityDuringSlice(history, sliceStartedAtMs, sliceEndedAtMs);

    // Step 3: add the slice to the line of that activity.
    timeInEachActivity.add(found.activity, millisecondsOfSlice);

    if (found.twoReportsDisagree) {
      timeInEachActivity.twoReportsDisagreed = true;
    }
  }

  return timeInEachActivity;
}

}  // namespace lce
