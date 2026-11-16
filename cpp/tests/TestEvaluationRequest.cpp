// Tests of one evaluation, from start to finish (EvaluationRequest.cpp).
//
// The test plays the part of the engine and of the platform: it stores the
// readings, tells the evaluation what happened, and says what time it is.
// No test waits for real time.
//
// The address is the starting point and the delivery radius is 100 meters.
// The usual times: first attempt 4.5 s, extra time 7 s, total 12 s.
#include "third_party/doctest.h"

#include <cstdint>
#include <vector>

#include "EvaluationRequest.hpp"
#include "LocationHistory.hpp"
#include "PhoneActivityTimeline.hpp"
#include "ReadingChecker.hpp"
#include "TestSupport.hpp"

using namespace lce;
using lce_test::activityReport;
using lce_test::DeterministicRng;
using lce_test::readingNorth;
using lce_test::secondsToMs;

namespace {

// A phone where everything works: permission granted, location turned on,
// app in the foreground. It cannot report its activity.
DeviceState phoneWhereEverythingWorks() {
  DeviceState device;
  device.locationPermission = LocationPermission::Granted;
  device.permissionPrecision = PermissionPrecision::Precise;
  device.locationIsTurnedOn = true;
  device.locationProvider = LocationProviderState::Available;
  device.appIsInForeground = true;
  device.phoneActivity = PhoneActivitySupport::NotSupported;
  return device;
}

EvaluationSettings usualSettings() {
  EvaluationSettings settings;
  settings.asked.destination = Coordinates{0.0, 0.0};
  settings.asked.radiusMeters = 100.0;
  settings.asked.usePhoneActivity = false;
  settings.device = phoneWhereEverythingWorks();
  return settings;
}

// One evaluation together with the memory it reads from.
struct OneEvaluation {
  LocationHistory history;
  PhoneActivityHistory phoneActivity;
  EvaluationRequest request;

  void start(double seconds, const EvaluationSettings& settings = usualSettings()) {
    request.start(7, 0, settings, history, phoneActivity, secondsToMs(seconds));
  }

  // The GPS delivers one reading: it is stored, and the evaluation is told.
  void gpsDelivers(double seconds, const LocationReading& reading) {
    const std::int64_t nowMs = secondsToMs(seconds);
    const LocationHistory::StoreResult result = history.store(reading, nowMs);

    NotesAboutNewReadings notes;
    notes.howManyArrived = 1;
    const bool wasKept = (result == LocationHistory::StoreResult::Stored ||
                          result == LocationHistory::StoreResult::StoredOutOfOrder);
    if (wasKept) {
      notes.howManyWereKept = 1;
    }
    request.whenReadingsArrive(history, phoneActivity, nowMs, notes);
  }

  // The GPS delivers a reading that was thrown away at the entrance.
  void gpsDeliversABrokenReading(double seconds, ReasonCode why) {
    NotesAboutNewReadings notes;
    notes.howManyArrived = 1;
    notes.reasons.add(why);
    request.whenReadingsArrive(history, phoneActivity, secondsToMs(seconds), notes);
  }

  void timePasses(double seconds) {
    request.whenTimePasses(history, phoneActivity, secondsToMs(seconds));
  }

  void stop(double seconds, WhyTheEvaluationStopped why) {
    request.stop(why, history, phoneActivity, secondsToMs(seconds));
  }

  const Assessment& report() const { return request.report(); }
};

bool hasReason(const Assessment& report, ReasonCode reason) {
  return report.reasons.contains(reason);
}

std::vector<EvaluationStep> stepsOf(const Assessment& report) {
  return std::vector<EvaluationStep>(report.steps.begin(),
                                     report.steps.begin() + report.howManySteps);
}

using Steps = std::vector<EvaluationStep>;
const EvaluationStep Started = EvaluationStep::Started;
const EvaluationStep FirstAttempt = EvaluationStep::FirstAttempt;
const EvaluationStep ExtraTime = EvaluationStep::ExtraTime;
const EvaluationStep AnswerFromHistory = EvaluationStep::AnswerFromHistory;
const EvaluationStep Finished = EvaluationStep::Finished;

}  // namespace

// --- The examples at the top of EvaluationRequest.cpp ----------------------------

TEST_CASE("REC-001 a good reading in the first attempt ends the evaluation at once") {
  OneEvaluation evaluation;
  evaluation.start(0);

  CHECK_FALSE(evaluation.request.isFinished());
  CHECK(evaluation.request.wantsLocationUpdates());
  CHECK(evaluation.request.nextWakeUpAtMs() == 4500);

  evaluation.gpsDelivers(1.2, readingNorth(1.2, 20, 8));

  REQUIRE(evaluation.request.isFinished());
  CHECK_FALSE(evaluation.request.wantsLocationUpdates());

  const Assessment& report = evaluation.report();
  CHECK(report.status == AssessmentStatus::Available);
  CHECK(report.source == FixSource::Fresh);
  CHECK(report.relation == EvidenceRelation::SupportsInside);
  CHECK(report.requestId == 7);
  CHECK(report.durationMs == 1200);
  CHECK_FALSE(report.firstAttemptTimedOut);
  CHECK_FALSE(report.extraTimeWasUsed);
  CHECK_FALSE(report.hasFallbackCause);
  CHECK(report.readingsReceived == 1);
  CHECK(report.readingsKept == 1);
  CHECK(stepsOf(report) == Steps{Started, FirstAttempt, Finished});
}

TEST_CASE("REC-003 a weak first reading starts the extra time; a good one ends it") {
  OneEvaluation evaluation;
  evaluation.start(0);

  evaluation.gpsDelivers(1, readingNorth(1, 20, 500));  // GPS error of 500 meters

  CHECK_FALSE(evaluation.request.isFinished());
  CHECK(evaluation.request.wantsLocationUpdates());
  CHECK(evaluation.request.nextWakeUpAtMs() == 8000);  // 1 s + 7 s of extra time

  evaluation.gpsDelivers(3, readingNorth(3, 20, 8));

  REQUIRE(evaluation.request.isFinished());
  const Assessment& report = evaluation.report();
  CHECK(report.status == AssessmentStatus::Available);
  CHECK(report.source == FixSource::Recovered);
  CHECK(report.gpsErrorMeters == 8.0);
  CHECK(report.extraTimeWasUsed);
  CHECK_FALSE(report.firstAttemptTimedOut);
  REQUIRE(report.hasFallbackCause);
  CHECK(report.fallbackCause == ReasonCode::LocationLowAccuracy);
  CHECK(report.readingsReceived == 2);
  CHECK(report.durationMs == 3000);
  CHECK(stepsOf(report) == Steps{Started, FirstAttempt, ExtraTime, Finished});
}

TEST_CASE("REC-005 REC-011 the GPS never answers and nothing is remembered") {
  OneEvaluation evaluation;
  evaluation.start(0);

  evaluation.timePasses(4.5);  // the first attempt is over
  CHECK_FALSE(evaluation.request.isFinished());
  CHECK(evaluation.request.wantsLocationUpdates());
  CHECK(evaluation.request.nextWakeUpAtMs() == 11500);

  evaluation.timePasses(11.5);  // the extra time is over
  REQUIRE(evaluation.request.isFinished());
  CHECK_FALSE(evaluation.request.wantsLocationUpdates());

  const Assessment& report = evaluation.report();
  CHECK(report.status == AssessmentStatus::Unavailable);
  CHECK(report.source == FixSource::Unavailable);
  CHECK_FALSE(report.hasPosition);
  CHECK(report.score.qualityScore == 0);
  CHECK(report.relation == EvidenceRelation::Inconclusive);
  CHECK(report.firstAttemptTimedOut);
  CHECK(report.extraTimeWasUsed);
  CHECK(report.fallbackCause == ReasonCode::InitialFixTimeout);
  CHECK(report.durationMs == 11500);
  CHECK(report.readingsReceived == 0);
  CHECK(hasReason(report, ReasonCode::InitialFixTimeout));
  CHECK(hasReason(report, ReasonCode::RecoveryWindowTimeout));
  CHECK(hasReason(report, ReasonCode::NoUsableFix));
  CHECK(hasReason(report, ReasonCode::HistoryEmpty));
  CHECK_FALSE(hasReason(report, ReasonCode::OverallDeadlineReached));
  CHECK(stepsOf(report) == Steps{Started, FirstAttempt, ExtraTime, AnswerFromHistory, Finished});
}

TEST_CASE("REC-006 nothing new, but a remembered reading with the phone standing still") {
  OneEvaluation evaluation;
  // 10 seconds before the evaluation: a good reading, and since then the
  // phone reports "standing still".
  evaluation.history.store(readingNorth(0, 20, 8), 0);
  evaluation.phoneActivity.store(activityReport(PhoneActivity::StandingStill, 0, 30));

  EvaluationSettings settings = usualSettings();
  settings.asked.usePhoneActivity = true;
  settings.device.phoneActivity = PhoneActivitySupport::Available;

  evaluation.start(10, settings);
  evaluation.timePasses(14.5);  // the first attempt is over

  // No extra time is spent.
  REQUIRE(evaluation.request.isFinished());
  const Assessment& report = evaluation.report();
  CHECK(report.status == AssessmentStatus::Degraded);
  CHECK(report.source == FixSource::Cached);
  CHECK(report.readingAgeMs == 14500);
  CHECK(report.howFarPhoneCouldBeMeters == doctest::Approx(15.25));  // 8 + 0.5 x 14.5
  CHECK(report.relation == EvidenceRelation::SupportsInside);
  CHECK(report.phoneActivity.standingStillSinceTheReading);
  CHECK(report.firstAttemptTimedOut);
  CHECK_FALSE(report.extraTimeWasUsed);
  CHECK(report.score.wasLimitedBy(ScoreLimit::Cached));
  CHECK(hasReason(report, ReasonCode::InitialFixTimeout));
  CHECK(stepsOf(report) == Steps{Started, FirstAttempt, AnswerFromHistory, Finished});
}

TEST_CASE("a remembered reading is only enough when nothing about it is weak") {
  EvaluationSettings settings = usualSettings();
  settings.asked.usePhoneActivity = true;
  settings.device.phoneActivity = PhoneActivitySupport::Available;

  SUBCASE("the phone stood still, but the reading is imprecise") {
    OneEvaluation evaluation;
    evaluation.history.store(readingNorth(0, 20, 200), 0);
    evaluation.phoneActivity.store(activityReport(PhoneActivity::StandingStill, 0, 60));

    evaluation.start(10, settings);
    evaluation.timePasses(14.5);

    CHECK_FALSE(evaluation.request.isFinished());  // the extra time is used
    CHECK(evaluation.request.nextWakeUpAtMs() == 21500);
  }

  SUBCASE("the phone stood still, but the reading is marked as simulated") {
    LocationReading simulated = readingNorth(0, 20, 8);
    simulated.mockSignal = MockSignal::FlaggedByOs;

    OneEvaluation evaluation;
    evaluation.history.store(simulated, 0);
    evaluation.phoneActivity.store(activityReport(PhoneActivity::StandingStill, 0, 60));

    evaluation.start(10, settings);
    evaluation.timePasses(14.5);

    CHECK_FALSE(evaluation.request.isFinished());
  }

  SUBCASE("the phone stood still, but the reading is probably a GPS jump") {
    OneEvaluation evaluation;
    evaluation.history.store(readingNorth(0, 0, 8), 0);
    evaluation.history.store(readingNorth(1, 0, 8), 1000);
    evaluation.history.store(readingNorth(70, 5000, 8), 70000);  // 5 km away
    evaluation.phoneActivity.store(activityReport(PhoneActivity::StandingStill, 70, 200));

    // At second 75.5 only the reading that jumped is recent enough.
    evaluation.start(71, settings);
    evaluation.timePasses(75.5);

    CHECK_FALSE(evaluation.request.isFinished());

    evaluation.timePasses(82.5);
    REQUIRE(evaluation.request.isFinished());
    CHECK(evaluation.report().status == AssessmentStatus::Degraded);
    CHECK(evaluation.report().score.wasLimitedBy(ScoreLimit::SuspectedJump));
  }

  SUBCASE("the phone did not stand still the whole time") {
    OneEvaluation evaluation;
    evaluation.history.store(readingNorth(0, 20, 8), 0);
    evaluation.phoneActivity.store(activityReport(PhoneActivity::StandingStill, 5, 60));

    evaluation.start(10, settings);
    evaluation.timePasses(14.5);

    CHECK_FALSE(evaluation.request.isFinished());
  }
}

TEST_CASE("CON-002 CON-005 CON-006 the app cancels; later events change nothing") {
  OneEvaluation evaluation;
  evaluation.start(0);

  evaluation.stop(2, WhyTheEvaluationStopped::CancelledByTheApp);

  REQUIRE(evaluation.request.isFinished());
  CHECK_FALSE(evaluation.request.wantsLocationUpdates());
  CHECK(evaluation.report().status == AssessmentStatus::Cancelled);
  CHECK_FALSE(evaluation.report().hasPosition);
  CHECK(evaluation.report().score.qualityScore == 0);
  CHECK(evaluation.report().durationMs == 2000);
  CHECK(hasReason(evaluation.report(), ReasonCode::RequestCancelled));
  CHECK(stepsOf(evaluation.report()) == Steps{Started, FirstAttempt, Finished});

  // A late reading, the timer, and a second cancel.
  evaluation.gpsDelivers(2.5, readingNorth(2.5, 20, 8));
  evaluation.timePasses(12);
  evaluation.stop(13, WhyTheEvaluationStopped::CancelledByTheApp);

  CHECK(evaluation.report().status == AssessmentStatus::Cancelled);
  CHECK(evaluation.report().durationMs == 2000);
  CHECK(evaluation.report().readingsReceived == 0);
  CHECK(stepsOf(evaluation.report()) == Steps{Started, FirstAttempt, Finished});
}

// --- Other ways the first attempt and the extra time can go ---------------------------

TEST_CASE("REC-002 a medium reading inside the limits is accepted") {
  OneEvaluation evaluation;
  evaluation.start(10);

  // Taken 5 seconds ago, GPS error 40: both inside the limits (10 s, 50 m).
  evaluation.gpsDelivers(10.5, readingNorth(5.5, 20, 40));

  REQUIRE(evaluation.request.isFinished());
  CHECK(evaluation.report().status == AssessmentStatus::Available);
  CHECK(evaluation.report().source == FixSource::Fresh);
  CHECK(evaluation.report().readingAgeMs == 5000);
}

TEST_CASE("REC-004 REC-009 nothing in the first attempt, a good reading in the extra time") {
  OneEvaluation evaluation;
  evaluation.start(0);
  evaluation.timePasses(4.5);

  evaluation.gpsDelivers(5.5, readingNorth(5.5, 20, 8));  // 1 second into the extra time

  // It stops right away: it does not wait for the 7 seconds.
  REQUIRE(evaluation.request.isFinished());
  const Assessment& report = evaluation.report();
  CHECK(report.status == AssessmentStatus::Available);
  CHECK(report.source == FixSource::Recovered);
  CHECK(report.firstAttemptTimedOut);
  CHECK(report.extraTimeWasUsed);
  CHECK(report.durationMs == 5500);
  CHECK(hasReason(report, ReasonCode::InitialFixTimeout));
  CHECK_FALSE(hasReason(report, ReasonCode::RecoveryWindowTimeout));
}

TEST_CASE("REC-007 nothing new and a remembered reading taken while in a vehicle") {
  OneEvaluation evaluation;
  evaluation.history.store(readingNorth(0, 20, 8), 0);
  evaluation.phoneActivity.store(activityReport(PhoneActivity::InVehicle, 0, 60));

  EvaluationSettings settings = usualSettings();
  settings.asked.usePhoneActivity = true;
  settings.device.phoneActivity = PhoneActivitySupport::Available;

  evaluation.start(30, settings);
  evaluation.timePasses(34.5);

  // The phone was moving: the remembered reading is not enough, so the
  // extra time is used.
  CHECK_FALSE(evaluation.request.isFinished());

  evaluation.timePasses(41.5);
  REQUIRE(evaluation.request.isFinished());

  const Assessment& report = evaluation.report();
  CHECK(report.status == AssessmentStatus::Degraded);
  CHECK(report.source == FixSource::Cached);
  CHECK(report.howFarPhoneCouldBeMeters == doctest::Approx(8.0 + 45.0 * 41.5));
  CHECK(report.relation == EvidenceRelation::Inconclusive);
  CHECK(report.score.qualityScore <= 15);
  CHECK(hasReason(report, ReasonCode::RecoveryWindowTimeout));
  CHECK(hasReason(report, ReasonCode::HistoryOnlyInconclusive));
}

TEST_CASE("REC-008 REC-010 only weak readings arrive") {
  OneEvaluation evaluation;
  evaluation.start(0);

  evaluation.gpsDelivers(1, readingNorth(1, 20, 200));
  evaluation.gpsDelivers(3, readingNorth(3, 20, 180));
  CHECK_FALSE(evaluation.request.isFinished());

  evaluation.timePasses(8);  // the extra time started at second 1
  REQUIRE(evaluation.request.isFinished());

  const Assessment& report = evaluation.report();
  CHECK(report.status == AssessmentStatus::Degraded);
  CHECK(report.hasPosition);
  CHECK(report.gpsErrorMeters == 180.0);
  CHECK(report.source == FixSource::Recovered);  // it arrived during the extra time
  CHECK(report.relation == EvidenceRelation::Inconclusive);
  CHECK(report.readingsReceived == 2);
  CHECK(report.fallbackCause == ReasonCode::LocationLowAccuracy);
  CHECK(hasReason(report, ReasonCode::LocationLowAccuracy));
  CHECK(hasReason(report, ReasonCode::RecoveryWindowTimeout));
  CHECK(stepsOf(report) == Steps{Started, FirstAttempt, ExtraTime, AnswerFromHistory, Finished});
}

TEST_CASE("a weak reading of the first attempt keeps the source 'fresh'") {
  OneEvaluation evaluation;
  evaluation.start(0);

  evaluation.gpsDelivers(1, readingNorth(1, 20, 200));
  evaluation.timePasses(8);

  REQUIRE(evaluation.request.isFinished());
  CHECK(evaluation.report().source == FixSource::Fresh);
  CHECK(evaluation.report().status == AssessmentStatus::Degraded);
}

TEST_CASE("REC-016 with no extra time, the first attempt is all there is") {
  EvaluationSettings settings = usualSettings();
  settings.extraTimeMs = 0;

  SUBCASE("nothing arrives") {
    OneEvaluation evaluation;
    evaluation.history.store(readingNorth(0, 20, 8), 0);
    evaluation.start(5, settings);

    evaluation.timePasses(9.5);

    REQUIRE(evaluation.request.isFinished());
    CHECK(evaluation.report().source == FixSource::Cached);
    CHECK(evaluation.report().status == AssessmentStatus::Degraded);
    CHECK_FALSE(evaluation.report().extraTimeWasUsed);
    CHECK(stepsOf(evaluation.report()) ==
          Steps{Started, FirstAttempt, AnswerFromHistory, Finished});
  }

  SUBCASE("a weak reading does not end the first attempt early") {
    OneEvaluation evaluation;
    evaluation.start(0, settings);

    evaluation.gpsDelivers(1, readingNorth(1, 20, 200));
    CHECK_FALSE(evaluation.request.isFinished());
    CHECK(evaluation.request.nextWakeUpAtMs() == 4500);

    // A good one can still arrive before the first attempt ends.
    evaluation.gpsDelivers(2, readingNorth(2, 20, 8));
    REQUIRE(evaluation.request.isFinished());
    CHECK(evaluation.report().status == AssessmentStatus::Available);
    CHECK(evaluation.report().source == FixSource::Fresh);
  }
}

// --- The total time ---------------------------------------------------------------------

TEST_CASE("REC-012 the total time ends during the first attempt") {
  EvaluationSettings settings = usualSettings();
  settings.totalTimeAllowedMs = 3000;  // shorter than the first attempt

  OneEvaluation evaluation;
  evaluation.start(0, settings);
  CHECK(evaluation.request.nextWakeUpAtMs() == 3000);

  evaluation.timePasses(3);

  REQUIRE(evaluation.request.isFinished());
  const Assessment& report = evaluation.report();
  CHECK(report.status == AssessmentStatus::Unavailable);
  CHECK(report.firstAttemptTimedOut);
  CHECK_FALSE(report.extraTimeWasUsed);
  CHECK(report.durationMs == 3000);
  CHECK(hasReason(report, ReasonCode::OverallDeadlineReached));
  CHECK(hasReason(report, ReasonCode::InitialFixTimeout));
  CHECK_FALSE(hasReason(report, ReasonCode::RecoveryWindowTimeout));
}

TEST_CASE("REC-014 the total time ends during the extra time") {
  EvaluationSettings settings = usualSettings();
  settings.totalTimeAllowedMs = 6000;

  OneEvaluation evaluation;
  evaluation.start(0, settings);
  evaluation.timePasses(4.5);

  // The extra time would last until 11.5, but everything must end at 6.
  CHECK(evaluation.request.nextWakeUpAtMs() == 6000);

  evaluation.gpsDelivers(5, readingNorth(5, 20, 300));  // weak: keeps waiting
  evaluation.timePasses(6);

  REQUIRE(evaluation.request.isFinished());
  const Assessment& report = evaluation.report();
  CHECK(report.status == AssessmentStatus::Degraded);  // what arrived is used
  CHECK(report.gpsErrorMeters == 300.0);
  CHECK(report.durationMs == 6000);
  CHECK(hasReason(report, ReasonCode::OverallDeadlineReached));
  CHECK_FALSE(hasReason(report, ReasonCode::RecoveryWindowTimeout));
}

TEST_CASE("REC-015 a short total time never becomes a long wait") {
  EvaluationSettings settings = usualSettings();
  settings.totalTimeAllowedMs = 1000;

  OneEvaluation evaluation;
  evaluation.start(0, settings);

  CHECK(evaluation.request.nextWakeUpAtMs() == 1000);
  evaluation.timePasses(1);

  REQUIRE(evaluation.request.isFinished());
  CHECK(evaluation.report().durationMs == 1000);
  CHECK_FALSE(evaluation.report().extraTimeWasUsed);
}

TEST_CASE("a reading that arrives after the total time still ends the evaluation") {
  // The platform timer was late, and the reading arrived first.
  OneEvaluation evaluation;
  evaluation.start(0);

  evaluation.gpsDelivers(12.5, readingNorth(12.5, 20, 8));

  REQUIRE(evaluation.request.isFinished());
  CHECK(hasReason(evaluation.report(), ReasonCode::OverallDeadlineReached));
  CHECK(evaluation.report().hasPosition);  // what is in memory is used
  CHECK(evaluation.report().readingsReceived == 1);
}

TEST_CASE("times that make no sense count as zero") {
  SUBCASE("no total time: finished at the start") {
    EvaluationSettings settings = usualSettings();
    settings.totalTimeAllowedMs = -5;

    OneEvaluation evaluation;
    evaluation.start(3, settings);

    REQUIRE(evaluation.request.isFinished());
    CHECK(evaluation.report().durationMs == 0);
    CHECK(hasReason(evaluation.report(), ReasonCode::OverallDeadlineReached));
  }

  SUBCASE("no first attempt: the extra time starts at once") {
    EvaluationSettings settings = usualSettings();
    settings.firstAttemptTimeMs = 0;

    OneEvaluation evaluation;
    evaluation.start(0, settings);

    CHECK_FALSE(evaluation.request.isFinished());
    CHECK(evaluation.request.nextWakeUpAtMs() == 7000);

    evaluation.gpsDelivers(1, readingNorth(1, 20, 8));
    REQUIRE(evaluation.request.isFinished());
    CHECK(evaluation.report().source == FixSource::Recovered);
    CHECK(evaluation.report().firstAttemptTimedOut);
  }

  SUBCASE("a negative extra time is no extra time") {
    EvaluationSettings settings = usualSettings();
    settings.extraTimeMs = -1;

    OneEvaluation evaluation;
    evaluation.start(0, settings);
    evaluation.timePasses(4.5);

    CHECK(evaluation.request.isFinished());
  }
}

TEST_CASE("the timer firing early or more than once is harmless") {
  OneEvaluation evaluation;
  evaluation.start(0);

  evaluation.timePasses(1);
  evaluation.timePasses(1);
  evaluation.timePasses(4.4);
  CHECK_FALSE(evaluation.request.isFinished());
  CHECK(evaluation.request.nextWakeUpAtMs() == 4500);

  evaluation.timePasses(4.5);
  evaluation.timePasses(4.5);
  CHECK(evaluation.request.nextWakeUpAtMs() == 11500);
}

// --- The GPS cannot be used at all ----------------------------------------------------------

TEST_CASE("PLT-003 PLT-004 without the permission the evaluation ends at the start") {
  struct Case {
    LocationPermission permission;
    ReasonCode reason;
  };
  const Case cases[] = {
      {LocationPermission::Denied, ReasonCode::PermissionDenied},
      {LocationPermission::NotDetermined, ReasonCode::PermissionNotDetermined},
      {LocationPermission::Restricted, ReasonCode::PermissionRestricted},
  };

  for (const Case& oneCase : cases) {
    EvaluationSettings settings = usualSettings();
    settings.device.locationPermission = oneCase.permission;

    OneEvaluation evaluation;
    evaluation.history.store(readingNorth(0, 20, 8), 0);  // a good reading in memory
    evaluation.start(1, settings);

    REQUIRE(evaluation.request.isFinished());
    CHECK_FALSE(evaluation.request.wantsLocationUpdates());  // the GPS is never turned on
    CHECK(evaluation.report().status == AssessmentStatus::Unavailable);
    CHECK_FALSE(evaluation.report().hasPosition);  // and the memory is not used
    CHECK(evaluation.report().score.qualityScore == 0);
    CHECK(hasReason(evaluation.report(), oneCase.reason));
    CHECK(evaluation.report().durationMs == 0);
    CHECK(stepsOf(evaluation.report()) == Steps{Started, AnswerFromHistory, Finished});
  }
}

TEST_CASE("REC-021 REC-022 when the GPS cannot be used, what is remembered still helps") {
  struct Case {
    const char* name;
    ReasonCode reason;
    void (*breakTheDevice)(DeviceState&);
  };
  const Case cases[] = {
      {"location is turned off", ReasonCode::LocationServicesDisabled,
       [](DeviceState& device) { device.locationIsTurnedOn = false; }},
      {"Google Play services are missing", ReasonCode::PlayServicesUnavailable,
       [](DeviceState& device) {
         device.locationProvider = LocationProviderState::PlayServicesMissing;
       }},
      {"the system cannot give locations", ReasonCode::NativeProviderUnavailable,
       [](DeviceState& device) { device.locationProvider = LocationProviderState::Unavailable; }},
      {"the app is not in the foreground", ReasonCode::ForegroundRequired,
       [](DeviceState& device) { device.appIsInForeground = false; }},
  };

  for (const Case& oneCase : cases) {
    CAPTURE(oneCase.name);
    EvaluationSettings settings = usualSettings();
    oneCase.breakTheDevice(settings.device);

    SUBCASE("nothing remembered: unavailable") {
      OneEvaluation evaluation;
      evaluation.start(0, settings);

      REQUIRE(evaluation.request.isFinished());
      CHECK_FALSE(evaluation.request.wantsLocationUpdates());
      CHECK(evaluation.report().status == AssessmentStatus::Unavailable);
      CHECK(evaluation.report().score.qualityScore == 0);
      CHECK(hasReason(evaluation.report(), oneCase.reason));
      CHECK(hasReason(evaluation.report(), ReasonCode::NoUsableFix));
    }

    SUBCASE("a recent reading remembered: degraded, with the reason kept") {
      OneEvaluation evaluation;
      evaluation.history.store(readingNorth(0, 20, 8), 0);
      evaluation.start(2, settings);

      REQUIRE(evaluation.request.isFinished());
      CHECK(evaluation.report().status == AssessmentStatus::Degraded);
      CHECK(evaluation.report().source == FixSource::Cached);
      CHECK(evaluation.report().hasPosition);
      CHECK(hasReason(evaluation.report(), oneCase.reason));
    }
  }
}

TEST_CASE("MOT-010 MOT-011 a phone that cannot report its activity does not stop anything") {
  struct Case {
    PhoneActivitySupport support;
    ReasonCode reason;
  };
  const Case cases[] = {
      {PhoneActivitySupport::PermissionDenied, ReasonCode::MotionPermissionDenied},
      {PhoneActivitySupport::NotSupported, ReasonCode::MotionUnavailable},
      {PhoneActivitySupport::PermissionNotAsked, ReasonCode::MotionUnavailable},
  };

  for (const Case& oneCase : cases) {
    EvaluationSettings settings = usualSettings();
    settings.asked.usePhoneActivity = true;
    settings.device.phoneActivity = oneCase.support;

    OneEvaluation evaluation;
    evaluation.start(0, settings);

    CHECK_FALSE(evaluation.request.isFinished());
    CHECK_FALSE(evaluation.request.wantsPhoneActivityUpdates());  // nothing is asked from the user

    evaluation.gpsDelivers(1, readingNorth(1, 20, 8));
    REQUIRE(evaluation.request.isFinished());
    CHECK(evaluation.report().status == AssessmentStatus::Available);
    CHECK(hasReason(evaluation.report(), oneCase.reason));
  }
}

TEST_CASE("phone activity updates are wanted only while they can be used") {
  EvaluationSettings settings = usualSettings();
  settings.asked.usePhoneActivity = true;
  settings.device.phoneActivity = PhoneActivitySupport::Available;

  OneEvaluation evaluation;
  CHECK_FALSE(evaluation.request.wantsPhoneActivityUpdates());  // not started yet

  evaluation.start(0, settings);
  CHECK(evaluation.request.wantsPhoneActivityUpdates());
  CHECK_FALSE(hasReason(evaluation.report(), ReasonCode::MotionUnavailable));

  evaluation.stop(1, WhyTheEvaluationStopped::CancelledByTheApp);
  CHECK_FALSE(evaluation.request.wantsPhoneActivityUpdates());

  // The app does not want it: never asked for.
  OneEvaluation withoutIt;
  withoutIt.start(0, usualSettings());
  CHECK_FALSE(withoutIt.request.wantsPhoneActivityUpdates());
  withoutIt.gpsDelivers(1, readingNorth(1, 20, 8));
  CHECK_FALSE(hasReason(withoutIt.report(), ReasonCode::MotionUnavailable));
}

// --- Stopped from the outside ---------------------------------------------------------------

TEST_CASE("CON-004 CON-009 CON-010 CON-007 CON-021 the ways an evaluation is stopped") {
  struct Case {
    WhyTheEvaluationStopped why;
    AssessmentStatus status;
    ReasonCode reason;
  };
  const Case cases[] = {
      {WhyTheEvaluationStopped::CancelledByTheApp, AssessmentStatus::Cancelled,
       ReasonCode::RequestCancelled},
      {WhyTheEvaluationStopped::AppWentToBackground, AssessmentStatus::Cancelled,
       ReasonCode::AppBackgrounded},
      {WhyTheEvaluationStopped::SessionWasClosed, AssessmentStatus::Cancelled,
       ReasonCode::SessionStopped},
      {WhyTheEvaluationStopped::LocationPermissionWasLost, AssessmentStatus::Unavailable,
       ReasonCode::PermissionDenied},
  };

  for (const Case& oneCase : cases) {
    SUBCASE("during the first attempt") {
      OneEvaluation evaluation;
      evaluation.history.store(readingNorth(0, 20, 8), 0);
      evaluation.start(0);
      evaluation.stop(2, oneCase.why);

      REQUIRE(evaluation.request.isFinished());
      CHECK_FALSE(evaluation.request.wantsLocationUpdates());
      CHECK(evaluation.report().status == oneCase.status);
      CHECK_FALSE(evaluation.report().hasPosition);  // nothing remembered is handed out
      CHECK(evaluation.report().score.qualityScore == 0);
      CHECK(evaluation.report().relation == EvidenceRelation::Inconclusive);
      CHECK(evaluation.report().radiusMeters == 100.0);
      CHECK(evaluation.report().permissionPrecision == PermissionPrecision::Precise);
      CHECK(hasReason(evaluation.report(), oneCase.reason));
    }

    SUBCASE("during the extra time") {
      OneEvaluation evaluation;
      evaluation.start(0);
      evaluation.timePasses(4.5);
      evaluation.stop(9.5, oneCase.why);

      REQUIRE(evaluation.request.isFinished());
      CHECK(evaluation.report().status == oneCase.status);
      CHECK(evaluation.report().extraTimeWasUsed);
      CHECK(evaluation.report().durationMs == 9500);
      CHECK(hasReason(evaluation.report(), oneCase.reason));
      CHECK(hasReason(evaluation.report(), ReasonCode::InitialFixTimeout));
      CHECK(stepsOf(evaluation.report()) == Steps{Started, FirstAttempt, ExtraTime, Finished});
    }
  }
}

TEST_CASE("REC-025 the GPS breaks, but a recent reading is remembered") {
  OneEvaluation evaluation;
  evaluation.history.store(readingNorth(0, 20, 8), 0);
  evaluation.start(3);

  evaluation.stop(4, WhyTheEvaluationStopped::LocationProviderFailed);

  REQUIRE(evaluation.request.isFinished());
  const Assessment& report = evaluation.report();
  CHECK(report.status == AssessmentStatus::Degraded);
  CHECK(report.source == FixSource::Cached);
  CHECK(report.hasPosition);
  CHECK(hasReason(report, ReasonCode::NativeProviderUnavailable));
  CHECK(stepsOf(report) == Steps{Started, FirstAttempt, AnswerFromHistory, Finished});

  // With nothing remembered, the same failure gives "unavailable".
  OneEvaluation withNothing;
  withNothing.start(3);
  withNothing.stop(4, WhyTheEvaluationStopped::LocationProviderFailed);
  CHECK(withNothing.report().status == AssessmentStatus::Unavailable);
  CHECK(hasReason(withNothing.report(), ReasonCode::NativeProviderUnavailable));
}

TEST_CASE("CON-019 CON-020 a timer and a reading that race give one answer") {
  SUBCASE("the timer first, then a good reading") {
    OneEvaluation evaluation;
    evaluation.start(0);
    evaluation.timePasses(4.5);
    evaluation.timePasses(11.5);  // finished: unavailable
    REQUIRE(evaluation.request.isFinished());

    evaluation.gpsDelivers(11.6, readingNorth(11.6, 20, 8));

    CHECK(evaluation.report().status == AssessmentStatus::Unavailable);
    CHECK_FALSE(evaluation.report().hasPosition);
    CHECK(evaluation.report().readingsReceived == 0);
  }

  SUBCASE("a good reading first, then the timer") {
    OneEvaluation evaluation;
    evaluation.start(0);
    evaluation.gpsDelivers(4.4, readingNorth(4.4, 20, 8));
    REQUIRE(evaluation.request.isFinished());

    evaluation.timePasses(4.5);
    evaluation.timePasses(12);

    CHECK(evaluation.report().status == AssessmentStatus::Available);
    CHECK(evaluation.report().durationMs == 4400);
    CHECK_FALSE(evaluation.report().firstAttemptTimedOut);
    CHECK_FALSE(hasReason(evaluation.report(), ReasonCode::InitialFixTimeout));
  }
}

// --- Readings that were thrown away ------------------------------------------------------------

TEST_CASE("REC-028 readings with a broken time leave nothing to use") {
  OneEvaluation evaluation;
  evaluation.start(0);

  evaluation.gpsDeliversABrokenReading(1, ReasonCode::LocationAgeUnreliable);

  // Something arrived, but it was not good: the extra time starts.
  CHECK_FALSE(evaluation.request.isFinished());
  CHECK(evaluation.request.nextWakeUpAtMs() == 8000);

  evaluation.timePasses(8);
  REQUIRE(evaluation.request.isFinished());

  const Assessment& report = evaluation.report();
  CHECK(report.status == AssessmentStatus::Unavailable);
  CHECK(report.score.qualityScore == 0);
  CHECK(report.ageReliability == AgeReliability::Unreliable);
  CHECK(report.readingsReceived == 1);
  CHECK(report.readingsKept == 0);
  CHECK(report.fallbackCause == ReasonCode::NoUsableFix);
  CHECK(hasReason(report, ReasonCode::LocationAgeUnreliable));
}

TEST_CASE("a reading with a time before the clock started also makes the age unreliable") {
  OneEvaluation evaluation;
  evaluation.start(0);
  evaluation.gpsDeliversABrokenReading(1, ReasonCode::LocationInvalidTimestamp);
  evaluation.timePasses(8);

  REQUIRE(evaluation.request.isFinished());
  CHECK(evaluation.report().ageReliability == AgeReliability::Unreliable);
}

TEST_CASE("a reading thrown away for another reason leaves the age alone") {
  OneEvaluation evaluation;
  evaluation.start(0);
  evaluation.gpsDeliversABrokenReading(1, ReasonCode::LocationInvalidCoordinates);
  evaluation.timePasses(8);

  REQUIRE(evaluation.request.isFinished());
  CHECK(evaluation.report().status == AssessmentStatus::Unavailable);
  CHECK(evaluation.report().ageReliability == AgeReliability::NotApplicable);
}

TEST_CASE("a broken reading among good ones does not spoil the age") {
  OneEvaluation evaluation;
  evaluation.start(0);
  evaluation.gpsDeliversABrokenReading(1, ReasonCode::LocationInvalidTimestamp);
  evaluation.gpsDelivers(2, readingNorth(2, 20, 8));

  REQUIRE(evaluation.request.isFinished());
  CHECK(evaluation.report().status == AssessmentStatus::Available);
  CHECK(evaluation.report().ageReliability == AgeReliability::Monotonic);
  CHECK(evaluation.report().readingsReceived == 2);
  CHECK(evaluation.report().readingsKept == 1);
  CHECK(hasReason(evaluation.report(), ReasonCode::LocationInvalidTimestamp));
}

TEST_CASE("a delivery with nothing in it does not start the extra time") {
  OneEvaluation evaluation;
  evaluation.start(0);

  NotesAboutNewReadings nothing;
  evaluation.request.whenReadingsArrive(evaluation.history, evaluation.phoneActivity, 1000,
                                        nothing);

  CHECK_FALSE(evaluation.request.isFinished());
  CHECK(evaluation.request.nextWakeUpAtMs() == 4500);
}

TEST_CASE("the reason the first attempt was not enough is the most telling one") {
  SUBCASE("a reading marked as simulated") {
    LocationReading simulated = readingNorth(1, 20, 8);
    simulated.mockSignal = MockSignal::FlaggedByOs;

    OneEvaluation evaluation;
    evaluation.start(0);
    evaluation.gpsDelivers(1, simulated);

    CHECK_FALSE(evaluation.request.isFinished());
    evaluation.timePasses(8);
    CHECK(evaluation.report().fallbackCause == ReasonCode::PossibleMockLocation);
    CHECK(evaluation.report().score.qualityScore == 0);
  }

  SUBCASE("an approximate location") {
    LocationReading blurred = readingNorth(1, 20, 8);
    blurred.onlyApproximatePermission = true;

    OneEvaluation evaluation;
    evaluation.start(0);
    evaluation.gpsDelivers(1, blurred);

    evaluation.timePasses(8);
    CHECK(evaluation.report().fallbackCause == ReasonCode::PrecisionReduced);
  }

  SUBCASE("an old reading handed over by the system") {
    OneEvaluation evaluation;
    evaluation.start(30);
    evaluation.gpsDelivers(31, readingNorth(11, 20, 8));  // 20 seconds old

    CHECK_FALSE(evaluation.request.isFinished());
    evaluation.timePasses(38);
    CHECK(evaluation.report().fallbackCause == ReasonCode::LocationStale);
    CHECK(evaluation.report().source == FixSource::Cached);
  }
}

// --- A request that was never started -------------------------------------------------------------

TEST_CASE("a request that was never started is finished and wants nothing") {
  EvaluationRequest request;

  CHECK(request.isFinished());
  CHECK_FALSE(request.wantsLocationUpdates());
  CHECK_FALSE(request.wantsPhoneActivityUpdates());
  CHECK(request.report().status == AssessmentStatus::Unavailable);
}

TEST_CASE("starting again forgets the evaluation before it") {
  OneEvaluation evaluation;
  evaluation.start(0);
  evaluation.gpsDelivers(1, readingNorth(1, 20, 500));
  evaluation.timePasses(8);
  REQUIRE(evaluation.request.isFinished());
  REQUIRE(evaluation.report().extraTimeWasUsed);

  // The same object is used for a new evaluation.
  evaluation.request.start(8, 3, usualSettings(), evaluation.history, evaluation.phoneActivity,
                           20000);

  CHECK_FALSE(evaluation.request.isFinished());
  CHECK(evaluation.request.requestId() == 8);
  CHECK(evaluation.request.sessionId() == 3);
  CHECK(evaluation.request.nextWakeUpAtMs() == 24500);

  evaluation.gpsDelivers(21, readingNorth(21, 20, 8));
  REQUIRE(evaluation.request.isFinished());
  CHECK(evaluation.report().requestId == 8);
  CHECK(evaluation.report().sessionId == 3);
  CHECK_FALSE(evaluation.report().extraTimeWasUsed);
  CHECK(evaluation.report().readingsReceived == 1);
  CHECK(evaluation.report().durationMs == 1000);
  CHECK_FALSE(hasReason(evaluation.report(), ReasonCode::RecoveryWindowTimeout));
}

// --- Properties ---------------------------------------------------------------------------------------

TEST_CASE("NFR-04 whatever happens, an evaluation ends by its total time and only once") {
  DeterministicRng rng(1204);

  for (int round = 0; round < 3000; ++round) {
    EvaluationSettings settings = usualSettings();
    settings.totalTimeAllowedMs = static_cast<std::int64_t>(rng.below(20000));
    settings.firstAttemptTimeMs = static_cast<std::int64_t>(rng.below(8000));
    settings.extraTimeMs = static_cast<std::int64_t>(rng.below(10000));

    OneEvaluation evaluation;
    evaluation.start(0, settings);

    // A random mix of readings (good and weak), timers and the odd stop.
    double seconds = 0.0;
    bool sawItFinished = evaluation.request.isFinished();
    Assessment reportWhenItFinished = evaluation.report();

    for (int event = 0; event < 12; ++event) {
      seconds += rng.uniform(0.0, 3.0);
      const std::uint64_t kind = rng.below(10);

      if (kind < 4) {
        const double gpsError = (rng.below(2) == 0) ? 8.0 : 300.0;
        evaluation.gpsDelivers(seconds, readingNorth(seconds, rng.uniform(0.0, 50.0), gpsError));
      } else if (kind < 9) {
        evaluation.timePasses(seconds);
      } else {
        evaluation.stop(seconds, WhyTheEvaluationStopped::AppWentToBackground);
      }

      if (evaluation.request.isFinished()) {
        if (!sawItFinished) {
          sawItFinished = true;
          reportWhenItFinished = evaluation.report();
        }
        // Finished means finished: nothing changes the report afterwards.
        REQUIRE(evaluation.report().status == reportWhenItFinished.status);
        REQUIRE(evaluation.report().durationMs == reportWhenItFinished.durationMs);
        REQUIRE(evaluation.report().score.qualityScore ==
                reportWhenItFinished.score.qualityScore);
        REQUIRE(evaluation.report().howManySteps == reportWhenItFinished.howManySteps);
        REQUIRE_FALSE(evaluation.request.wantsLocationUpdates());
      } else {
        // While it runs, the next wake up is never after the total time.
        REQUIRE(evaluation.request.nextWakeUpAtMs() <= settings.totalTimeAllowedMs);
      }
    }

    // One timer at the end of the total time finishes whatever is left.
    evaluation.timePasses(static_cast<double>(settings.totalTimeAllowedMs) / 1000.0 + 0.001);
    REQUIRE(evaluation.request.isFinished());

    // The last step of every report is "finished".
    const Assessment& report = evaluation.report();
    REQUIRE(report.howManySteps >= 2);
    REQUIRE(report.steps[report.howManySteps - 1] == EvaluationStep::Finished);
  }
}
