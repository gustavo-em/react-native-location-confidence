#include "ReasonCodes.hpp"

namespace lce {

namespace {

// Order must match the ReasonCode enum exactly.
constexpr const char* kNames[] = {
    "permission_not_determined",
    "permission_denied",
    "permission_restricted",
    "precision_reduced",
    "location_services_disabled",
    "motion_permission_denied",
    "motion_unavailable",
    "motion_insufficient_coverage",
    "motion_delayed",
    "play_services_unavailable",
    "platform_unsupported",
    "app_backgrounded",
    "foreground_required",
    "initial_fix_timeout",
    "recovery_window_timeout",
    "overall_deadline_reached",
    "native_provider_unavailable",
    "location_accuracy_unavailable",
    "location_invalid_coordinates",
    "location_invalid_timestamp",
    "location_stale",
    "location_low_accuracy",
    "location_age_unreliable",
    "location_future_timestamp",
    "clock_discontinuity",
    "history_empty",
    "history_only_inconclusive",
    "samples_duplicate",
    "samples_out_of_order",
    "suspected_gps_jump",
    "possible_mock_location",
    "no_usable_fix",
    "request_cancelled",
    "session_stopped",
    "busy",
    "native_internal_error",
    "unknown_session",
    "location_invalid_accuracy",
    "location_metadata_inconsistent",
    "provider_conflict",
    "motion_conflicting_evidence",
    "monitoring_duration_reached",
    "uncertainty_model_heuristic",
};

static_assert(sizeof(kNames) / sizeof(kNames[0]) == kReasonCodeCount,
              "reason code name table is out of sync with the enum");

}  // namespace

const char* reasonCodeName(ReasonCode code) noexcept {
  const auto index = static_cast<std::size_t>(code);
  return index < kReasonCodeCount ? kNames[index] : nullptr;
}

}  // namespace lce
