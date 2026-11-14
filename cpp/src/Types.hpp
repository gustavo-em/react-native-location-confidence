// The basic vocabulary of the engine.
//
// This file only describes data: what a "location reading" is and which
// fixed numbers the rules use. It contains no logic.
//
// The engine never talks to the GPS, to the clock or to React Native. The
// platform code (Kotlin on Android, Swift on iOS) hands it readings and the
// current time, and the engine answers questions about them.
#ifndef LCE_TYPES_HPP
#define LCE_TYPES_HPP

#include <cstddef>
#include <cstdint>
#include <optional>

namespace lce {

// --- Fixed numbers used by the rules -----------------------------------------
// These are starting guesses to be tuned with real data. They are not
// scientific facts and not guarantees.

// The history remembers at most this many location readings.
inline constexpr std::size_t MAX_STORED_LOCATIONS = 10;

// A reading older than 2 minutes is forgotten. After that long we cannot say
// anything useful about where the phone is now.
inline constexpr std::int64_t MAX_AGE_IN_HISTORY_MS = 120000;

// Sometimes a reading claims to have been taken slightly in the future,
// because two clocks inside the phone disagree by a few milliseconds.
// Up to 1 second we treat it as "taken right now". More than that means the
// clock cannot be trusted and the reading is thrown away.
inline constexpr std::int64_t CLOCK_JITTER_TOLERANCE_MS = 1000;

// Largest delivery radius the library accepts (100 km).
inline constexpr double MAX_RADIUS_METERS = 100000.0;

// Name of the current set of scoring rules. If the rules change in a way that
// changes results, this name must change too.
inline constexpr const char* SCORE_MODEL_VERSION = "heuristic-v1";

// --- A point on the map --------------------------------------------------------

struct Coordinates {
  double latitudeDegrees = 0.0;   // -90 (south pole) to +90 (north pole)
  double longitudeDegrees = 0.0;  // -180 to +180
};

// --- Extra information that comes with a reading ---------------------------------

// Which system service produced the reading.
enum class Provider : std::uint8_t {
  Fused,         // Android: Google Play services location
  CoreLocation,  // iOS
  SystemCache,   // an old reading the operating system had stored
};

// Did the operating system tell us this location is simulated (a "mock")?
// Important: "NotFlaggedByOs" does NOT prove the location is real. Some
// spoofing tools are invisible to the operating system.
enum class MockSignal : std::uint8_t {
  Unknown,         // the operating system gave no information
  NotFlaggedByOs,  // the operating system did not mark it as simulated
  FlaggedByOs,     // the operating system marked it as simulated
};

// What the app wants to happen when a reading is flagged as simulated.
enum class MockPolicy : std::uint8_t {
  ZeroScore,  // the quality score becomes 0
  FlagOnly,   // only report it; the score is not changed
};

// At which moment of the flow the reading was obtained.
enum class AcquisitionPhase : std::uint8_t {
  Monitoring,      // collected in the background of an open session
  InitialAttempt,  // the first attempt of an evaluation
  RecoveryWindow,  // the extra time given when the first attempt was not enough
};

// Where the reading used in the final answer came from.
enum class FixSource : std::uint8_t {
  Fresh,        // obtained now, in the first attempt
  Recovered,    // obtained now, during the recovery window
  Cached,       // taken from the history (an older reading)
  Unavailable,  // there was no usable reading
};

// --- One location reading -----------------------------------------------------------
//
// A "reading" (also called a "fix") is one answer from the GPS: a position,
// how precise the phone thinks it is, and when it was taken.
//
// Every reading here was really measured by the phone. The engine never
// invents or predicts a position.
struct LocationReading {
  Coordinates position;

  // The phone's own estimate of its error: "the real position is probably
  // within this many meters of `position`". Smaller is better.
  double horizontalAccuracyMeters = 0.0;

  // When the reading was taken, in milliseconds on the phone's monotonic
  // clock. A monotonic clock only moves forward and is not affected when the
  // user changes the date or time, so it is safe for measuring age.
  std::int64_t capturedAtMs = 0;

  // True when the user only allowed approximate location. The position may
  // then be off by kilometers, no matter what the accuracy field says.
  bool onlyApproximatePermission = false;

  // Optional extras. "Empty" means the phone did not report the value.
  // Empty is never the same as zero: an unknown speed is not "standing still".
  std::optional<double> speedMetersPerSecond;
  std::optional<double> speedAccuracyMetersPerSecond;
  std::optional<double> headingDegrees;  // direction of travel, 0 up to (not including) 360

  Provider provider = Provider::Fused;
  MockSignal mockSignal = MockSignal::Unknown;

  // Which evaluation request obtained this reading (0 = none) and when.
  std::uint32_t obtainedByRequestId = 0;
  AcquisitionPhase obtainedDuring = AcquisitionPhase::Monitoring;

  // A counter given by the history when the reading is stored: 1, 2, 3...
  // It only says "this one arrived before that one". Two readings with
  // different numbers can still be the same reading delivered twice.
  std::uint64_t arrivalNumber = 0;
};

// How old is this reading right now? Never negative.
inline std::int64_t ageInMs(const LocationReading& reading, std::int64_t nowMs) noexcept {
  const std::int64_t age = nowMs - reading.capturedAtMs;
  return age > 0 ? age : 0;
}

}  // namespace lce

#endif  // LCE_TYPES_HPP
