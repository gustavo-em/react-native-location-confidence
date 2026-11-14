// Stable public reason codes. The snake_case names are the single source of
// truth for both platforms; they never contain coordinates or OS error text.
#ifndef LCE_REASON_CODES_HPP
#define LCE_REASON_CODES_HPP

#include <cstddef>
#include <cstdint>

namespace lce {

enum class ReasonCode : std::uint8_t {
  PermissionNotDetermined = 0,
  PermissionDenied,
  PermissionRestricted,
  PrecisionReduced,
  LocationServicesDisabled,
  MotionPermissionDenied,
  MotionUnavailable,
  MotionInsufficientCoverage,
  MotionDelayed,
  PlayServicesUnavailable,
  PlatformUnsupported,
  AppBackgrounded,
  ForegroundRequired,
  InitialFixTimeout,
  RecoveryWindowTimeout,
  OverallDeadlineReached,
  NativeProviderUnavailable,
  LocationAccuracyUnavailable,
  LocationInvalidCoordinates,
  LocationInvalidTimestamp,
  LocationStale,
  LocationLowAccuracy,
  LocationAgeUnreliable,
  LocationFutureTimestamp,
  ClockDiscontinuity,
  HistoryEmpty,
  HistoryOnlyInconclusive,
  SamplesDuplicate,
  SamplesOutOfOrder,
  SuspectedGpsJump,
  PossibleMockLocation,
  NoUsableFix,
  RequestCancelled,
  SessionStopped,
  Busy,
  NativeInternalError,
  // Added by the Phase 0 spec review (R-12).
  UnknownSession,
  LocationInvalidAccuracy,
  LocationMetadataInconsistent,
  ProviderConflict,
  MotionConflictingEvidence,
  MonitoringDurationReached,
  UncertaintyModelHeuristic,
  Count
};

inline constexpr std::size_t kReasonCodeCount = static_cast<std::size_t>(ReasonCode::Count);

// Returns the stable snake_case name, or nullptr for an out-of-range value.
const char* reasonCodeName(ReasonCode code) noexcept;

// Small value-type set of reason codes.
class ReasonSet {
 public:
  void add(ReasonCode code) noexcept { bits_ |= bit(code); }
  bool contains(ReasonCode code) const noexcept { return (bits_ & bit(code)) != 0; }
  bool empty() const noexcept { return bits_ == 0; }
  void merge(const ReasonSet& other) noexcept { bits_ |= other.bits_; }
  std::uint64_t bits() const noexcept { return bits_; }

 private:
  static std::uint64_t bit(ReasonCode code) noexcept {
    return std::uint64_t{1} << static_cast<unsigned>(code);
  }

  std::uint64_t bits_ = 0;
};

static_assert(kReasonCodeCount <= 64, "ReasonSet stores codes in a 64-bit mask");

}  // namespace lce

#endif  // LCE_REASON_CODES_HPP
