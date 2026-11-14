// Bounded in-memory history of measured fixes for one session.
//
// Fixed capacity, no allocation, ordered by capture time. Not thread-safe:
// the owning session is confined to a single serial queue by the platform.
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
  enum class InsertResult : std::uint8_t {
    Inserted,
    // Stored, but its capture time is older than a fix already in the buffer.
    InsertedOutOfOrder,
    // Same provider, capture time, position and accuracy as a stored fix.
    Duplicate,
    // Older than the retention window when it arrived.
    DroppedExpired,
    // Buffer full and the new fix would itself be the eviction victim.
    DroppedAsOldest,
  };

  struct Stats {
    std::uint64_t received = 0;
    std::uint64_t duplicates = 0;
    std::uint64_t evicted = 0;
    std::uint64_t pruned = 0;
  };

  static constexpr std::size_t capacity() noexcept { return kHistoryCapacity; }

  // Inserts a validated sample, assigning its sequence number.
  //
  // When the buffer is full the oldest fix is evicted, except the one whose
  // sequence equals `protectedSequence` (the current best candidate), so that
  // a burst of poor readings cannot push out the only good one.
  InsertResult insert(const LocationSample& sample,
                      std::int64_t nowMonotonicMs,
                      std::optional<std::uint64_t> protectedSequence = std::nullopt) noexcept;

  // Removes fixes older than the retention window.
  void prune(std::int64_t nowMonotonicMs) noexcept;

  // Erases every stored fix. Sequence numbers keep increasing afterwards.
  void clear() noexcept;

  std::size_t size() const noexcept { return size_; }
  bool empty() const noexcept { return size_ == 0; }

  // Fixes in ascending capture time; ties keep insertion order.
  const LocationSample& at(std::size_t index) const noexcept { return samples_[index]; }
  const LocationSample* begin() const noexcept { return samples_.data(); }
  const LocationSample* end() const noexcept { return samples_.data() + size_; }

  // Most recently captured fix, or nullptr when empty.
  const LocationSample* newest() const noexcept {
    return size_ == 0 ? nullptr : &samples_[size_ - 1];
  }

  const Stats& stats() const noexcept { return stats_; }

 private:
  bool containsDuplicateOf(const LocationSample& sample) const noexcept;
  void removeAt(std::size_t index) noexcept;
  void insertAt(std::size_t index, const LocationSample& sample) noexcept;

  std::array<LocationSample, kHistoryCapacity> samples_{};
  std::size_t size_ = 0;
  std::uint64_t nextSequence_ = 1;
  Stats stats_{};
};

}  // namespace lce

#endif  // LCE_LOCATION_HISTORY_HPP
