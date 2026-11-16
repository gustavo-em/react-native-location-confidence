// Since a GPS reading was taken, what was the phone doing, and for how long?
//
// The phone can tell us about its activity with reports like this one:
//
//     "from second 10 to second 25 I was standing still, and I am very sure"
//
// This file takes all the reports we have and answers, for one GPS reading:
// since that reading was taken, how long did the phone spend in each
// activity? The answer is a small table (TimeInEachPhoneActivity). The
// movement model (UncertaintyModel.cpp) uses that table to calculate how far
// the phone could have moved.
//
// How it works, with a picture. The reading was taken at second 0 and now is
// second 30. We have two reports:
//
//     reading taken                                             now
//          |                                                     |
//          |== standing still ==|          |====== walking ======|
//          0                   10         15                    30   seconds
//
//   Step 1 - Find the moments where something changes: 0, 10, 15 and 30.
//
//   Step 2 - Between two neighbor moments nothing changes. Each of those
//            slices of time gets one activity:
//              0 to 10     standing still     10 seconds
//              10 to 15    nothing reported    5 seconds   ->  unknown
//              15 to 30    walking            15 seconds
//
//   Step 3 - Add the slices to the table:
//              standing still 10000,  walking 15000,  unknown 5000
//
// Four rules keep the answer cautious:
//
//   - Only reports the phone is very sure about (High) are used.
//   - Time that no report covers is unknown. Nothing is guessed from the
//     reports around it: every millisecond needs a report of its own.
//   - When two reports say different things about the same slice of time,
//     that slice is unknown. We never pick the one we like best.
//   - A report says nothing about the time outside it. "Standing still" right
//     now does not mean the phone stood still since the reading was taken.
//
// The activity is about the PHONE, not about the person. A courier can walk
// away and leave the phone on the motorbike.
//
// A picture of the order of the calls, with examples worked step by step, is
// at the top of PhoneActivityTimeline.cpp.
#ifndef LCE_PHONE_ACTIVITY_TIMELINE_HPP
#define LCE_PHONE_ACTIVITY_TIMELINE_HPP

#include <array>
#include <cstddef>
#include <cstdint>

#include "UncertaintyModel.hpp"

namespace lce {

// How sure the phone is about an activity it reported.
enum class PhoneActivityConfidence : std::uint8_t {
  High,
  Medium,
  Low,
  Unknown,  // the phone did not say
};

// One report from the phone: "from this moment to that moment I was doing
// this". The moments use the same clock as LocationReading::capturedAtMs.
struct PhoneActivityReport {
  std::int64_t startedAtMs = 0;
  std::int64_t endedAtMs = 0;
  PhoneActivity activity = PhoneActivity::Unknown;
  PhoneActivityConfidence confidence = PhoneActivityConfidence::Unknown;
};

// The history keeps at most this many reports.
inline constexpr std::size_t MAX_STORED_ACTIVITY_REPORTS = 32;

// The reports received from the phone. Their order has no meaning.
//
// For now this is only a list. Which old reports to forget when it is full
// will be decided together with the sessions, in a later phase.
class PhoneActivityHistory {
 public:
  // Stores one report. When there is no room left it stores nothing and
  // returns false.
  bool store(const PhoneActivityReport& report) noexcept {
    const bool thereIsRoom = (count_ < MAX_STORED_ACTIVITY_REPORTS);
    if (!thereIsRoom) {
      return false;
    }
    reports_[count_] = report;
    count_ += 1;
    return true;
  }

  // Forgets every report.
  void clear() noexcept {
    reports_ = {};
    count_ = 0;
  }

  std::size_t size() const noexcept { return count_; }

  const PhoneActivityReport& at(std::size_t index) const noexcept { return reports_[index]; }

 private:
  // Fixed-size storage: 32 slots that always exist. `count_` says how many
  // of them hold a real report (slots 0 .. count_-1).
  std::array<PhoneActivityReport, MAX_STORED_ACTIVITY_REPORTS> reports_{};
  std::size_t count_ = 0;
};

// THE MAIN FUNCTION OF THIS FILE.
// Since the reading was taken, how long did the phone spend in each activity?
//
//   history            the reports received from the phone
//   readingTakenAtMs   when the GPS reading was taken
//                      (LocationReading::capturedAtMs)
//   nowMs              the current time, on the same clock
//
// The lines of the answer added together are the time from the reading until
// now. When now is not after the reading, no time has passed and every line
// of the table is zero.
TimeInEachPhoneActivity calculateTimeInEachPhoneActivity(const PhoneActivityHistory& history,
                                                         std::int64_t readingTakenAtMs,
                                                         std::int64_t nowMs) noexcept;

// --- Two smaller questions, used to describe the activity in the final report --

// Going back in time from now: since when do we know what the phone was
// doing, with no break? Only the time after the reading is looked at.
// When the most recent moments are unknown, the answer is `nowMs`.
std::int64_t findSinceWhenPhoneActivityIsKnown(const PhoneActivityHistory& history,
                                               std::int64_t readingTakenAtMs,
                                               std::int64_t nowMs) noexcept;

// What the phone is doing at one moment, and how sure it is.
struct PhoneActivityAtOneMoment {
  PhoneActivity activity = PhoneActivity::Unknown;
  PhoneActivityConfidence confidence = PhoneActivityConfidence::Unknown;
};

// What is the phone doing at this moment? The report the phone is most sure
// about wins; two equally sure reports that disagree give Unknown.
PhoneActivityAtOneMoment findPhoneActivityAtMoment(const PhoneActivityHistory& history,
                                                   std::int64_t momentMs) noexcept;

}  // namespace lce

#endif  // LCE_PHONE_ACTIVITY_TIMELINE_HPP
