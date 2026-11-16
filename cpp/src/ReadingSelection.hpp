// Of all the GPS readings we remember, which one should we use?
//
// The history can hold up to 10 readings. Some are new and some are old, some
// are precise and some are not, and one of them may be a GPS mistake. This
// file picks the reading that tells us the most about where the phone is NOW.
//
// The newest reading is not always the best one, and neither is the most
// precise one:
//
//     reading A: taken 50 seconds ago, GPS error of 5 meters
//     reading B: taken 1 second ago,   GPS error of 200 meters
//
//   If we know nothing about what the phone did, it may have moved a lot in
//   50 seconds, so A says little about now. B is the better one.
//   If the phone reported "standing still" during those 50 seconds, A is
//   still good, and it is much more precise. A is the better one.
//
// So the readings are compared by one number that takes both things into
// account: HOW FAR THE PHONE COULD BE FROM THE READING RIGHT NOW.
//
//     how far the phone could be = GPS error
//                                  + distance the phone could have moved since
//
// How the choice is made:
//
//   Step 1 - Look for GPS jumps (JumpDetector.cpp).
//
//   Step 2 - Examine each reading that is not too old: how long the phone
//            spent in each activity since then (PhoneActivityTimeline.cpp),
//            how far it could have moved, and how far it could be now
//            (UncertaintyModel.cpp).
//
//   Step 3 - Keep the best one:
//              1st  a reading that is probably a GPS jump loses to one that is not
//              2nd  the smaller "how far the phone could be" wins
//              3rd  if they are equal, the newer reading wins
//              4th  if still equal, the smaller GPS error wins
//              5th  if still equal, the one that arrived first wins
//
// The reading that is chosen is always one of the readings the phone really
// measured. This file never mixes two readings and never invents a position.
//
// A picture of the order of the calls, with examples worked step by step, is
// at the top of ReadingSelection.cpp.
#ifndef LCE_READING_SELECTION_HPP
#define LCE_READING_SELECTION_HPP

#include <cstddef>
#include <cstdint>

#include "JumpDetector.hpp"
#include "LocationHistory.hpp"
#include "PhoneActivityTimeline.hpp"
#include "Types.hpp"
#include "UncertaintyModel.hpp"

namespace lce {

// What the app allows when a reading is chosen.
struct RulesForChoosingReading {
  // A reading older than this is never used. Normally 60 seconds.
  std::int64_t oldestAgeAllowedMs = 60000;

  // Normally 45; the app can change it.
  double fastestPossibleSpeedMetersPerSecond = FASTEST_POSSIBLE_SPEED_METERS_PER_SECOND;

  // False when the app asked us not to use what the phone reported about its
  // activity. Then all the time since a reading counts as unknown.
  bool usePhoneActivity = true;

  // Readings whose arrival number is smaller than this are not used.
  // 0 means "any reading can be used". An evaluation request sets it when
  // the app does not want an answer taken from old readings of the history.
  std::uint64_t firstArrivalNumberAllowed = 0;
};

// Everything we worked out about ONE reading.
struct WhatWeKnowAboutReading {
  // A copy of the reading, exactly as it is in the history.
  LocationReading reading;

  // How old it is right now.
  std::int64_t ageMs = 0;

  // How long the phone spent in each activity since the reading was taken.
  TimeInEachPhoneActivity timeInEachActivity;

  // How far the phone could have moved since then, and which option of the
  // movement model gave that number.
  DistancePhoneCouldHaveMoved distanceMoved;

  // GPS error + distance the phone could have moved.
  double howFarPhoneCouldBeMeters = 0.0;

  // What the jump detector found about this reading.
  JumpCheckOfOneReading jumpCheck;

  // True when the speed the GPS reported with this reading does not fit with
  // how far the phone really moved since the previous reading. The reported
  // speed is then ignored when the distance is calculated.
  bool gpsSpeedDoesNotMatchTheMovement = false;
};

// The answer of this file.
struct ChosenReading {
  // False when no reading can be used: the history is empty, or every
  // reading is too old or is not allowed.
  bool found = false;

  // The reading that was chosen. Only meaningful when `found` is true.
  WhatWeKnowAboutReading best;

  // Numbers about all the readings of the history, for the final report.
  std::size_t howManyReadingsWereExamined = 0;
  std::size_t howManyWereTooOld = 0;
  std::size_t howManyAreProbablyGpsJumps = 0;
  bool someReadingsContradictEachOther = false;
  bool readingsFromDifferentProvidersContradict = false;
};

// THE MAIN FUNCTION OF THIS FILE.
// Which reading of the history tells us the most about where the phone is now?
//
//   history            the GPS readings we remember
//   activityHistory    what the phone reported about its activity
//   nowMs              the current time, on the same clock as the readings
//   rules              what the app allows
ChosenReading chooseBestReading(const LocationHistory& history,
                                const PhoneActivityHistory& activityHistory,
                                std::int64_t nowMs,
                                const RulesForChoosingReading& rules) noexcept;

}  // namespace lce

#endif  // LCE_READING_SELECTION_HPP
