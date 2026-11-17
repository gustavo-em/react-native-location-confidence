// Tests of the front door (lce.h): the plain C functions that Kotlin and
// Swift call. Everything here uses only what lce.h announces, exactly as the
// platform code does.
//
// The address is the starting point and the delivery radius is 100 meters.
#include "third_party/doctest.h"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

#include "location_confidence/lce.h"

namespace {

// Creates an engine and destroys it when the test ends.
struct EngineForTest {
  lce_engine* engine = lce_engine_create();
  ~EngineForTest() { lce_engine_destroy(engine); }
};

lce_device_state deviceWhereEverythingWorks() {
  lce_device_state device{};
  device.location_permission = LCE_PERMISSION_GRANTED;
  device.permission_precision = LCE_PRECISION_PRECISE;
  device.location_is_turned_on = 1;
  device.location_provider = LCE_PROVIDER_STATE_AVAILABLE;
  device.app_is_in_foreground = 1;
  device.phone_activity_support = LCE_ACTIVITY_SUPPORT_AVAILABLE;
  return device;
}

// The usual limits, written out the way the JavaScript side fills them.
lce_request_settings usualRequest() {
  lce_request_settings settings{};
  settings.destination_latitude = 0.0;
  settings.destination_longitude = 0.0;
  settings.radius_meters = 100.0;
  settings.newest_age_that_counts_as_fresh_ms = 10000;
  settings.oldest_age_allowed_ms = 60000;
  settings.worst_gps_error_accepted_meters = 50.0;
  settings.fastest_possible_speed_mps = 45.0;
  settings.use_phone_activity = 1;
  settings.allow_answer_from_history = 1;
  settings.mock_policy = LCE_MOCK_POLICY_ZERO_SCORE;
  settings.total_time_allowed_ms = 12000;
  settings.first_attempt_time_ms = 4500;
  settings.extra_time_ms = 7000;
  settings.device = deviceWhereEverythingWorks();
  return settings;
}

lce_session_settings sessionThatKeepsCollecting() {
  lce_session_settings settings{};
  settings.keep_collecting_readings = 1;
  settings.time_between_readings_ms = 5000;
  settings.longest_collecting_time_ms = 900000;
  settings.device = deviceWhereEverythingWorks();
  return settings;
}

// A reading some meters north of the address, taken at `capturedAtMs`.
lce_reading readingNorth(int64_t capturedAtMs, double metersNorth, double gpsErrorMeters) {
  const double degreesPerMeter = 180.0 / (3.14159265358979323846 * 6371008.8);

  lce_reading reading{};
  reading.latitude = metersNorth * degreesPerMeter;
  reading.longitude = 0.0;
  reading.has_accuracy = 1;
  reading.accuracy_meters = gpsErrorMeters;
  reading.captured_at_ms = capturedAtMs;
  reading.time_is_reliable = 1;
  reading.provider = LCE_READING_PROVIDER_FUSED;
  reading.mock_signal = LCE_MOCK_NOT_FLAGGED;
  return reading;
}

// Takes every report that is waiting.
std::vector<lce_report> takeEveryReport(lce_engine* engine) {
  std::vector<lce_report> reports;
  lce_report report;
  std::memset(&report, 0, sizeof(report));
  while (lce_take_finished_report(engine, &report) == 1) {
    reports.push_back(report);
    std::memset(&report, 0, sizeof(report));
  }
  return reports;
}

// The number of a reason, found by its public text. -1 when there is none.
int32_t reasonNumber(const std::string& text) {
  for (int32_t number = 0; number < static_cast<int32_t>(lce_reason_count()); ++number) {
    if (text == lce_reason_name(number)) {
      return number;
    }
  }
  return -1;
}

bool hasReason(uint64_t reasons, const std::string& text) {
  const int32_t number = reasonNumber(text);
  REQUIRE(number >= 0);
  return (reasons & (uint64_t{1} << number)) != 0;
}

std::vector<std::string> stepsOf(const lce_report& report) {
  std::vector<std::string> steps;
  for (int32_t position = 0; position < report.how_many_steps; ++position) {
    steps.push_back(lce_step_name(report.steps[position]));
  }
  return steps;
}

}  // namespace

TEST_CASE("facade: ABI version matches the header") {
  CHECK(lce_abi_version() == LCE_ABI_VERSION);
  CHECK(lce_abi_version() == 2u);
}

// --- The scenario the Android and iOS tests repeat -------------------------------------
// The same script, with the same expected numbers, runs through JNI in the
// Android tests and through Swift in the iOS tests. If the three agree, the
// values cross each bridge without being changed.

TEST_CASE("golden scenario: one request, one reading, every field of the report") {
  EngineForTest test;
  REQUIRE(test.engine != nullptr);

  // second 0: the app asks.
  const lce_request_settings settings = usualRequest();
  uint32_t requestId = 0;
  REQUIRE(lce_start_request(test.engine, 0, &settings, 0, &requestId) == LCE_OK);
  CHECK(requestId == 1);

  lce_platform_work work{};
  REQUIRE(lce_what_the_platform_must_do_now(test.engine, &work) == LCE_OK);
  CHECK(work.location_updates_are_wanted == 1);
  CHECK(work.time_between_location_updates_ms == 1000);
  CHECK(work.phone_activity_updates_are_wanted == 1);
  CHECK(work.wake_up_is_wanted == 1);
  CHECK(work.wake_up_at_ms == 4500);
  CHECK(takeEveryReport(test.engine).empty());

  // second 2: the GPS delivers a reading taken at second 1, 20 meters from
  // the address, with a GPS error of 8 meters.
  const lce_reading reading = readingNorth(1000, 20.0, 8.0);
  REQUIRE(lce_readings_arrived(test.engine, &reading, 1, 2000) == LCE_OK);

  const std::vector<lce_report> reports = takeEveryReport(test.engine);
  REQUIRE(reports.size() == 1);
  const lce_report& report = reports[0];

  CHECK(report.request_id == 1);
  CHECK(report.session_id == 0);
  CHECK(report.status == LCE_STATUS_AVAILABLE);
  CHECK(report.source == LCE_SOURCE_FRESH);

  CHECK(report.has_position == 1);
  CHECK(report.latitude == reading.latitude);
  CHECK(report.longitude == 0.0);
  CHECK(report.distance_to_destination_meters == doctest::Approx(20.0).epsilon(1e-9));
  CHECK(report.gps_error_meters == 8.0);
  CHECK(report.how_far_phone_could_be_meters == 53.0);  // 8 + 45 x 1 second
  CHECK(report.reading_age_ms == 1000);
  CHECK(report.mock_signal == LCE_MOCK_NOT_FLAGGED);

  CHECK(report.radius_meters == 100.0);
  CHECK(report.age_reliability == LCE_AGE_MONOTONIC);
  CHECK(report.permission_precision == LCE_PRECISION_PRECISE);
  CHECK(report.relation == LCE_RELATION_SUPPORTS_INSIDE);  // 20 + 53 = 73

  // accuracy 0.92, freshness 59/60, movement 0.55, one reading 0.60:
  // 36.8 + 24.58 + 11 + 9 = 81.38 -> 81, then the single reading limit.
  CHECK(report.quality_score == 75);
  CHECK(report.has_score_breakdown == 1);
  CHECK(report.accuracy_factor == doctest::Approx(0.92).epsilon(1e-12));
  CHECK(report.freshness_factor == doctest::Approx(59.0 / 60.0).epsilon(1e-12));
  CHECK(report.movement_factor == doctest::Approx(0.55).epsilon(1e-12));
  CHECK(report.consistency_factor == doctest::Approx(0.60).epsilon(1e-12));
  CHECK(report.raw_score == 81);
  CHECK(report.limits_applied == (1u << 5));  // limit number 5
  CHECK(std::string(lce_score_limit_name(5)) == "single_fix");

  CHECK(report.activity_now == LCE_ACTIVITY_UNKNOWN);
  CHECK(report.activity_confidence_now == LCE_CONFIDENCE_UNKNOWN);
  CHECK(report.standing_still_since_the_reading == 0);
  CHECK(report.activity_known_until_now == 0);
  CHECK(report.activity_known_since_age_ms == 0);
  CHECK(report.activity_has_gaps == 0);

  CHECK(report.reasons == (uint64_t{1} << reasonNumber("uncertainty_model_heuristic")));

  CHECK(stepsOf(report) == std::vector<std::string>{"started", "first_attempt", "finished"});
  CHECK(report.first_attempt_timed_out == 0);
  CHECK(report.extra_time_was_used == 0);
  CHECK(report.fallback_cause == -1);

  CHECK(report.readings_received == 1);
  CHECK(report.readings_kept == 1);
  CHECK(report.readings_examined == 1);
  CHECK(report.gps_jumps_found == 0);
  CHECK(report.duration_ms == 2000);

  // Nothing is wanted anymore.
  REQUIRE(lce_what_the_platform_must_do_now(test.engine, &work) == LCE_OK);
  CHECK(work.location_updates_are_wanted == 0);
  CHECK(work.phone_activity_updates_are_wanted == 0);
  CHECK(work.wake_up_is_wanted == 0);
}

// --- Requests that end in other ways --------------------------------------------------

TEST_CASE("facade: the timer alone ends a request that gets no reading") {
  EngineForTest test;
  const lce_request_settings settings = usualRequest();
  uint32_t requestId = 0;
  REQUIRE(lce_start_request(test.engine, 0, &settings, 1000, &requestId) == LCE_OK);

  lce_platform_work work{};
  lce_what_the_platform_must_do_now(test.engine, &work);
  CHECK(work.wake_up_at_ms == 5500);

  REQUIRE(lce_time_passed(test.engine, 5500) == LCE_OK);
  CHECK(takeEveryReport(test.engine).empty());
  lce_what_the_platform_must_do_now(test.engine, &work);
  CHECK(work.wake_up_at_ms == 12500);

  REQUIRE(lce_time_passed(test.engine, 12500) == LCE_OK);
  const std::vector<lce_report> reports = takeEveryReport(test.engine);
  REQUIRE(reports.size() == 1);

  const lce_report& report = reports[0];
  CHECK(report.status == LCE_STATUS_UNAVAILABLE);
  CHECK(report.source == LCE_SOURCE_UNAVAILABLE);
  CHECK(report.has_position == 0);
  CHECK(report.latitude == 0.0);
  CHECK(report.quality_score == 0);
  CHECK(report.has_score_breakdown == 0);
  CHECK(report.relation == LCE_RELATION_INCONCLUSIVE);
  CHECK(report.age_reliability == LCE_AGE_NOT_APPLICABLE);
  CHECK(report.first_attempt_timed_out == 1);
  CHECK(report.extra_time_was_used == 1);
  CHECK(report.fallback_cause == reasonNumber("initial_fix_timeout"));
  CHECK(report.duration_ms == 11500);
  CHECK(hasReason(report.reasons, "initial_fix_timeout"));
  CHECK(hasReason(report.reasons, "recovery_window_timeout"));
  CHECK(hasReason(report.reasons, "no_usable_fix"));
  CHECK(hasReason(report.reasons, "history_empty"));
  CHECK(stepsOf(report) == std::vector<std::string>{"started", "first_attempt", "extra_time",
                                                    "answer_from_history", "finished"});
  // The limit "no_candidate" is the one that made the score 0.
  CHECK(std::string(lce_score_limit_name(10)) == "no_candidate");
  CHECK(report.limits_applied == (1u << 10));
}

TEST_CASE("facade: cancel, background, permission lost and a GPS failure") {
  const lce_request_settings settings = usualRequest();

  SUBCASE("cancel") {
    EngineForTest test;
    uint32_t requestId = 0;
    lce_start_request(test.engine, 0, &settings, 0, &requestId);
    REQUIRE(lce_cancel_request(test.engine, requestId, 800) == LCE_OK);
    REQUIRE(lce_cancel_request(test.engine, requestId, 900) == LCE_OK);     // twice

    const std::vector<lce_report> reports = takeEveryReport(test.engine);
    REQUIRE(reports.size() == 1);
    CHECK(reports[0].status == LCE_STATUS_CANCELLED);
    CHECK(reports[0].duration_ms == 800);
    CHECK(hasReason(reports[0].reasons, "request_cancelled"));
  }

  SUBCASE("the app goes to the background") {
    EngineForTest test;
    uint32_t requestId = 0;
    lce_start_request(test.engine, 0, &settings, 0, &requestId);
    REQUIRE(lce_app_went_to_background(test.engine, 500) == LCE_OK);

    const std::vector<lce_report> reports = takeEveryReport(test.engine);
    REQUIRE(reports.size() == 1);
    CHECK(reports[0].status == LCE_STATUS_CANCELLED);
    CHECK(hasReason(reports[0].reasons, "app_backgrounded"));
  }

  SUBCASE("the location permission is lost") {
    EngineForTest test;
    uint32_t requestId = 0;
    lce_start_request(test.engine, 0, &settings, 0, &requestId);
    REQUIRE(lce_location_permission_was_lost(test.engine, 500) == LCE_OK);

    const std::vector<lce_report> reports = takeEveryReport(test.engine);
    REQUIRE(reports.size() == 1);
    CHECK(reports[0].status == LCE_STATUS_UNAVAILABLE);
    CHECK(hasReason(reports[0].reasons, "permission_denied"));
  }

  SUBCASE("the GPS fails") {
    EngineForTest test;
    uint32_t requestId = 0;
    lce_start_request(test.engine, 0, &settings, 0, &requestId);
    REQUIRE(lce_location_provider_failed(test.engine, 500) == LCE_OK);

    const std::vector<lce_report> reports = takeEveryReport(test.engine);
    REQUIRE(reports.size() == 1);
    CHECK(reports[0].status == LCE_STATUS_UNAVAILABLE);
    CHECK(hasReason(reports[0].reasons, "native_provider_unavailable"));
  }
}

// --- Sessions -----------------------------------------------------------------------------

TEST_CASE("facade: a session is opened, used, cleared and closed") {
  EngineForTest test;

  const lce_session_settings sessionSettings = sessionThatKeepsCollecting();
  lce_session_opened opened{};
  REQUIRE(lce_open_session(test.engine, &sessionSettings, 0, &opened) == LCE_OK);
  CHECK(opened.session_id == 1);
  CHECK(opened.collecting == LCE_COLLECTING_ACTIVE);
  CHECK(opened.reasons == 0);

  lce_platform_work work{};
  lce_what_the_platform_must_do_now(test.engine, &work);
  CHECK(work.location_updates_are_wanted == 1);
  CHECK(work.time_between_location_updates_ms == 5000);
  CHECK(work.phone_activity_updates_are_wanted == 0);
  CHECK(work.wake_up_at_ms == 900000);

  // The session collects a reading; a request that gets nothing new
  // answers with it.
  const lce_reading reading = readingNorth(0, 20.0, 8.0);
  lce_readings_arrived(test.engine, &reading, 1, 0);

  const lce_request_settings settings = usualRequest();
  uint32_t requestId = 0;
  REQUIRE(lce_start_request(test.engine, opened.session_id, &settings, 2000, &requestId) ==
          LCE_OK);
  lce_time_passed(test.engine, 6500);
  lce_time_passed(test.engine, 13500);

  std::vector<lce_report> reports = takeEveryReport(test.engine);
  REQUIRE(reports.size() == 1);
  CHECK(reports[0].session_id == 1);
  CHECK(reports[0].status == LCE_STATUS_DEGRADED);
  CHECK(reports[0].source == LCE_SOURCE_CACHED);
  CHECK(reports[0].reading_age_ms == 13500);
  CHECK(reports[0].readings_received == 0);
  CHECK(reports[0].readings_examined == 1);

  // Clearing: the next request has nothing to fall back on.
  CHECK(lce_clear_session_history(test.engine, opened.session_id) == LCE_OK);
  lce_start_request(test.engine, opened.session_id, &settings, 20000, &requestId);
  lce_time_passed(test.engine, 24500);
  lce_time_passed(test.engine, 31500);
  reports = takeEveryReport(test.engine);
  REQUIRE(reports.size() == 1);
  CHECK(reports[0].status == LCE_STATUS_UNAVAILABLE);

  // Closing stops the request that is running and turns everything off.
  lce_start_request(test.engine, opened.session_id, &settings, 40000, &requestId);
  REQUIRE(lce_close_session(test.engine, opened.session_id, 41000) == LCE_OK);
  reports = takeEveryReport(test.engine);
  REQUIRE(reports.size() == 1);
  CHECK(reports[0].status == LCE_STATUS_CANCELLED);
  CHECK(hasReason(reports[0].reasons, "session_stopped"));

  lce_what_the_platform_must_do_now(test.engine, &work);
  CHECK(work.location_updates_are_wanted == 0);
  CHECK(work.wake_up_is_wanted == 0);

  // The session is gone.
  CHECK(lce_close_session(test.engine, opened.session_id, 42000) == LCE_OK);  // harmless
  CHECK(lce_clear_session_history(test.engine, opened.session_id) == LCE_ERROR_NO_SUCH_SESSION);

  REQUIRE(lce_start_request(test.engine, opened.session_id, &settings, 43000, &requestId) ==
          LCE_OK);
  reports = takeEveryReport(test.engine);
  REQUIRE(reports.size() == 1);
  CHECK(reports[0].status == LCE_STATUS_UNAVAILABLE);
  CHECK(hasReason(reports[0].reasons, "unknown_session"));
}

TEST_CASE("facade: collecting that cannot start says why, in the reason bits") {
  EngineForTest test;

  lce_session_settings settings = sessionThatKeepsCollecting();
  settings.device.location_permission = LCE_PERMISSION_DENIED;

  lce_session_opened opened{};
  REQUIRE(lce_open_session(test.engine, &settings, 0, &opened) == LCE_OK);

  CHECK(opened.session_id != 0);
  CHECK(opened.collecting == LCE_COLLECTING_BLOCKED);
  CHECK(opened.reasons == (uint64_t{1} << reasonNumber("permission_denied")));
}

TEST_CASE("facade: the phone activity goes in and comes out") {
  EngineForTest test;

  const lce_session_settings sessionSettings = sessionThatKeepsCollecting();
  lce_session_opened opened{};
  lce_open_session(test.engine, &sessionSettings, 0, &opened);

  const lce_reading reading = readingNorth(0, 20.0, 5.0);
  lce_readings_arrived(test.engine, &reading, 1, 0);
  REQUIRE(lce_phone_activity_changed(test.engine, LCE_ACTIVITY_STANDING_STILL,
                                     LCE_CONFIDENCE_HIGH, 0, 500) == LCE_OK);

  const lce_request_settings settings = usualRequest();
  uint32_t requestId = 0;
  lce_start_request(test.engine, opened.session_id, &settings, 10000, &requestId);
  lce_time_passed(test.engine, 14500);

  std::vector<lce_report> reports = takeEveryReport(test.engine);
  REQUIRE(reports.size() == 1);
  CHECK(reports[0].activity_now == LCE_ACTIVITY_STANDING_STILL);
  CHECK(reports[0].activity_confidence_now == LCE_CONFIDENCE_HIGH);
  CHECK(reports[0].standing_still_since_the_reading == 1);
  CHECK(reports[0].activity_known_until_now == 1);
  CHECK(reports[0].activity_known_since_age_ms == 14500);
  CHECK(reports[0].activity_has_gaps == 0);
  CHECK(reports[0].how_far_phone_could_be_meters == doctest::Approx(5.0 + 0.5 * 14.5));

  // After the updates stop, the rest of the time is unknown.
  REQUIRE(lce_phone_activity_updates_stopped(test.engine, 15000) == LCE_OK);
  lce_start_request(test.engine, opened.session_id, &settings, 20000, &requestId);
  lce_time_passed(test.engine, 24500);
  lce_time_passed(test.engine, 31500);

  reports = takeEveryReport(test.engine);
  REQUIRE(reports.size() == 1);
  CHECK(reports[0].activity_now == LCE_ACTIVITY_UNKNOWN);
  CHECK(reports[0].standing_still_since_the_reading == 0);
  CHECK(reports[0].activity_has_gaps == 1);
  CHECK(hasReason(reports[0].reasons, "motion_insufficient_coverage"));
}

TEST_CASE("facade: readings made up by the app are marked") {
  EngineForTest test;
  REQUIRE(lce_set_readings_are_simulated(test.engine, 1) == LCE_OK);

  const lce_request_settings settings = usualRequest();
  uint32_t requestId = 0;
  lce_start_request(test.engine, 0, &settings, 0, &requestId);
  const lce_reading reading = readingNorth(1000, 20.0, 8.0);
  lce_readings_arrived(test.engine, &reading, 1, 1000);

  const std::vector<lce_report> reports = takeEveryReport(test.engine);
  REQUIRE(reports.size() == 1);
  CHECK(reports[0].status == LCE_STATUS_AVAILABLE);
  CHECK(hasReason(reports[0].reasons, "simulated_location_source"));
}

// --- Values that make no sense ------------------------------------------------------------

TEST_CASE("GEO-012 GEO-014 GEO-015 GEO-016 an address or radius that makes no sense") {
  EngineForTest test;
  const double notANumber = std::numeric_limits<double>::quiet_NaN();
  const double infinity = std::numeric_limits<double>::infinity();

  struct Case {
    const char* name;
    double latitude;
    double longitude;
    double radius;
  };
  const Case cases[] = {
      {"latitude 90.00001", 90.00001, 0.0, 100.0},
      {"longitude 180.00001", 0.0, 180.00001, 100.0},
      {"latitude that is not a number", notANumber, 0.0, 100.0},
      {"infinite radius", 0.0, 0.0, infinity},
      {"radius zero", 0.0, 0.0, 0.0},
      {"negative radius", 0.0, 0.0, -1.0},
      {"radius of 100001 meters", 0.0, 0.0, 100001.0},
  };

  for (const Case& oneCase : cases) {
    CAPTURE(oneCase.name);
    lce_request_settings settings = usualRequest();
    settings.destination_latitude = oneCase.latitude;
    settings.destination_longitude = oneCase.longitude;
    settings.radius_meters = oneCase.radius;

    uint32_t requestId = 77;
    CHECK(lce_start_request(test.engine, 0, &settings, 0, &requestId) ==
          LCE_ERROR_INVALID_REQUEST);
    CHECK(requestId == 0);
  }

  // Nothing was started and nothing was turned on.
  CHECK(takeEveryReport(test.engine).empty());
  lce_platform_work work{};
  lce_what_the_platform_must_do_now(test.engine, &work);
  CHECK(work.location_updates_are_wanted == 0);
  CHECK(work.wake_up_is_wanted == 0);
}

TEST_CASE("PLT-029 numbers that are not one of the choices become the cautious choice") {
  EngineForTest test;

  SUBCASE("an unknown permission counts as 'never asked'") {
    lce_request_settings settings = usualRequest();
    settings.device.location_permission = 99;

    uint32_t requestId = 0;
    REQUIRE(lce_start_request(test.engine, 0, &settings, 0, &requestId) == LCE_OK);
    const std::vector<lce_report> reports = takeEveryReport(test.engine);
    REQUIRE(reports.size() == 1);
    CHECK(reports[0].status == LCE_STATUS_UNAVAILABLE);
    CHECK(hasReason(reports[0].reasons, "permission_not_determined"));
  }

  SUBCASE("an unknown provider state counts as 'no provider'") {
    lce_request_settings settings = usualRequest();
    settings.device.location_provider = -3;

    uint32_t requestId = 0;
    lce_start_request(test.engine, 0, &settings, 0, &requestId);
    const std::vector<lce_report> reports = takeEveryReport(test.engine);
    REQUIRE(reports.size() == 1);
    CHECK(hasReason(reports[0].reasons, "native_provider_unavailable"));
  }

  SUBCASE("unknown precision, activity support and mock policy") {
    lce_request_settings settings = usualRequest();
    settings.device.permission_precision = 42;
    settings.device.phone_activity_support = 42;
    settings.mock_policy = 42;  // counts as the strict policy: score 0

    uint32_t requestId = 0;
    lce_start_request(test.engine, 0, &settings, 0, &requestId);

    lce_platform_work work{};
    lce_what_the_platform_must_do_now(test.engine, &work);
    CHECK(work.phone_activity_updates_are_wanted == 0);  // "not supported"

    lce_reading reading = readingNorth(0, 20.0, 8.0);
    reading.mock_signal = LCE_MOCK_FLAGGED;
    reading.provider = 1234;  // counts as the usual provider
    lce_readings_arrived(test.engine, &reading, 1, 0);
    lce_time_passed(test.engine, 7000);

    const std::vector<lce_report> reports = takeEveryReport(test.engine);
    REQUIRE(reports.size() == 1);
    CHECK(reports[0].permission_precision == LCE_PRECISION_UNKNOWN);
    CHECK(reports[0].quality_score == 0);
    CHECK(reports[0].mock_signal == LCE_MOCK_FLAGGED);
    CHECK(hasReason(reports[0].reasons, "possible_mock_location"));
    CHECK(hasReason(reports[0].reasons, "motion_unavailable"));
  }

  SUBCASE("an unknown mock signal counts as 'the system said nothing'") {
    const lce_request_settings settings = usualRequest();
    uint32_t requestId = 0;
    lce_start_request(test.engine, 0, &settings, 0, &requestId);

    lce_reading reading = readingNorth(0, 20.0, 8.0);
    reading.mock_signal = 55;
    lce_readings_arrived(test.engine, &reading, 1, 0);

    const std::vector<lce_report> reports = takeEveryReport(test.engine);
    REQUIRE(reports.size() == 1);
    CHECK(reports[0].mock_signal == LCE_MOCK_UNKNOWN);
    CHECK(reports[0].status == LCE_STATUS_AVAILABLE);
  }

  SUBCASE("an unknown activity counts as 'unknown'") {
    const lce_session_settings sessionSettings = sessionThatKeepsCollecting();
    lce_session_opened opened{};
    lce_open_session(test.engine, &sessionSettings, 0, &opened);
    const lce_reading reading = readingNorth(0, 20.0, 5.0);
    lce_readings_arrived(test.engine, &reading, 1, 0);

    REQUIRE(lce_phone_activity_changed(test.engine, 77, 77, 0, 0) == LCE_OK);

    const lce_request_settings settings = usualRequest();
    uint32_t requestId = 0;
    lce_start_request(test.engine, opened.session_id, &settings, 5000, &requestId);
    lce_time_passed(test.engine, 9500);
    lce_time_passed(test.engine, 16500);

    const std::vector<lce_report> reports = takeEveryReport(test.engine);
    REQUIRE(reports.size() == 1);
    CHECK(reports[0].activity_now == LCE_ACTIVITY_UNKNOWN);
    CHECK(reports[0].standing_still_since_the_reading == 0);
  }
}

TEST_CASE("facade: a null pointer gives an error code and never a crash") {
  EngineForTest test;
  const lce_request_settings request = usualRequest();
  const lce_session_settings session = sessionThatKeepsCollecting();
  const lce_reading reading = readingNorth(0, 20.0, 8.0);
  lce_session_opened opened{};
  lce_platform_work work{};
  lce_report report{};
  uint32_t requestId = 0;

  // No engine.
  CHECK(lce_open_session(nullptr, &session, 0, &opened) == LCE_ERROR_NULL_ARGUMENT);
  CHECK(lce_clear_session_history(nullptr, 1) == LCE_ERROR_NULL_ARGUMENT);
  CHECK(lce_close_session(nullptr, 1, 0) == LCE_ERROR_NULL_ARGUMENT);
  CHECK(lce_start_request(nullptr, 0, &request, 0, &requestId) == LCE_ERROR_NULL_ARGUMENT);
  CHECK(lce_cancel_request(nullptr, 1, 0) == LCE_ERROR_NULL_ARGUMENT);
  CHECK(lce_readings_arrived(nullptr, &reading, 1, 0) == LCE_ERROR_NULL_ARGUMENT);
  CHECK(lce_phone_activity_changed(nullptr, 0, 0, 0, 0) == LCE_ERROR_NULL_ARGUMENT);
  CHECK(lce_phone_activity_updates_stopped(nullptr, 0) == LCE_ERROR_NULL_ARGUMENT);
  CHECK(lce_time_passed(nullptr, 0) == LCE_ERROR_NULL_ARGUMENT);
  CHECK(lce_app_went_to_background(nullptr, 0) == LCE_ERROR_NULL_ARGUMENT);
  CHECK(lce_location_permission_was_lost(nullptr, 0) == LCE_ERROR_NULL_ARGUMENT);
  CHECK(lce_location_provider_failed(nullptr, 0) == LCE_ERROR_NULL_ARGUMENT);
  CHECK(lce_set_readings_are_simulated(nullptr, 1) == LCE_ERROR_NULL_ARGUMENT);
  CHECK(lce_what_the_platform_must_do_now(nullptr, &work) == LCE_ERROR_NULL_ARGUMENT);
  CHECK(lce_take_finished_report(nullptr, &report) == 0);
  lce_engine_destroy(nullptr);  // does nothing

  // An engine, but no place to read from or to write to.
  CHECK(lce_open_session(test.engine, nullptr, 0, &opened) == LCE_ERROR_NULL_ARGUMENT);
  CHECK(lce_open_session(test.engine, &session, 0, nullptr) == LCE_ERROR_NULL_ARGUMENT);
  CHECK(lce_start_request(test.engine, 0, nullptr, 0, &requestId) == LCE_ERROR_NULL_ARGUMENT);
  CHECK(lce_start_request(test.engine, 0, &request, 0, nullptr) == LCE_ERROR_NULL_ARGUMENT);
  CHECK(lce_readings_arrived(test.engine, nullptr, 3, 0) == LCE_ERROR_NULL_ARGUMENT);
  CHECK(lce_what_the_platform_must_do_now(test.engine, nullptr) == LCE_ERROR_NULL_ARGUMENT);
  CHECK(lce_take_finished_report(test.engine, nullptr) == 0);

  // "No readings" with no list is fine.
  CHECK(lce_readings_arrived(test.engine, nullptr, 0, 0) == LCE_OK);

  // None of the calls above started or opened anything.
  CHECK(takeEveryReport(test.engine).empty());
  lce_what_the_platform_must_do_now(test.engine, &work);
  CHECK(work.location_updates_are_wanted == 0);
}

TEST_CASE("facade: taking a report when none is waiting leaves the struct alone") {
  EngineForTest test;

  lce_report report;
  std::memset(&report, 0x5A, sizeof(report));
  CHECK(lce_take_finished_report(test.engine, &report) == 0);
  CHECK(report.request_id == 0x5A5A5A5Au);
}

// --- Every value that crosses the door, one by one ----------------------------------------
// Each test changes ONE field of what goes in and checks what comes out. If a
// field were dropped or mixed up with another while crossing, one of these
// tests would notice.

namespace {

// Runs one request with no session: the reading arrives 1 second after the
// start, and the timers fire until a report comes out.
lce_report reportWith(const lce_request_settings& settings, const lce_reading& reading) {
  EngineForTest test;
  uint32_t requestId = 0;
  REQUIRE(lce_start_request(test.engine, 0, &settings, 0, &requestId) == LCE_OK);
  lce_readings_arrived(test.engine, &reading, 1, 1000);
  lce_time_passed(test.engine, 30000);

  const std::vector<lce_report> reports = takeEveryReport(test.engine);
  REQUIRE(reports.size() == 1);
  return reports[0];
}

}  // namespace

TEST_CASE("facade: each field of the request settings arrives") {
  const lce_reading reading = readingNorth(0, 20.0, 8.0);  // 1 second old when it arrives

  SUBCASE("the address") {
    lce_request_settings settings = usualRequest();
    settings.destination_latitude = 0.0;
    settings.destination_longitude = 0.01;  // about 1.1 km east

    const lce_report report = reportWith(settings, reading);
    CHECK(report.distance_to_destination_meters > 1000.0);
    CHECK(report.relation == LCE_RELATION_SUPPORTS_OUTSIDE);

    settings.destination_latitude = 0.01;  // about 1.1 km north
    settings.destination_longitude = 0.0;
    const lce_report other = reportWith(settings, reading);
    CHECK(other.distance_to_destination_meters > 1000.0);
    CHECK(other.distance_to_destination_meters != report.distance_to_destination_meters);
  }

  SUBCASE("the radius") {
    lce_request_settings settings = usualRequest();
    settings.radius_meters = 60.0;  // 20 + 53 = 73 no longer fits
    const lce_report report = reportWith(settings, reading);
    CHECK(report.radius_meters == 60.0);
    CHECK(report.relation == LCE_RELATION_INCONCLUSIVE);
  }

  SUBCASE("how old a reading can be and still count as fresh") {
    lce_request_settings settings = usualRequest();
    settings.newest_age_that_counts_as_fresh_ms = 500;  // the reading is 1000 ms old
    const lce_report report = reportWith(settings, reading);
    CHECK(report.source == LCE_SOURCE_CACHED);
    CHECK(hasReason(report.reasons, "location_stale"));
  }

  SUBCASE("how old a reading can be and still be used") {
    lce_request_settings settings = usualRequest();
    settings.newest_age_that_counts_as_fresh_ms = 500;
    settings.oldest_age_allowed_ms = 900;  // the reading is 1000 ms old
    const lce_report report = reportWith(settings, reading);
    CHECK(report.has_position == 0);
    CHECK(report.status == LCE_STATUS_UNAVAILABLE);
  }

  SUBCASE("the worst GPS error accepted") {
    lce_request_settings settings = usualRequest();
    settings.worst_gps_error_accepted_meters = 7.0;  // the reading has 8
    const lce_report report = reportWith(settings, reading);
    CHECK(report.status == LCE_STATUS_DEGRADED);
    CHECK(hasReason(report.reasons, "location_low_accuracy"));
  }

  SUBCASE("the fastest possible speed") {
    lce_request_settings settings = usualRequest();
    settings.fastest_possible_speed_mps = 5.0;
    const lce_report report = reportWith(settings, reading);
    CHECK(report.how_far_phone_could_be_meters == 13.0);  // 8 + 5 x 1 second
  }

  SUBCASE("refusing the history") {
    EngineForTest test;
    const lce_session_settings sessionSettings = sessionThatKeepsCollecting();
    lce_session_opened opened{};
    lce_open_session(test.engine, &sessionSettings, 0, &opened);
    lce_readings_arrived(test.engine, &reading, 1, 0);  // a reading in the history

    lce_request_settings settings = usualRequest();
    settings.allow_answer_from_history = 0;
    uint32_t requestId = 0;
    lce_start_request(test.engine, opened.session_id, &settings, 1000, &requestId);
    lce_time_passed(test.engine, 30000);

    const std::vector<lce_report> reports = takeEveryReport(test.engine);
    REQUIRE(reports.size() == 1);
    CHECK(reports[0].has_position == 0);
    CHECK(reports[0].readings_examined == 1);
  }

  SUBCASE("not using the phone activity") {
    EngineForTest test;
    lce_request_settings settings = usualRequest();
    settings.use_phone_activity = 0;
    uint32_t requestId = 0;
    lce_start_request(test.engine, 0, &settings, 0, &requestId);

    lce_platform_work work{};
    lce_what_the_platform_must_do_now(test.engine, &work);
    CHECK(work.phone_activity_updates_are_wanted == 0);
  }

  SUBCASE("the policy for simulated readings") {
    lce_reading simulated = reading;
    simulated.mock_signal = LCE_MOCK_FLAGGED;

    lce_request_settings strict = usualRequest();
    strict.mock_policy = LCE_MOCK_POLICY_ZERO_SCORE;
    CHECK(reportWith(strict, simulated).quality_score == 0);

    lce_request_settings onlyTell = usualRequest();
    onlyTell.mock_policy = LCE_MOCK_POLICY_FLAG_ONLY;
    const lce_report report = reportWith(onlyTell, simulated);
    CHECK(report.quality_score == 75);
    CHECK(report.status == LCE_STATUS_AVAILABLE);
    CHECK(hasReason(report.reasons, "possible_mock_location"));
  }

  SUBCASE("the three times") {
    EngineForTest test;
    lce_request_settings settings = usualRequest();
    settings.total_time_allowed_ms = 9000;
    settings.first_attempt_time_ms = 2000;
    settings.extra_time_ms = 3000;
    uint32_t requestId = 0;
    lce_start_request(test.engine, 0, &settings, 0, &requestId);

    lce_platform_work work{};
    lce_what_the_platform_must_do_now(test.engine, &work);
    CHECK(work.wake_up_at_ms == 2000);  // the first attempt

    lce_time_passed(test.engine, 2000);
    lce_what_the_platform_must_do_now(test.engine, &work);
    CHECK(work.wake_up_at_ms == 5000);  // the extra time

    lce_time_passed(test.engine, 5000);
    const std::vector<lce_report> reports = takeEveryReport(test.engine);
    REQUIRE(reports.size() == 1);
    CHECK(reports[0].duration_ms == 5000);

    // And the total time, when it is the shortest of the three.
    EngineForTest other;
    settings.total_time_allowed_ms = 1500;
    lce_start_request(other.engine, 0, &settings, 0, &requestId);
    lce_what_the_platform_must_do_now(other.engine, &work);
    CHECK(work.wake_up_at_ms == 1500);
  }
}

TEST_CASE("facade: each field of the device state arrives") {
  const lce_reading reading = readingNorth(0, 20.0, 8.0);

  SUBCASE("the three permission problems") {
    struct Case {
      int32_t permission;
      const char* reason;
    };
    const Case cases[] = {
        {LCE_PERMISSION_NOT_DETERMINED, "permission_not_determined"},
        {LCE_PERMISSION_DENIED, "permission_denied"},
        {LCE_PERMISSION_RESTRICTED, "permission_restricted"},
    };
    for (const Case& oneCase : cases) {
      lce_request_settings settings = usualRequest();
      settings.device.location_permission = oneCase.permission;
      const lce_report report = reportWith(settings, reading);
      CHECK(report.status == LCE_STATUS_UNAVAILABLE);
      CHECK(hasReason(report.reasons, oneCase.reason));
      CHECK(report.duration_ms == 0);  // it ended at the start
    }
  }

  SUBCASE("an approximate permission") {
    lce_request_settings settings = usualRequest();
    settings.device.permission_precision = LCE_PRECISION_APPROXIMATE;
    const lce_report report = reportWith(settings, reading);
    CHECK(report.permission_precision == LCE_PRECISION_APPROXIMATE);
    CHECK(report.status == LCE_STATUS_DEGRADED);
    CHECK((report.limits_applied & (1u << 1)) != 0);  // limit number 1
    CHECK(std::string(lce_score_limit_name(1)) == "approximate_permission");
    CHECK(report.quality_score <= 30);
    CHECK(hasReason(report.reasons, "precision_reduced"));
  }

  SUBCASE("location turned off") {
    lce_request_settings settings = usualRequest();
    settings.device.location_is_turned_on = 0;
    CHECK(hasReason(reportWith(settings, reading).reasons, "location_services_disabled"));
  }

  SUBCASE("any number that is not zero means yes") {
    lce_request_settings settings = usualRequest();
    settings.device.location_is_turned_on = 7;
    settings.device.app_is_in_foreground = -1;
    CHECK(reportWith(settings, reading).status == LCE_STATUS_AVAILABLE);
  }

  SUBCASE("no Google Play services") {
    lce_request_settings settings = usualRequest();
    settings.device.location_provider = LCE_PROVIDER_STATE_PLAY_SERVICES_MISSING;
    CHECK(hasReason(reportWith(settings, reading).reasons, "play_services_unavailable"));
  }

  SUBCASE("no location provider") {
    lce_request_settings settings = usualRequest();
    settings.device.location_provider = LCE_PROVIDER_STATE_UNAVAILABLE;
    CHECK(hasReason(reportWith(settings, reading).reasons, "native_provider_unavailable"));
  }

  SUBCASE("the app is not in the foreground") {
    lce_request_settings settings = usualRequest();
    settings.device.app_is_in_foreground = 0;
    CHECK(hasReason(reportWith(settings, reading).reasons, "foreground_required"));
  }

  SUBCASE("the phone activity permission was denied") {
    lce_request_settings settings = usualRequest();
    settings.device.phone_activity_support = LCE_ACTIVITY_SUPPORT_PERMISSION_DENIED;
    const lce_report report = reportWith(settings, reading);
    CHECK(report.status == LCE_STATUS_AVAILABLE);
    CHECK(hasReason(report.reasons, "motion_permission_denied"));
  }

  SUBCASE("the phone cannot report its activity") {
    lce_request_settings settings = usualRequest();
    settings.device.phone_activity_support = LCE_ACTIVITY_SUPPORT_NOT_SUPPORTED;
    CHECK(hasReason(reportWith(settings, reading).reasons, "motion_unavailable"));

    settings.device.phone_activity_support = LCE_ACTIVITY_SUPPORT_PERMISSION_NOT_ASKED;
    CHECK(hasReason(reportWith(settings, reading).reasons, "motion_unavailable"));
  }
}

TEST_CASE("facade: each field of a reading arrives") {
  const lce_request_settings settings = usualRequest();

  SUBCASE("the position") {
    lce_reading reading = readingNorth(0, 20.0, 8.0);
    reading.longitude = 0.0001;
    const lce_report report = reportWith(settings, reading);
    CHECK(report.latitude == reading.latitude);
    CHECK(report.longitude == 0.0001);
  }

  SUBCASE("no GPS error given") {
    lce_reading reading = readingNorth(0, 20.0, 8.0);
    reading.has_accuracy = 0;
    const lce_report report = reportWith(settings, reading);
    CHECK(report.has_position == 0);
    CHECK(hasReason(report.reasons, "location_accuracy_unavailable"));
  }

  SUBCASE("a time that cannot be trusted") {
    lce_reading reading = readingNorth(0, 20.0, 8.0);
    reading.time_is_reliable = 0;
    const lce_report report = reportWith(settings, reading);
    CHECK(report.has_position == 0);
    CHECK(report.age_reliability == LCE_AGE_UNRELIABLE);
    CHECK(hasReason(report.reasons, "location_age_unreliable"));
  }

  SUBCASE("a time worked out from the wall clock") {
    lce_reading reading = readingNorth(0, 20.0, 8.0);
    reading.capture_time_was_estimated = 1;
    CHECK(reportWith(settings, reading).age_reliability == LCE_AGE_ESTIMATED);
  }

  SUBCASE("taken with an approximate permission") {
    lce_reading reading = readingNorth(0, 20.0, 8.0);
    reading.only_approximate_permission = 1;
    const lce_report report = reportWith(settings, reading);
    CHECK(report.status == LCE_STATUS_DEGRADED);
    CHECK((report.limits_applied & (1u << 1)) != 0);  // "approximate_permission"
    CHECK(report.quality_score <= 30);
    CHECK(hasReason(report.reasons, "precision_reduced"));
  }

  SUBCASE("a speed and its error") {
    lce_reading reading = readingNorth(0, 20.0, 8.0);
    reading.has_speed = 1;
    reading.speed_mps = 0.3;
    reading.has_speed_accuracy = 1;
    reading.speed_accuracy_mps = 0.5;
    // 1 second old: (0.3 + 2 x 0.5) x 1 + 2.5 x 1 x 1 / 2 = 2.55 meters.
    CHECK(reportWith(settings, reading).how_far_phone_could_be_meters ==
          doctest::Approx(8.0 + 2.55));

    // The speed alone, with no error, is not enough to use it.
    reading.has_speed_accuracy = 0;
    CHECK(reportWith(settings, reading).how_far_phone_could_be_meters == 53.0);

    // Neither is the error alone.
    reading.has_speed = 0;
    reading.has_speed_accuracy = 1;
    CHECK(reportWith(settings, reading).how_far_phone_could_be_meters == 53.0);
  }

  SUBCASE("a direction that does not exist") {
    lce_reading reading = readingNorth(0, 20.0, 8.0);
    reading.has_heading = 1;
    reading.heading_degrees = 400.0;
    const lce_report report = reportWith(settings, reading);
    CHECK(report.status == LCE_STATUS_AVAILABLE);  // the reading is still used
    CHECK(hasReason(report.reasons, "location_metadata_inconsistent"));

    reading.heading_degrees = 90.0;
    CHECK_FALSE(hasReason(reportWith(settings, reading).reasons,
                          "location_metadata_inconsistent"));

    reading.has_heading = 0;
    reading.heading_degrees = 400.0;  // not looked at
    CHECK_FALSE(hasReason(reportWith(settings, reading).reasons,
                          "location_metadata_inconsistent"));
  }

  SUBCASE("the provider") {
    // Two readings that contradict each other. Only when they come from
    // different providers does the report say "provider_conflict".
    const int32_t providers[] = {LCE_READING_PROVIDER_FUSED, LCE_READING_PROVIDER_CORE_LOCATION,
                                 LCE_READING_PROVIDER_SYSTEM_CACHE};
    for (const int32_t providerOfTheSecond : providers) {
      EngineForTest test;
      uint32_t requestId = 0;
      lce_request_settings strict = usualRequest();
      strict.worst_gps_error_accepted_meters = 1.0;  // keep the request running
      lce_start_request(test.engine, 0, &strict, 0, &requestId);

      lce_reading first = readingNorth(0, 0.0, 5.0);
      lce_reading second = readingNorth(2000, 450.0, 5.0);
      second.provider = providerOfTheSecond;
      lce_readings_arrived(test.engine, &first, 1, 0);
      lce_readings_arrived(test.engine, &second, 1, 2000);
      lce_time_passed(test.engine, 30000);

      const std::vector<lce_report> reports = takeEveryReport(test.engine);
      REQUIRE(reports.size() == 1);
      const bool differentProviders = (providerOfTheSecond != LCE_READING_PROVIDER_FUSED);
      CHECK(hasReason(reports[0].reasons, "provider_conflict") == differentProviders);
      CHECK(hasReason(reports[0].reasons, "suspected_gps_jump"));
    }
  }

  SUBCASE("the simulation mark") {
    lce_reading reading = readingNorth(0, 20.0, 8.0);
    reading.mock_signal = LCE_MOCK_UNKNOWN;
    CHECK(reportWith(settings, reading).mock_signal == LCE_MOCK_UNKNOWN);
    reading.mock_signal = LCE_MOCK_NOT_FLAGGED;
    CHECK(reportWith(settings, reading).mock_signal == LCE_MOCK_NOT_FLAGGED);
    reading.mock_signal = LCE_MOCK_FLAGGED;
    CHECK(reportWith(settings, reading).mock_signal == LCE_MOCK_FLAGGED);
  }
}

TEST_CASE("facade: every activity and every confidence goes in and comes out") {
  const int32_t activities[] = {LCE_ACTIVITY_STANDING_STILL, LCE_ACTIVITY_WALKING,
                                LCE_ACTIVITY_RUNNING, LCE_ACTIVITY_CYCLING,
                                LCE_ACTIVITY_IN_VEHICLE};
  const int32_t confidences[] = {LCE_CONFIDENCE_HIGH, LCE_CONFIDENCE_MEDIUM, LCE_CONFIDENCE_LOW,
                                 LCE_CONFIDENCE_UNKNOWN};

  for (const int32_t activity : activities) {
    for (const int32_t confidence : confidences) {
      EngineForTest test;
      REQUIRE(lce_phone_activity_changed(test.engine, activity, confidence, 0, 0) == LCE_OK);

      const lce_request_settings settings = usualRequest();
      uint32_t requestId = 0;
      lce_start_request(test.engine, 0, &settings, 0, &requestId);
      const lce_reading reading = readingNorth(1000, 20.0, 8.0);
      lce_readings_arrived(test.engine, &reading, 1, 1000);

      const std::vector<lce_report> reports = takeEveryReport(test.engine);
      REQUIRE(reports.size() == 1);
      CHECK(reports[0].activity_now == activity);
      CHECK(reports[0].activity_confidence_now == confidence);
    }
  }
}

TEST_CASE("facade: each field of the session settings arrives") {
  SUBCASE("a session that does not collect") {
    EngineForTest test;
    lce_session_settings settings = sessionThatKeepsCollecting();
    settings.keep_collecting_readings = 0;
    lce_session_opened opened{};
    lce_open_session(test.engine, &settings, 0, &opened);

    CHECK(opened.collecting == LCE_COLLECTING_NOT_REQUESTED);
    lce_platform_work work{};
    lce_what_the_platform_must_do_now(test.engine, &work);
    CHECK(work.location_updates_are_wanted == 0);
  }

  SUBCASE("the time between readings and the longest collecting time") {
    EngineForTest test;
    lce_session_settings settings = sessionThatKeepsCollecting();
    settings.time_between_readings_ms = 3000;
    settings.longest_collecting_time_ms = 60000;
    lce_session_opened opened{};
    lce_open_session(test.engine, &settings, 1000, &opened);

    lce_platform_work work{};
    lce_what_the_platform_must_do_now(test.engine, &work);
    CHECK(work.time_between_location_updates_ms == 3000);
    CHECK(work.wake_up_at_ms == 61000);

    REQUIRE(lce_time_passed(test.engine, 61000) == LCE_OK);
    lce_what_the_platform_must_do_now(test.engine, &work);
    CHECK(work.location_updates_are_wanted == 0);
    CHECK(work.wake_up_is_wanted == 0);
  }

  SUBCASE("the device state of the session") {
    EngineForTest test;
    lce_session_settings settings = sessionThatKeepsCollecting();
    settings.device.location_is_turned_on = 0;
    lce_session_opened opened{};
    lce_open_session(test.engine, &settings, 0, &opened);

    CHECK(opened.collecting == LCE_COLLECTING_BLOCKED);
    CHECK(opened.reasons == (uint64_t{1} << reasonNumber("location_services_disabled")));
  }

  SUBCASE("a ninth session is refused") {
    EngineForTest test;
    const lce_session_settings settings = sessionThatKeepsCollecting();
    lce_session_opened opened{};
    for (int count = 0; count < 8; ++count) {
      lce_open_session(test.engine, &settings, 0, &opened);
      CHECK(opened.session_id == static_cast<uint32_t>(count + 1));
    }
    REQUIRE(lce_open_session(test.engine, &settings, 0, &opened) == LCE_OK);
    CHECK(opened.session_id == 0);
    CHECK(opened.reasons == (uint64_t{1} << reasonNumber("busy")));
  }
}

TEST_CASE("facade: when nobody takes the reports, starting is refused with its own code") {
  EngineForTest test;
  const lce_request_settings settings = usualRequest();

  // Session 77 does not exist: each start leaves one report waiting.
  uint32_t requestId = 0;
  for (int count = 0; count < 20; ++count) {
    REQUIRE(lce_start_request(test.engine, 77, &settings, 0, &requestId) == LCE_OK);
  }
  CHECK(lce_start_request(test.engine, 77, &settings, 0, &requestId) ==
        LCE_ERROR_TOO_MANY_REPORTS_WAITING);
  CHECK(requestId == 0);

  CHECK(takeEveryReport(test.engine).size() == 20);
  CHECK(lce_start_request(test.engine, 77, &settings, 0, &requestId) == LCE_OK);
}

// --- Long deliveries, replay, texts -------------------------------------------------------

TEST_CASE("facade: a delivery longer than what is converted at a time is fully used") {
  EngineForTest test;

  lce_request_settings settings = usualRequest();
  settings.worst_gps_error_accepted_meters = 1.0;  // nothing is good enough: it keeps running
  uint32_t requestId = 0;
  lce_start_request(test.engine, 0, &settings, 0, &requestId);

  // 40 readings, 100 milliseconds apart.
  std::vector<lce_reading> readings;
  for (int count = 0; count < 40; ++count) {
    readings.push_back(readingNorth(count * 100, 20.0, 8.0));
  }
  REQUIRE(lce_readings_arrived(test.engine, readings.data(), 40, 4000) == LCE_OK);
  lce_time_passed(test.engine, 12000);

  const std::vector<lce_report> reports = takeEveryReport(test.engine);
  REQUIRE(reports.size() == 1);
  CHECK(reports[0].readings_received == 40);
  CHECK(reports[0].readings_kept == 40);
  CHECK(reports[0].readings_examined == 10);  // the history holds 10
}

TEST_CASE("REC-029 the same script gives the same report, byte by byte") {
  auto runTheScript = [](lce_report& report) {
    EngineForTest test;

    const lce_session_settings sessionSettings = sessionThatKeepsCollecting();
    lce_session_opened opened{};
    lce_open_session(test.engine, &sessionSettings, 0, &opened);

    lce_reading readings[3] = {
        readingNorth(0, 20.0, 8.0),
        readingNorth(2000, 470.0, 6.0),  // a jump
        readingNorth(5000, 21.0, 12.0),
    };
    readings[2].has_speed = 1;
    readings[2].speed_mps = 0.4;
    readings[2].has_speed_accuracy = 1;
    readings[2].speed_accuracy_mps = 0.3;
    lce_readings_arrived(test.engine, &readings[0], 1, 0);
    lce_readings_arrived(test.engine, &readings[1], 1, 5000);
    lce_readings_arrived(test.engine, &readings[2], 1, 10000);
    lce_phone_activity_changed(test.engine, LCE_ACTIVITY_WALKING, LCE_CONFIDENCE_HIGH, 6000,
                               10000);

    const lce_request_settings settings = usualRequest();
    uint32_t requestId = 0;
    lce_start_request(test.engine, opened.session_id, &settings, 12000, &requestId);
    lce_time_passed(test.engine, 16500);
    lce_time_passed(test.engine, 23500);

    std::memset(&report, 0, sizeof(report));
    REQUIRE(lce_take_finished_report(test.engine, &report) == 1);
  };

  lce_report first;
  lce_report second;
  runTheScript(first);
  runTheScript(second);

  CHECK(std::memcmp(&first, &second, sizeof(lce_report)) == 0);
  CHECK(first.has_position == 1);
  CHECK(first.gps_jumps_found == 1);
}

TEST_CASE("facade: every number used in a report has a public text") {
  CHECK(lce_reason_count() == 44);
  for (int32_t number = 0; number < static_cast<int32_t>(lce_reason_count()); ++number) {
    REQUIRE(lce_reason_name(number) != nullptr);
  }
  CHECK(std::string(lce_reason_name(0)) == "permission_not_determined");
  CHECK(std::string(lce_reason_name(1)) == "permission_denied");
  CHECK(std::string(lce_reason_name(43)) == "simulated_location_source");
  CHECK(lce_reason_name(-1) == nullptr);
  CHECK(lce_reason_name(44) == nullptr);

  CHECK(lce_score_limit_count() == 11);
  for (int32_t number = 0; number < static_cast<int32_t>(lce_score_limit_count()); ++number) {
    REQUIRE(lce_score_limit_name(number) != nullptr);
  }
  CHECK(std::string(lce_score_limit_name(0)) == "cached");
  CHECK(lce_score_limit_name(-1) == nullptr);
  CHECK(lce_score_limit_name(11) == nullptr);

  CHECK(lce_step_count() == 5);
  CHECK(std::string(lce_step_name(0)) == "started");
  CHECK(std::string(lce_step_name(1)) == "first_attempt");
  CHECK(std::string(lce_step_name(2)) == "extra_time");
  CHECK(std::string(lce_step_name(3)) == "answer_from_history");
  CHECK(std::string(lce_step_name(4)) == "finished");
  CHECK(lce_step_name(-1) == nullptr);
  CHECK(lce_step_name(5) == nullptr);
}

TEST_CASE("facade: many engines can be created and destroyed") {
  for (int round = 0; round < 50; ++round) {
    lce_engine* engine = lce_engine_create();
    REQUIRE(engine != nullptr);

    const lce_request_settings settings = usualRequest();
    uint32_t requestId = 0;
    lce_start_request(engine, 0, &settings, 0, &requestId);
    CHECK(requestId == 1);  // each engine counts on its own

    lce_engine_destroy(engine);  // with a request still running
  }
}
