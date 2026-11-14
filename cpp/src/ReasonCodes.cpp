#include "ReasonCodes.hpp"

namespace lce {

namespace {

// Public text of each reason, in exactly the same order as the ReasonCode
// list in ReasonCodes.hpp. Apps depend on these texts: never rename one.
constexpr const char* REASON_NAMES[] = {
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

// The build fails here if someone adds a reason to the list in the header
// and forgets to add its text above (or the other way around).
static_assert(sizeof(REASON_NAMES) / sizeof(REASON_NAMES[0]) == REASON_CODE_COUNT,
              "the list of reason names does not match the ReasonCode list");

}  // namespace

const char* reasonCodeName(ReasonCode code) noexcept {
  const std::size_t position = static_cast<std::size_t>(code);
  const bool isARealReason = position < REASON_CODE_COUNT;
  return isARealReason ? REASON_NAMES[position] : nullptr;
}

}  // namespace lce
