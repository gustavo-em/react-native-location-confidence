// Reason codes: short, fixed labels that explain a result.
//
// Example: when there is no GPS permission, the answer carries the reason
// "permission_denied". Apps can show a message or decide what to do based
// on these labels, so their text must never change once published.
//
// A reason code never contains coordinates or error text from the system.
#ifndef LCE_REASON_CODES_HPP
#define LCE_REASON_CODES_HPP

#include <bitset>
#include <cstddef>
#include <cstdint>

namespace lce {

// Every reason the engine or the platform code can report.
// The order here must match the list of names in ReasonCodes.cpp.
enum class ReasonCode : std::uint8_t {
  // Permissions and device state
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

  // Time limits
  InitialFixTimeout,
  RecoveryWindowTimeout,
  OverallDeadlineReached,
  NativeProviderUnavailable,

  // Problems with a location reading
  LocationAccuracyUnavailable,
  LocationInvalidCoordinates,
  LocationInvalidTimestamp,
  LocationStale,
  LocationLowAccuracy,
  LocationAgeUnreliable,
  LocationFutureTimestamp,
  ClockDiscontinuity,

  // Problems with the set of readings
  HistoryEmpty,
  HistoryOnlyInconclusive,
  SamplesDuplicate,
  SamplesOutOfOrder,
  SuspectedGpsJump,
  PossibleMockLocation,
  NoUsableFix,

  // Request lifecycle
  RequestCancelled,
  SessionStopped,
  Busy,
  NativeInternalError,

  // Added after the spec review (finding R-12)
  UnknownSession,
  LocationInvalidAccuracy,
  LocationMetadataInconsistent,
  ProviderConflict,
  MotionConflictingEvidence,
  MonitoringDurationReached,
  UncertaintyModelHeuristic,

  Count  // not a reason: only tells how many reasons exist
};

inline constexpr std::size_t REASON_CODE_COUNT = static_cast<std::size_t>(ReasonCode::Count);

// The public text of a reason, for example "permission_denied".
// Returns nullptr if the value is not a real reason.
const char* reasonCodeName(ReasonCode code) noexcept;

// A small collection of reasons, like a checklist where each reason is
// either ticked or not. Adding the same reason twice changes nothing.
class ReasonSet {
 public:
  void add(ReasonCode code) noexcept { ticked_.set(positionOf(code)); }

  bool contains(ReasonCode code) const noexcept { return ticked_.test(positionOf(code)); }

  bool empty() const noexcept { return ticked_.none(); }

  // Ticks here everything that is ticked in `other`.
  void merge(const ReasonSet& other) noexcept { ticked_ |= other.ticked_; }

 private:
  static std::size_t positionOf(ReasonCode code) noexcept {
    return static_cast<std::size_t>(code);
  }

  std::bitset<REASON_CODE_COUNT> ticked_;
};

}  // namespace lce

#endif  // LCE_REASON_CODES_HPP
