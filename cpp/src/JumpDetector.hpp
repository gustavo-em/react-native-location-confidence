// Does any GPS reading look like a "jump"?
//
// Sometimes the GPS makes a mistake for one reading: on the map the phone
// seems to jump 450 meters away and come back two seconds later. Nobody moves
// like that. This file finds those readings.
//
// It works in two steps.
//
//   Step 1 - Take the readings two by two and ask:
//            "could the phone really have gone from one to the other?"
//
//              longest distance possible = fastest speed x seconds between them
//                                          + GPS error of one reading
//                                          + GPS error of the other reading
//
//            If the two readings are farther apart than that, they CONTRADICT
//            each other: one of the two is wrong.
//
//              450 meters apart, taken 2 seconds apart, GPS errors 5 and 5:
//              longest distance possible = 45 x 2 + 5 + 5 = 100 meters
//              450 is more than 100  ->  they contradict each other
//
//   Step 2 - A pair that contradicts tells us that one of the two is wrong,
//            but not which one. So each reading counts the others:
//
//              on its side   itself + every reading that does not contradict it
//              against it    every reading that contradicts it
//
//              nothing against it                 ->  LooksFine
//              more against it than on its side   ->  ProbablyGpsJump
//              anything else                      ->  InConflict
//
// This file only puts a label on each reading. It never removes a reading,
// never changes one and never invents a position.
//
// A picture of the order of the calls, with examples worked step by step, is
// at the top of JumpDetector.cpp.
#ifndef LCE_JUMP_DETECTOR_HPP
#define LCE_JUMP_DETECTOR_HPP

#include <array>
#include <cstddef>
#include <cstdint>

#include "LocationHistory.hpp"
#include "Types.hpp"
#include "UncertaintyModel.hpp"

namespace lce {

// The label one reading gets after it is compared with all the others.
enum class JumpCheckResult : std::uint8_t {
  // No other reading contradicts this one.
  LooksFine,

  // Some reading contradicts this one, but this one has at least as many
  // readings on its side. We cannot tell which side is wrong.
  InConflict,

  // More readings contradict this one than are on its side.
  // It is probably a GPS mistake.
  ProbablyGpsJump,
};

// What we found about ONE reading.
struct JumpCheckOfOneReading {
  // The reading itself (that is why it starts at 1) plus every other reading
  // that does not contradict it.
  std::size_t readingsOnItsSide = 1;

  // Every other reading that contradicts it.
  std::size_t readingsAgainstIt = 0;

  JumpCheckResult result = JumpCheckResult::LooksFine;
};

// What we found about ALL the readings of the history.
struct JumpCheckOfAllReadings {
  // One answer for each reading, in the same order as the history: position 0
  // is the oldest reading. Only the first `howManyReadings` positions are used.
  std::array<JumpCheckOfOneReading, MAX_STORED_LOCATIONS> ofEachReading{};
  std::size_t howManyReadings = 0;

  // The totals. The number of GPS jumps is shown in the final report.
  std::size_t howManyAreInConflict = 0;
  std::size_t howManyAreProbablyGpsJumps = 0;

  // True when two readings that contradict each other came from different
  // sources, for example a new reading and an old one the system had stored.
  bool readingsFromDifferentProvidersContradict = false;

  // True when at least one pair of readings contradicts each other.
  bool someReadingsContradictEachOther() const noexcept {
    return howManyAreInConflict + howManyAreProbablyGpsJumps > 0;
  }
};

// Step 1, for one pair of readings.
// True when the phone could NOT have gone from one reading to the other in
// the time between them. The order of the two readings does not matter.
bool readingsContradictEachOther(const LocationReading& oneReading,
                                 const LocationReading& otherReading,
                                 double fastestPossibleSpeedMetersPerSecond =
                                     FASTEST_POSSIBLE_SPEED_METERS_PER_SECOND) noexcept;

// THE MAIN FUNCTION OF THIS FILE.
// Compares every reading of the history with all the others and gives each
// one a label.
//
// The history already guarantees two things this function depends on:
//   - every reading passed the entrance check (checkReading);
//   - the same reading delivered twice is stored only once, so a GPS mistake
//     that is repeated cannot count as support for itself.
JumpCheckOfAllReadings checkAllReadingsForGpsJumps(
    const LocationHistory& history,
    double fastestPossibleSpeedMetersPerSecond =
        FASTEST_POSSIBLE_SPEED_METERS_PER_SECOND) noexcept;

}  // namespace lce

#endif  // LCE_JUMP_DETECTOR_HPP
