#include "LocationHistory.hpp"

namespace lce {

namespace {

bool isTooOldToKeep(const LocationReading& reading, std::int64_t nowMs) noexcept {
  return ageInMs(reading, nowMs) > MAX_AGE_IN_HISTORY_MS;
}

// Two readings are "the same reading delivered twice" only when everything
// that identifies them is identical: same provider, same capture time, same
// position and same accuracy.
//
// A reading at the same place but taken one second later is NOT a duplicate:
// it is new evidence that the phone is still there.
bool isSameReading(const LocationReading& first, const LocationReading& second) noexcept {
  return first.provider == second.provider &&
         first.capturedAtMs == second.capturedAtMs &&
         first.position.latitudeDegrees == second.position.latitudeDegrees &&
         first.position.longitudeDegrees == second.position.longitudeDegrees &&
         first.horizontalAccuracyMeters == second.horizontalAccuracyMeters;
}

}  // namespace

bool LocationHistory::alreadyHave(const LocationReading& reading) const noexcept {
  for (std::size_t index = 0; index < count_; ++index) {
    if (isSameReading(readings_[index], reading)) {
      return true;
    }
  }
  return false;
}

std::size_t LocationHistory::findPositionThatKeepsTimeOrder(
    const LocationReading& reading) const noexcept {
  // Start at the end (after the newest) and walk back while the stored
  // reading in front of us was taken later than the new one.
  //
  // Readings with the same capture time are left in front, so among equals
  // the one that arrived first stays first.
  std::size_t position = count_;
  while (position > 0 && readings_[position - 1].capturedAtMs > reading.capturedAtMs) {
    --position;
  }
  return position;
}

std::optional<std::size_t> LocationHistory::chooseReadingToRemove(
    std::size_t positionOfNewReading,
    std::optional<std::uint64_t> protectedArrivalNumber) const noexcept {
  // Picture all readings in a line, oldest first, with the new one standing
  // at `positionOfNewReading`. We walk the line from the oldest and the
  // first reading that is not protected loses its place.
  //
  // Stored readings older than the new one come first in the line:
  for (std::size_t index = 0; index < positionOfNewReading; ++index) {
    const bool isTheProtectedOne =
        protectedArrivalNumber.has_value() &&
        readings_[index].arrivalNumber == *protectedArrivalNumber;
    if (!isTheProtectedOne) {
      return index;
    }
  }

  // Next in line is the new reading itself, and it is never protected.
  // So the new reading is the one that does not get in.
  return std::nullopt;
}

void LocationHistory::removeAt(std::size_t index) noexcept {
  // Close the gap: every later reading moves one slot towards the front.
  for (std::size_t slot = index; slot + 1 < count_; ++slot) {
    readings_[slot] = readings_[slot + 1];
  }
  --count_;

  // Wipe the slot that is now unused, so old coordinates do not linger in
  // memory after they were supposed to be forgotten.
  readings_[count_] = LocationReading{};
}

void LocationHistory::insertAt(std::size_t index, const LocationReading& reading) noexcept {
  // Open a gap: every reading from `index` on moves one slot towards the end.
  for (std::size_t slot = count_; slot > index; --slot) {
    readings_[slot] = readings_[slot - 1];
  }
  readings_[index] = reading;
  ++count_;
}

void LocationHistory::forgetOldReadings(std::int64_t nowMs) noexcept {
  std::size_t index = 0;
  while (index < count_) {
    if (isTooOldToKeep(readings_[index], nowMs)) {
      removeAt(index);  // the next reading slides into `index`, so do not advance
      ++stats_.removedForAge;
    } else {
      ++index;
    }
  }
}

void LocationHistory::clear() noexcept {
  for (std::size_t index = 0; index < count_; ++index) {
    readings_[index] = LocationReading{};
  }
  count_ = 0;
  // `nextArrivalNumber_` is deliberately not reset: a number handed out
  // before the clear must never be given to a different reading.
}

LocationHistory::StoreResult LocationHistory::store(
    const LocationReading& newReading,
    std::int64_t nowMs,
    std::optional<std::uint64_t> protectedArrivalNumber) noexcept {
  ++stats_.received;

  // Step 1: housekeeping. Drop what has become too old since the last call.
  forgetOldReadings(nowMs);

  // Step 2: a reading that arrives already older than 2 minutes is useless.
  if (isTooOldToKeep(newReading, nowMs)) {
    ++stats_.removedForAge;
    return StoreResult::IgnoredTooOld;
  }

  // Step 3: the system sometimes delivers the same reading twice. Storing it
  // twice would make one piece of evidence look like two.
  if (alreadyHave(newReading)) {
    ++stats_.duplicates;
    return StoreResult::IgnoredDuplicate;
  }

  // Step 4: find where it goes so the list stays sorted by capture time.
  std::size_t position = findPositionThatKeepsTimeOrder(newReading);
  const bool wasTakenBeforeSomethingWeAlreadyHave = position < count_;

  // Step 5: if the memory is full, someone has to leave.
  if (isFull()) {
    ++stats_.removedToMakeRoom;

    const std::optional<std::size_t> positionToRemove =
        chooseReadingToRemove(position, protectedArrivalNumber);

    const bool theNewReadingIsTheOneLeftOut = !positionToRemove.has_value();
    if (theNewReadingIsTheOneLeftOut) {
      return StoreResult::IgnoredOlderThanAllStored;
    }

    removeAt(*positionToRemove);

    // Removing a reading in front of our position shifts our position by one.
    if (*positionToRemove < position) {
      --position;
    }
  }

  // Step 6: store it, stamped with the next arrival number.
  LocationReading stored = newReading;
  stored.arrivalNumber = nextArrivalNumber_++;
  insertAt(position, stored);

  return wasTakenBeforeSomethingWeAlreadyHave ? StoreResult::StoredOutOfOrder
                                              : StoreResult::Stored;
}

}  // namespace lce
