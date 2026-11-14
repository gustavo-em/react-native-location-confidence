#include "LocationHistory.hpp"

namespace lce {

namespace {

bool isExpired(const LocationSample& sample, std::int64_t nowMonotonicMs) noexcept {
  return ageMs(sample, nowMonotonicMs) > kHistoryRetentionMs;
}

}  // namespace

bool LocationHistory::containsDuplicateOf(const LocationSample& sample) const noexcept {
  for (std::size_t i = 0; i < size_; ++i) {
    const LocationSample& stored = samples_[i];
    // Strict equivalence: a repeated callback, not merely a nearby reading.
    if (stored.provider == sample.provider &&
        stored.fixTimeMonotonicMs == sample.fixTimeMonotonicMs &&
        stored.position.latitudeDeg == sample.position.latitudeDeg &&
        stored.position.longitudeDeg == sample.position.longitudeDeg &&
        stored.horizontalAccuracyMeters == sample.horizontalAccuracyMeters) {
      return true;
    }
  }
  return false;
}

void LocationHistory::removeAt(std::size_t index) noexcept {
  for (std::size_t i = index; i + 1 < size_; ++i) {
    samples_[i] = samples_[i + 1];
  }
  --size_;
  samples_[size_] = LocationSample{};  // do not keep stale coordinates around
}

void LocationHistory::insertAt(std::size_t index, const LocationSample& sample) noexcept {
  for (std::size_t i = size_; i > index; --i) {
    samples_[i] = samples_[i - 1];
  }
  samples_[index] = sample;
  ++size_;
}

void LocationHistory::prune(std::int64_t nowMonotonicMs) noexcept {
  std::size_t i = 0;
  while (i < size_) {
    if (isExpired(samples_[i], nowMonotonicMs)) {
      removeAt(i);
      ++stats_.pruned;
    } else {
      ++i;
    }
  }
}

void LocationHistory::clear() noexcept {
  for (std::size_t i = 0; i < size_; ++i) {
    samples_[i] = LocationSample{};
  }
  size_ = 0;
}

LocationHistory::InsertResult LocationHistory::insert(
    const LocationSample& sample,
    std::int64_t nowMonotonicMs,
    std::optional<std::uint64_t> protectedSequence) noexcept {
  ++stats_.received;

  prune(nowMonotonicMs);

  if (isExpired(sample, nowMonotonicMs)) {
    ++stats_.pruned;
    return InsertResult::DroppedExpired;
  }

  if (containsDuplicateOf(sample)) {
    ++stats_.duplicates;
    return InsertResult::Duplicate;
  }

  // Position that keeps ascending capture time; equal times go after the
  // fixes already stored, so arrival order breaks the tie.
  std::size_t position = size_;
  while (position > 0 && samples_[position - 1].fixTimeMonotonicMs > sample.fixTimeMonotonicMs) {
    --position;
  }
  const bool outOfOrder = position < size_;

  if (size_ == kHistoryCapacity) {
    // Victim: the oldest fix that is not protected, considering the new fix
    // at `position` as part of the ordering.
    std::size_t victim = 0;
    bool victimIsIncoming = false;
    for (std::size_t slot = 0; slot <= size_; ++slot) {
      if (slot == position) {
        victimIsIncoming = true;
        break;
      }
      const std::size_t stored = slot < position ? slot : slot - 1;
      if (!protectedSequence || samples_[stored].sequence != *protectedSequence) {
        victim = stored;
        break;
      }
    }

    ++stats_.evicted;
    if (victimIsIncoming) {
      return InsertResult::DroppedAsOldest;
    }
    removeAt(victim);
    if (victim < position) {
      --position;
    }
  }

  LocationSample stored = sample;
  stored.sequence = nextSequence_++;
  insertAt(position, stored);

  return outOfOrder ? InsertResult::InsertedOutOfOrder : InsertResult::Inserted;
}

}  // namespace lce
