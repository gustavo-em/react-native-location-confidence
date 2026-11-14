// Core value types shared by every module of the Location Confidence core.
//
// Pure C++17: no platform SDK, no React Native, no clocks. All times are
// monotonic milliseconds supplied by the platform adapter.
#ifndef LCE_TYPES_HPP
#define LCE_TYPES_HPP

#include <cstddef>
#include <cstdint>
#include <optional>

namespace lce {

// --- Model constants --------------------------------------------------------
// Numeric thresholds are initial design hypotheses, not statistical guarantees.

inline constexpr std::size_t kHistoryCapacity = 10;
inline constexpr std::int64_t kHistoryRetentionMs = 120000;

// A fix whose capture time is at most this far ahead of "now" is treated as
// clock jitter and clamped to age 0. Beyond it the time base is not trusted.
inline constexpr std::int64_t kFutureTimestampToleranceMs = 1000;

inline constexpr double kMaxRadiusMeters = 100000.0;

inline constexpr const char* kScoreModelVersion = "heuristic-v1";

// --- Geometry ---------------------------------------------------------------

struct Coordinates {
  double latitudeDeg = 0.0;   // WGS-84, [-90, 90]
  double longitudeDeg = 0.0;  // WGS-84, [-180, 180]
};

// --- Sample metadata --------------------------------------------------------

enum class Provider : std::uint8_t { Fused, CoreLocation, SystemCache };

// What the operating system reported about simulation. NotReported is not a
// statement that the location is authentic.
enum class MockFlag : std::uint8_t { Unknown, NotReported, Reported };

// Which part of the flow obtained the fix. Used to label the result source.
enum class AcquisitionPhase : std::uint8_t { Monitoring, Initial, Recovery };

enum class FixSource : std::uint8_t { Fresh, Recovered, Cached, Unavailable };

enum class MockPolicy : std::uint8_t { ZeroScore, FlagOnly };

// A syntactically valid, normalized location fix. Only measured fixes exist:
// the core never synthesizes coordinates.
struct LocationSample {
  Coordinates position;
  double horizontalAccuracyMeters = 0.0;

  // Capture time on the process-wide monotonic clock of the platform.
  std::int64_t fixTimeMonotonicMs = 0;

  bool approximatePermission = false;

  // Absent means "not reported", never zero.
  std::optional<double> speedMps;
  std::optional<double> speedAccuracyMps;
  std::optional<double> courseDeg;  // [0, 360)

  Provider provider = Provider::Fused;
  MockFlag mockFlag = MockFlag::Unknown;

  // Provenance: request that acquired the fix (0 = none) and in which phase.
  std::uint32_t acquiredByRequest = 0;
  AcquisitionPhase phase = AcquisitionPhase::Monitoring;

  // Assigned by LocationHistory on insertion. Only used as a stable tie-break;
  // it is not an identity for deduplication.
  std::uint64_t sequence = 0;
};

// Age of a sample at `nowMonotonicMs`, never negative.
inline std::int64_t ageMs(const LocationSample& sample, std::int64_t nowMonotonicMs) noexcept {
  const std::int64_t age = nowMonotonicMs - sample.fixTimeMonotonicMs;
  return age > 0 ? age : 0;
}

}  // namespace lce

#endif  // LCE_TYPES_HPP
