/*
 * lce.h - the front door of the Location Confidence Engine ("lce").
 *
 * The engine is written in C++. The platform code that uses it is not:
 * Android uses Kotlin (through a small JNI file) and iOS uses Swift.
 * Both of them can call plain C functions easily, so the engine exposes a
 * set of C functions here and hides all of its C++ behind them.
 *
 * This file is a HEADER: it only announces which functions exist and what
 * they take and return. The code that runs is in cpp/src/CFacade.cpp, and
 * each function there only translates values and calls Engine.cpp.
 *
 * Rule for this file: plain C only. No C++ and no Objective-C, otherwise
 * Swift can no longer read it.
 *
 * HOW THE PLATFORM CODE USES IT
 *
 *   1. Create one engine when the module starts:   lce_engine_create()
 *
 *   2. Tell it what happens:
 *        the app opened a session        lce_open_session()
 *        the app asked for an evaluation lce_start_evaluation()
 *        the GPS delivered readings      lce_readings_arrived()
 *        the timer fired                 lce_time_passed()
 *        ...
 *
 *   3. After EVERY call of step 2, ask two things and obey:
 *        lce_take_finished_report()        again and again until it returns 0;
 *                                          hand each report to the app
 *        lce_what_the_platform_must_do_now()
 *                                          GPS on or off, activity updates
 *                                          on or off, when to call
 *                                          lce_time_passed() next
 *
 *   4. Destroy the engine when the module goes away: lce_engine_destroy()
 *
 * RULES THAT HOLD FOR EVERY FUNCTION
 *
 *   - Times are milliseconds on a monotonic clock (one that only moves
 *     forward and is not changed when the user changes the date). The
 *     platform chooses the clock and must always use the same one. The
 *     engine never reads a clock: `now_ms` always comes from the platform.
 *   - The engine never calls back into the platform and never starts a
 *     thread. It must be called from one thread at a time.
 *   - Nothing here can crash on a wrong value: a null pointer or a number
 *     that is not one of the listed choices gives an error code or is
 *     replaced by the cautious choice.
 *   - Yes/no values are int32_t: 0 means no, anything else means yes.
 */
#ifndef LOCATION_CONFIDENCE_LCE_H
#define LOCATION_CONFIDENCE_LCE_H

#include <stdint.h>

/* Tells a C++ compiler "these functions use C naming rules". Without this,
 * C++ would rename them internally and C or Swift code could not find them.
 * A C compiler skips these lines. */
#ifdef __cplusplus
extern "C" {
#endif

/* Version of this front door. Increase it whenever a function or a struct
 * here changes what it takes, what it returns or what it means. */
#define LCE_ABI_VERSION 2u

/* Returns the version above, as it was when the engine was compiled.
 *
 * Why it exists: the JavaScript package and the compiled engine are built
 * separately. If an app ends up with a new JavaScript and an old engine,
 * comparing versions lets us fail with a clear error instead of
 * misbehaving. It cannot fail. */
uint32_t lce_abi_version(void);

/* ---------------------------------------------------------------------------
 * What a function answers when it only reports success or failure
 * ------------------------------------------------------------------------- */

#define LCE_OK 0
/* A pointer that must point to something was null. Nothing was done. */
#define LCE_ERROR_NULL_ARGUMENT 1
/* The address or the radius of an evaluation makes no sense. Nothing was
 * started. This is a mistake of the calling code. */
#define LCE_ERROR_INVALID_REQUEST 2
/* The platform is not taking the finished reports. Nothing was started. */
#define LCE_ERROR_TOO_MANY_REPORTS_WAITING 3
/* There is no session with that number. */
#define LCE_ERROR_NO_SUCH_SESSION 4

/* ---------------------------------------------------------------------------
 * The choices (each group is one "enum")
 * ------------------------------------------------------------------------- */

/* The location permission the user gave to the app. */
#define LCE_PERMISSION_NOT_DETERMINED 0 /* the user was never asked */
#define LCE_PERMISSION_DENIED 1
#define LCE_PERMISSION_RESTRICTED 2 /* blocked by the device */
#define LCE_PERMISSION_GRANTED 3

/* Precise location, or only an approximate one. */
#define LCE_PRECISION_PRECISE 0
#define LCE_PRECISION_APPROXIMATE 1
#define LCE_PRECISION_UNKNOWN 2

/* Can the phone give locations at all? */
#define LCE_PROVIDER_STATE_AVAILABLE 0
#define LCE_PROVIDER_STATE_UNAVAILABLE 1
#define LCE_PROVIDER_STATE_PLAY_SERVICES_MISSING 2 /* Android only */

/* Can the phone report its activity (walking, standing still...)? */
#define LCE_ACTIVITY_SUPPORT_AVAILABLE 0
#define LCE_ACTIVITY_SUPPORT_NOT_SUPPORTED 1
#define LCE_ACTIVITY_SUPPORT_PERMISSION_DENIED 2
#define LCE_ACTIVITY_SUPPORT_PERMISSION_NOT_ASKED 3

/* What the phone is doing. */
#define LCE_ACTIVITY_STANDING_STILL 0
#define LCE_ACTIVITY_WALKING 1
#define LCE_ACTIVITY_RUNNING 2
#define LCE_ACTIVITY_CYCLING 3
#define LCE_ACTIVITY_IN_VEHICLE 4
#define LCE_ACTIVITY_UNKNOWN 5

/* How sure the phone is about its activity. */
#define LCE_CONFIDENCE_HIGH 0
#define LCE_CONFIDENCE_MEDIUM 1
#define LCE_CONFIDENCE_LOW 2
#define LCE_CONFIDENCE_UNKNOWN 3

/* Which system service produced a reading. */
#define LCE_READING_PROVIDER_FUSED 0         /* Android: Google Play services */
#define LCE_READING_PROVIDER_CORE_LOCATION 1 /* iOS */
#define LCE_READING_PROVIDER_SYSTEM_CACHE 2  /* an old reading the system had stored */

/* Did the system mark a reading as simulated? "Not flagged" proves nothing. */
#define LCE_MOCK_UNKNOWN 0
#define LCE_MOCK_NOT_FLAGGED 1
#define LCE_MOCK_FLAGGED 2

/* What the app wants for a reading marked as simulated. */
#define LCE_MOCK_POLICY_ZERO_SCORE 0
#define LCE_MOCK_POLICY_FLAG_ONLY 1

/* Is a session collecting readings? */
#define LCE_COLLECTING_NOT_REQUESTED 0
#define LCE_COLLECTING_ACTIVE 1
#define LCE_COLLECTING_BLOCKED 2 /* asked for, but it could not start */
#define LCE_COLLECTING_STOPPED 3

/* How good a report is as a whole. */
#define LCE_STATUS_AVAILABLE 0
#define LCE_STATUS_DEGRADED 1
#define LCE_STATUS_UNAVAILABLE 2
#define LCE_STATUS_CANCELLED 3

/* Where the reading of a report came from. */
#define LCE_SOURCE_FRESH 0
#define LCE_SOURCE_RECOVERED 1
#define LCE_SOURCE_CACHED 2
#define LCE_SOURCE_UNAVAILABLE 3

/* What the evidence says about the delivery radius. */
#define LCE_RELATION_SUPPORTS_INSIDE 0
#define LCE_RELATION_SUPPORTS_OUTSIDE 1
#define LCE_RELATION_INCONCLUSIVE 2

/* How much the age of the reading can be trusted. */
#define LCE_AGE_NOT_APPLICABLE 0 /* no reading was used */
#define LCE_AGE_MONOTONIC 1
#define LCE_AGE_ESTIMATED 2
#define LCE_AGE_UNRELIABLE 3

/* ---------------------------------------------------------------------------
 * The data that goes in
 * ------------------------------------------------------------------------- */

/* What the platform knows about the phone at one moment. */
typedef struct lce_device_state {
  int32_t location_permission;    /* LCE_PERMISSION_... */
  int32_t permission_precision;   /* LCE_PRECISION_... */
  int32_t location_is_turned_on;  /* the "Location" switch of the phone */
  int32_t location_provider;      /* LCE_PROVIDER_STATE_... */
  int32_t app_is_in_foreground;
  int32_t phone_activity_support; /* LCE_ACTIVITY_SUPPORT_... */
} lce_device_state;

/* What the app chose when it opened a session. */
typedef struct lce_session_settings {
  /* 0: the session only remembers the readings of its evaluations.
   * 1: the session keeps collecting readings while it is open. */
  int32_t keep_collecting_readings;
  int64_t time_between_readings_ms;   /* only used when collecting */
  int64_t longest_collecting_time_ms; /* only used when collecting */
  lce_device_state device;
} lce_session_settings;

/* What the app asked for in one evaluation. */
typedef struct lce_evaluation_settings {
  /* The address, and how close to it counts as "there". */
  double destination_latitude;
  double destination_longitude;
  double radius_meters;

  int64_t newest_age_that_counts_as_fresh_ms; /* usually 10000 */
  int64_t oldest_age_allowed_ms;              /* usually 60000 */
  double worst_gps_error_accepted_meters;     /* usually 50 */
  double fastest_possible_speed_mps;          /* usually 45 */
  int32_t use_phone_activity;
  int32_t allow_answer_from_history;
  int32_t mock_policy;                        /* LCE_MOCK_POLICY_... */

  int64_t total_time_allowed_ms;  /* usually 12000 */
  int64_t first_attempt_time_ms;  /* usually 4500 */
  int64_t extra_time_ms;          /* usually 7000; 0 means no extra time */

  lce_device_state device;
} lce_evaluation_settings;

/* One reading, exactly as the GPS delivered it. Nothing in it is trusted
 * yet: the engine checks every field. */
typedef struct lce_reading {
  double latitude;
  double longitude;

  int32_t has_accuracy;
  double accuracy_meters;

  /* When the reading was taken, on the monotonic clock. */
  int64_t captured_at_ms;
  /* 0 when the platform could not work that time out safely. */
  int32_t time_is_reliable;
  /* 1 when that time was worked out from the wall clock (iOS). */
  int32_t capture_time_was_estimated;

  int32_t only_approximate_permission;

  int32_t has_speed;
  double speed_mps;
  int32_t has_speed_accuracy;
  double speed_accuracy_mps;
  int32_t has_heading;
  double heading_degrees;

  int32_t provider;    /* LCE_READING_PROVIDER_... */
  int32_t mock_signal; /* LCE_MOCK_... */
} lce_reading;

/* ---------------------------------------------------------------------------
 * The data that comes out
 * ------------------------------------------------------------------------- */

/* The answer to "open a session". */
typedef struct lce_session_opened {
  uint32_t session_id; /* 0: the session was NOT opened */
  int32_t collecting;  /* LCE_COLLECTING_... */
  /* Why the collecting is blocked, or why the session was not opened.
   * One bit for each reason: bit N is set when reason number N applies.
   * lce_reason_name(N) gives its text. */
  uint64_t reasons;
} lce_session_opened;

/* What the platform must do now. It describes how things must BE, so the
 * same answer twice in a row means "change nothing". */
typedef struct lce_platform_work {
  int32_t location_updates_are_wanted;
  int64_t time_between_location_updates_ms;
  int32_t phone_activity_updates_are_wanted;
  int32_t wake_up_is_wanted;
  int64_t wake_up_at_ms; /* when to call lce_time_passed() */
} lce_platform_work;

/* A report can list at most this many steps. */
#define LCE_MAX_STEPS_IN_REPORT 8

/* The final report of one evaluation. */
typedef struct lce_report {
  uint32_t request_id;
  uint32_t session_id; /* 0: the evaluation had no session */

  int32_t status; /* LCE_STATUS_... */
  int32_t source; /* LCE_SOURCE_... */

  /* The reading that was used. When has_position is 0 there is none and the
   * seven values after it mean nothing. */
  int32_t has_position;
  double latitude;
  double longitude;
  double distance_to_destination_meters;
  double gps_error_meters;
  double how_far_phone_could_be_meters;
  int64_t reading_age_ms;
  int32_t mock_signal; /* LCE_MOCK_... */

  double radius_meters;
  int32_t age_reliability;      /* LCE_AGE_... */
  int32_t permission_precision; /* LCE_PRECISION_... */
  int32_t relation;             /* LCE_RELATION_... */

  /* The quality score: an index from 0 to 100, NOT a probability. */
  int32_t quality_score;
  /* When has_score_breakdown is 0 there was nothing to score and the five
   * values after it mean nothing. */
  int32_t has_score_breakdown;
  double accuracy_factor;
  double freshness_factor;
  double movement_factor;
  double consistency_factor;
  int32_t raw_score; /* before the limits */
  /* The limits that applied. Bit N is set when limit number N applied;
   * lce_score_limit_name(N) gives its text. */
  uint32_t limits_applied;

  /* The phone activity. */
  int32_t activity_now;            /* LCE_ACTIVITY_... */
  int32_t activity_confidence_now; /* LCE_CONFIDENCE_... */
  int32_t standing_still_since_the_reading;
  int32_t activity_known_until_now;
  int64_t activity_known_since_age_ms; /* only when the value above is 1 */
  int32_t activity_has_gaps;

  /* Why the report says what it says. One bit for each reason. */
  uint64_t reasons;

  /* The steps the evaluation went through; lce_step_name() gives the text. */
  int32_t steps[LCE_MAX_STEPS_IN_REPORT];
  int32_t how_many_steps;

  int32_t first_attempt_timed_out;
  int32_t extra_time_was_used;
  /* Why the engine had to go beyond the first attempt: a reason number, or
   * -1 when it did not. */
  int32_t fallback_cause;

  uint32_t readings_received; /* delivered by the GPS during the evaluation */
  uint32_t readings_kept;     /* of those, how many were stored */
  uint32_t readings_examined; /* readings in memory when the report was built */
  uint32_t gps_jumps_found;
  int64_t duration_ms;
} lce_report;

/* ---------------------------------------------------------------------------
 * The engine itself
 * ------------------------------------------------------------------------- */

/* The platform only holds a pointer to the engine and never looks inside. */
typedef struct lce_engine lce_engine;

/* Creates an engine. Returns NULL when there is no memory for it. */
lce_engine* lce_engine_create(void);

/* Destroys an engine and wipes what it remembered. Passing NULL does nothing.
 * The pointer must not be used afterwards. */
void lce_engine_destroy(lce_engine* engine);

/* --- Sessions --- */

/* Opens a session. Returns LCE_OK also when the session could not be opened
 * for lack of room: then `opened->session_id` is 0 and the reasons say why. */
int32_t lce_open_session(lce_engine* engine,
                         const lce_session_settings* settings,
                         int64_t now_ms,
                         lce_session_opened* opened);

/* Forgets the readings of a session; the session stays open. */
int32_t lce_clear_session_history(lce_engine* engine, uint32_t session_id);

/* Closes a session: its evaluations stop and its readings are forgotten.
 * Closing a session that does not exist is not an error. */
int32_t lce_close_session(lce_engine* engine, uint32_t session_id, int64_t now_ms);

/* --- Evaluations --- */

/* Starts an evaluation. `session_id` 0 means "no session".
 * On LCE_OK, `*request_id` identifies the evaluation and its report will
 * come out of lce_take_finished_report(), maybe right away. */
int32_t lce_start_evaluation(lce_engine* engine,
                             uint32_t session_id,
                             const lce_evaluation_settings* settings,
                             int64_t now_ms,
                             uint32_t* request_id);

/* Cancels an evaluation. Cancelling one that already ended does nothing. */
int32_t lce_cancel_evaluation(lce_engine* engine, uint32_t request_id, int64_t now_ms);

/* --- Things that happen --- */

/* The GPS delivered `how_many` readings. */
int32_t lce_readings_arrived(lce_engine* engine,
                             const lce_reading* readings,
                             uint32_t how_many,
                             int64_t now_ms);

/* "Since `since_ms` the phone is doing this." */
int32_t lce_phone_activity_changed(lce_engine* engine,
                                   int32_t activity,   /* LCE_ACTIVITY_... */
                                   int32_t confidence, /* LCE_CONFIDENCE_... */
                                   int64_t since_ms,
                                   int64_t now_ms);

/* The phone stopped reporting its activity. */
int32_t lce_phone_activity_updates_stopped(lce_engine* engine, int64_t now_ms);

/* Time passed. Call it at the moment lce_what_the_platform_must_do_now()
 * asked for. Calling it earlier or more often is harmless. */
int32_t lce_time_passed(lce_engine* engine, int64_t now_ms);

int32_t lce_app_went_to_background(lce_engine* engine, int64_t now_ms);
int32_t lce_location_permission_was_lost(lce_engine* engine, int64_t now_ms);
int32_t lce_location_provider_failed(lce_engine* engine, int64_t now_ms);

/* 1: the app replaced the GPS with readings of its own (a demo, a test).
 * Every report then carries the reason "simulated_location_source". */
int32_t lce_set_readings_are_simulated(lce_engine* engine, int32_t readings_are_simulated);

/* --- What the platform asks after every call --- */

int32_t lce_what_the_platform_must_do_now(const lce_engine* engine, lce_platform_work* work);

/* Returns 1 and fills `report` when a finished report was waiting.
 * Returns 0 when none was (also when a pointer is null). */
int32_t lce_take_finished_report(lce_engine* engine, lce_report* report);

/* --- Texts ---
 * The numbers used in the reports have a fixed public text. The texts live
 * in the engine so that Android and iOS always show exactly the same ones.
 * Each function returns NULL for a number that is not in the list. The
 * returned text is never freed and never changes. */

uint32_t lce_reason_count(void);
const char* lce_reason_name(int32_t reason); /* for example "permission_denied" */

uint32_t lce_score_limit_count(void);
const char* lce_score_limit_name(int32_t limit); /* for example "single_fix" */

uint32_t lce_step_count(void);
const char* lce_step_name(int32_t step); /* for example "first_attempt" */

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* LOCATION_CONFIDENCE_LCE_H */
