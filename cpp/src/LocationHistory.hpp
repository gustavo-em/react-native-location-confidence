// The short memory of one session: the last few location readings.
//
// Why keep a history? If the GPS fails to answer right now, a good reading
// from 10 seconds ago may still be useful. And several readings that agree
// with each other are stronger evidence than a single one.
//
// Rules of this memory:
//   - it holds at most 10 readings (MAX_STORED_LOCATIONS);
//   - readings are kept sorted by the time they were taken, oldest first;
//   - the same reading delivered twice is stored only once;
//   - readings older than 2 minutes are forgotten;
//   - it lives only in RAM: nothing is ever written to disk.
//
// It must be used from one thread at a time. The platform code guarantees
// that by sending all work for the engine through a single queue.
#ifndef LCE_LOCATION_HISTORY_HPP
#define LCE_LOCATION_HISTORY_HPP

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

#include "Types.hpp"

namespace lce {

class LocationHistory {
 public:
  // What happened when we tried to store a reading.
  enum class StoreResult : std::uint8_t {
    // Stored normally.
    Stored,
    // Stored, but it was taken BEFORE a reading we already had. This happens
    // when the system delivers readings late or out of order.
    StoredOutOfOrder,
    // Not stored: it is the same reading we already have, delivered again.
    IgnoredDuplicate,
    // Not stored: it was already older than 2 minutes when it arrived.
    IgnoredTooOld,
    // Not stored: the memory is full and this reading is older than
    // everything we could remove to make room for it.
    IgnoredOlderThanAllStored,
  };

  // Counters for the diagnostics part of the final report.
  struct Stats {
    std::uint64_t received = 0;           // every reading offered to `store`
    std::uint64_t duplicates = 0;         // ignored because we already had them
    std::uint64_t removedToMakeRoom = 0;  // lost their place because the memory was full
    std::uint64_t removedForAge = 0;      // forgotten for being older than 2 minutes
  };

  static constexpr std::size_t capacity() noexcept { return MAX_STORED_LOCATIONS; }

  // Tries to store one reading that already passed `checkReading`.
  //
  // `protectedArrivalNumber` (optional) names the one reading that must not
  // be removed to make room: the best reading we currently have. Without
  // this, ten poor readings in a row would push out the only good one.
  StoreResult store(const LocationReading& newReading,
                    std::int64_t nowMs,
                    std::optional<std::uint64_t> protectedArrivalNumber = std::nullopt) noexcept;

  // Forgets every reading older than 2 minutes.
  void forgetOldReadings(std::int64_t nowMs) noexcept;

  // Forgets everything. Used when the app clears or stops a session, so no
  // trace of one user's locations is left for the next.
  void clear() noexcept;

  std::size_t size() const noexcept { return count_; }
  bool empty() const noexcept { return count_ == 0; }

  // Reading at a position, where 0 is the oldest.
  const LocationReading& at(std::size_t index) const noexcept { return readings_[index]; }

  // These two let callers write: for (const LocationReading& r : history)
  const LocationReading* begin() const noexcept { return readings_.data(); }
  const LocationReading* end() const noexcept { return readings_.data() + count_; }

  // The most recently taken reading, or nullptr when the memory is empty.
  const LocationReading* newest() const noexcept {
    return count_ == 0 ? nullptr : &readings_[count_ - 1];
  }

  const Stats& stats() const noexcept { return stats_; }

  // The arrival number the NEXT stored reading will get. A request
  // request asks this when it starts: every reading with this number or a
  // higher one arrived after the request started.
  std::uint64_t arrivalNumberOfNextReading() const noexcept { return nextArrivalNumber_; }

 private:
  bool isFull() const noexcept { return count_ == MAX_STORED_LOCATIONS; }

  // Do we already hold this exact reading?
  bool alreadyHave(const LocationReading& reading) const noexcept;

  // At which position must a reading go so the list stays sorted by time?
  std::size_t findPositionThatKeepsTimeOrder(const LocationReading& reading) const noexcept;

  // The memory is full: which stored reading gives up its place?
  // Returns no position when the answer is "none: drop the new reading".
  std::optional<std::size_t> chooseReadingToRemove(
      std::size_t positionOfNewReading,
      std::optional<std::uint64_t> protectedArrivalNumber) const noexcept;

  void removeAt(std::size_t index) noexcept;
  void insertAt(std::size_t index, const LocationReading& reading) noexcept;

  // Fixed-size storage: 10 slots that always exist. `count_` says how many
  // of them hold a real reading (slots 0 .. count_-1).
  std::array<LocationReading, MAX_STORED_LOCATIONS> readings_{};
  std::size_t count_ = 0;

  std::uint64_t nextArrivalNumber_ = 1;
  Stats stats_{};
};

}  // namespace lce

#endif  // LCE_LOCATION_HISTORY_HPP
