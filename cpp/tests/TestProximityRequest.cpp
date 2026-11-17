// Tests of one request, from start to finish (ProximityRequest.cpp).
//
// The test plays the part of the engine and of the platform: it stores the
// readings, tells the request what happened, and says what time it is.
// No test waits for real time.
//
// The address is the starting point and the delivery radius is 100 meters.
// The usual times: first attempt 4.5 s, extra time 7 s, total 12 s.
#include "third_party/doctest.h"

#include <cstdint>
#include <vector>

#include "ProximityRequest.hpp"
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

RequestSettings usualSettings() {
  RequestSettings settings;
  settings.asked.destination = Coordinates{0.0, 0.0};
  settings.asked.radiusMeters = 100.0;
  settings.asked.usePhoneActivity = false;
  settings.device = phoneWhereEverythingWorks();
  return settings;
}

// One request together with the memory it reads from.
struct PhoneWithOneRequest {
  LocationHistory history;
  PhoneActivityHistory phoneActivity;
  ProximityRequest request;

  void start(double seconds, const RequestSettings& settings = usualSettings()) {
    request.start(7, 0, settings, history, phoneActivity, secondsToMs(seconds));
  }

  // The GPS delivers one reading: it is stored, and the request is told.
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

  void stop(double seconds, WhyTheRequestStopped why) {
    request.stop(why, history, phoneActivity, secondsToMs(seconds));
  }

  const Report& report() const { return request.report(); }
};

bool hasReason(const Report& report, ReasonCode reason) {
  return report.reasons.contains(reason);
}

std::vector<RequestStep> stepsOf(const Report& report) {
  return std::vector<RequestStep>(report.steps.begin(),
                                     report.steps.begin() + report.howManySteps);
}

using Steps = std::vector<RequestStep>;
const RequestStep Started = RequestStep::Started;
const RequestStep FirstAttempt = RequestStep::FirstAttempt;
const RequestStep ExtraTime = RequestStep::ExtraTime;
const RequestStep AnswerFromHistory = RequestStep::AnswerFromHistory;
const RequestStep Finished = RequestStep::Finished;

}  // namespace

// --- The examples at the top of ProximityRequest.cpp ----------------------------

TEST_CASE("REC-001 a good reading in the first attempt ends the request at once") {
  PhoneWithOneRequest phone;
  phone.start(0);

  CHECK_FALSE(phone.request.isFinished());
  CHECK(phone.request.wantsLocationUpdates());
  CHECK(phone.request.nextWakeUpAtMs() == 4500);

  phone.gpsDelivers(1.2, readingNorth(1.2, 20, 8));

  REQUIRE(phone.request.isFinished());
  CHECK_FALSE(phone.request.wantsLocationUpdates());

  const Report& report = phone.report();
  CHECK(report.status == ReportStatus::Available);
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
  PhoneWithOneRequest phone;
  phone.start(0);

  phone.gpsDelivers(1, readingNorth(1, 20, 500));       // GPS error of 500 meters

  CHECK_FALSE(phone.request.isFinished());
  CHECK(phone.request.wantsLocationUpdates());
  CHECK(phone.request.nextWakeUpAtMs() == 8000);       // 1 s + 7 s of extra time

  phone.gpsDelivers(3, readingNorth(3, 20, 8));

  REQUIRE(phone.request.isFinished());
  const Report& report = phone.report();
  CHECK(report.status == ReportStatus::Available);
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
  PhoneWithOneRequest phone;
  phone.start(0);

  phone.timePasses(4.5);       // the first attempt is over
  CHECK_FALSE(phone.request.isFinished());
  CHECK(phone.request.wantsLocationUpdates());
  CHECK(phone.request.nextWakeUpAtMs() == 11500);

  phone.timePasses(11.5);       // the extra time is over
  REQUIRE(phone.request.isFinished());
  CHECK_FALSE(phone.request.wantsLocationUpdates());

  const Report& report = phone.report();
  CHECK(report.status == ReportStatus::Unavailable);
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
  PhoneWithOneRequest phone;
  // 10 seconds before the request: a good reading, and since then the
  // phone reports "standing still".
  phone.history.store(readingNorth(0, 20, 8), 0);
  phone.phoneActivity.store(activityReport(PhoneActivity::StandingStill, 0, 30));

  RequestSettings settings = usualSettings();
  settings.asked.usePhoneActivity = true;
  settings.device.phoneActivity = PhoneActivitySupport::Available;

  phone.start(10, settings);
  phone.timePasses(14.5);       // the first attempt is over

  // No extra time is spent.
  REQUIRE(phone.request.isFinished());
  const Report& report = phone.report();
  CHECK(report.status == ReportStatus::Degraded);
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
  RequestSettings settings = usualSettings();
  settings.asked.usePhoneActivity = true;
  settings.device.phoneActivity = PhoneActivitySupport::Available;

  SUBCASE("the phone stood still, but the reading is imprecise") {
    PhoneWithOneRequest phone;
    phone.history.store(readingNorth(0, 20, 200), 0);
    phone.phoneActivity.store(activityReport(PhoneActivity::StandingStill, 0, 60));

    phone.start(10, settings);
    phone.timePasses(14.5);

    CHECK_FALSE(phone.request.isFinished());       // the extra time is used
    CHECK(phone.request.nextWakeUpAtMs() == 21500);
  }

  SUBCASE("the phone stood still, but the reading is marked as simulated") {
    LocationReading simulated = readingNorth(0, 20, 8);
    simulated.mockSignal = MockSignal::FlaggedByOs;

    PhoneWithOneRequest phone;
    phone.history.store(simulated, 0);
    phone.phoneActivity.store(activityReport(PhoneActivity::StandingStill, 0, 60));

    phone.start(10, settings);
    phone.timePasses(14.5);

    CHECK_FALSE(phone.request.isFinished());
  }

  SUBCASE("the phone stood still, but the reading is probably a GPS jump") {
    PhoneWithOneRequest phone;
    phone.history.store(readingNorth(0, 0, 8), 0);
    phone.history.store(readingNorth(1, 0, 8), 1000);
    phone.history.store(readingNorth(70, 5000, 8), 70000);       // 5 km away
    phone.phoneActivity.store(activityReport(PhoneActivity::StandingStill, 70, 200));

    // At second 75.5 only the reading that jumped is recent enough.
    phone.start(71, settings);
    phone.timePasses(75.5);

    CHECK_FALSE(phone.request.isFinished());

    phone.timePasses(82.5);
    REQUIRE(phone.request.isFinished());
    CHECK(phone.report().status == ReportStatus::Degraded);
    CHECK(phone.report().score.wasLimitedBy(ScoreLimit::SuspectedJump));
  }

  SUBCASE("the phone did not stand still the whole time") {
    PhoneWithOneRequest phone;
    phone.history.store(readingNorth(0, 20, 8), 0);
    phone.phoneActivity.store(activityReport(PhoneActivity::StandingStill, 5, 60));

    phone.start(10, settings);
    phone.timePasses(14.5);

    CHECK_FALSE(phone.request.isFinished());
  }
}

TEST_CASE("CON-002 CON-005 CON-006 the app cancels; later events change nothing") {
  PhoneWithOneRequest phone;
  phone.start(0);

  phone.stop(2, WhyTheRequestStopped::CancelledByTheApp);

  REQUIRE(phone.request.isFinished());
  CHECK_FALSE(phone.request.wantsLocationUpdates());
  CHECK(phone.report().status == ReportStatus::Cancelled);
  CHECK_FALSE(phone.report().hasPosition);
  CHECK(phone.report().score.qualityScore == 0);
  CHECK(phone.report().durationMs == 2000);
  CHECK(hasReason(phone.report(), ReasonCode::RequestCancelled));
  CHECK(stepsOf(phone.report()) == Steps{Started, FirstAttempt, Finished});

  // A late reading, the timer, and a second cancel.
  phone.gpsDelivers(2.5, readingNorth(2.5, 20, 8));
  phone.timePasses(12);
  phone.stop(13, WhyTheRequestStopped::CancelledByTheApp);

  CHECK(phone.report().status == ReportStatus::Cancelled);
  CHECK(phone.report().durationMs == 2000);
  CHECK(phone.report().readingsReceived == 0);
  CHECK(stepsOf(phone.report()) == Steps{Started, FirstAttempt, Finished});
}

// --- Other ways the first attempt and the extra time can go ---------------------------

TEST_CASE("REC-002 a medium reading inside the limits is accepted") {
  PhoneWithOneRequest phone;
  phone.start(10);

  // Taken 5 seconds ago, GPS error 40: both inside the limits (10 s, 50 m).
  phone.gpsDelivers(10.5, readingNorth(5.5, 20, 40));

  REQUIRE(phone.request.isFinished());
  CHECK(phone.report().status == ReportStatus::Available);
  CHECK(phone.report().source == FixSource::Fresh);
  CHECK(phone.report().readingAgeMs == 5000);
}

TEST_CASE("REC-004 REC-009 nothing in the first attempt, a good reading in the extra time") {
  PhoneWithOneRequest phone;
  phone.start(0);
  phone.timePasses(4.5);

  phone.gpsDelivers(5.5, readingNorth(5.5, 20, 8));       // 1 second into the extra time

  // It stops right away: it does not wait for the 7 seconds.
  REQUIRE(phone.request.isFinished());
  const Report& report = phone.report();
  CHECK(report.status == ReportStatus::Available);
  CHECK(report.source == FixSource::Recovered);
  CHECK(report.firstAttemptTimedOut);
  CHECK(report.extraTimeWasUsed);
  CHECK(report.durationMs == 5500);
  CHECK(hasReason(report, ReasonCode::InitialFixTimeout));
  CHECK_FALSE(hasReason(report, ReasonCode::RecoveryWindowTimeout));
}

TEST_CASE("REC-007 nothing new and a remembered reading taken while in a vehicle") {
  PhoneWithOneRequest phone;
  phone.history.store(readingNorth(0, 20, 8), 0);
  phone.phoneActivity.store(activityReport(PhoneActivity::InVehicle, 0, 60));

  RequestSettings settings = usualSettings();
  settings.asked.usePhoneActivity = true;
  settings.device.phoneActivity = PhoneActivitySupport::Available;

  phone.start(30, settings);
  phone.timePasses(34.5);

  // The phone was moving: the remembered reading is not enough, so the
  // extra time is used.
  CHECK_FALSE(phone.request.isFinished());

  phone.timePasses(41.5);
  REQUIRE(phone.request.isFinished());

  const Report& report = phone.report();
  CHECK(report.status == ReportStatus::Degraded);
  CHECK(report.source == FixSource::Cached);
  CHECK(report.howFarPhoneCouldBeMeters == doctest::Approx(8.0 + 45.0 * 41.5));
  CHECK(report.relation == EvidenceRelation::Inconclusive);
  CHECK(report.score.qualityScore <= 15);
  CHECK(hasReason(report, ReasonCode::RecoveryWindowTimeout));
  CHECK(hasReason(report, ReasonCode::HistoryOnlyInconclusive));
}

TEST_CASE("REC-008 REC-010 only weak readings arrive") {
  PhoneWithOneRequest phone;
  phone.start(0);

  phone.gpsDelivers(1, readingNorth(1, 20, 200));
  phone.gpsDelivers(3, readingNorth(3, 20, 180));
  CHECK_FALSE(phone.request.isFinished());

  phone.timePasses(8);       // the extra time started at second 1
  REQUIRE(phone.request.isFinished());

  const Report& report = phone.report();
  CHECK(report.status == ReportStatus::Degraded);
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
  PhoneWithOneRequest phone;
  phone.start(0);

  phone.gpsDelivers(1, readingNorth(1, 20, 200));
  phone.timePasses(8);

  REQUIRE(phone.request.isFinished());
  CHECK(phone.report().source == FixSource::Fresh);
  CHECK(phone.report().status == ReportStatus::Degraded);
}

TEST_CASE("REC-016 with no extra time, the first attempt is all there is") {
  RequestSettings settings = usualSettings();
  settings.extraTimeMs = 0;

  SUBCASE("nothing arrives") {
    PhoneWithOneRequest phone;
    phone.history.store(readingNorth(0, 20, 8), 0);
    phone.start(5, settings);

    phone.timePasses(9.5);

    REQUIRE(phone.request.isFinished());
    CHECK(phone.report().source == FixSource::Cached);
    CHECK(phone.report().status == ReportStatus::Degraded);
    CHECK_FALSE(phone.report().extraTimeWasUsed);
    CHECK(stepsOf(phone.report()) ==
          Steps{Started, FirstAttempt, AnswerFromHistory, Finished});
  }

  SUBCASE("a weak reading does not end the first attempt early") {
    PhoneWithOneRequest phone;
    phone.start(0, settings);

    phone.gpsDelivers(1, readingNorth(1, 20, 200));
    CHECK_FALSE(phone.request.isFinished());
    CHECK(phone.request.nextWakeUpAtMs() == 4500);

    // A good one can still arrive before the first attempt ends.
    phone.gpsDelivers(2, readingNorth(2, 20, 8));
    REQUIRE(phone.request.isFinished());
    CHECK(phone.report().status == ReportStatus::Available);
    CHECK(phone.report().source == FixSource::Fresh);
  }
}

// --- The total time ---------------------------------------------------------------------

TEST_CASE("REC-012 the total time ends during the first attempt") {
  RequestSettings settings = usualSettings();
  settings.totalTimeAllowedMs = 3000;  // shorter than the first attempt

  PhoneWithOneRequest phone;
  phone.start(0, settings);
  CHECK(phone.request.nextWakeUpAtMs() == 3000);

  phone.timePasses(3);

  REQUIRE(phone.request.isFinished());
  const Report& report = phone.report();
  CHECK(report.status == ReportStatus::Unavailable);
  CHECK(report.firstAttemptTimedOut);
  CHECK_FALSE(report.extraTimeWasUsed);
  CHECK(report.durationMs == 3000);
  CHECK(hasReason(report, ReasonCode::OverallDeadlineReached));
  CHECK(hasReason(report, ReasonCode::InitialFixTimeout));
  CHECK_FALSE(hasReason(report, ReasonCode::RecoveryWindowTimeout));
}

TEST_CASE("REC-014 the total time ends during the extra time") {
  RequestSettings settings = usualSettings();
  settings.totalTimeAllowedMs = 6000;

  PhoneWithOneRequest phone;
  phone.start(0, settings);
  phone.timePasses(4.5);

  // The extra time would last until 11.5, but everything must end at 6.
  CHECK(phone.request.nextWakeUpAtMs() == 6000);

  phone.gpsDelivers(5, readingNorth(5, 20, 300));       // weak: keeps waiting
  phone.timePasses(6);

  REQUIRE(phone.request.isFinished());
  const Report& report = phone.report();
  CHECK(report.status == ReportStatus::Degraded);      // what arrived is used
  CHECK(report.gpsErrorMeters == 300.0);
  CHECK(report.durationMs == 6000);
  CHECK(hasReason(report, ReasonCode::OverallDeadlineReached));
  CHECK_FALSE(hasReason(report, ReasonCode::RecoveryWindowTimeout));
}

TEST_CASE("REC-015 a short total time never becomes a long wait") {
  RequestSettings settings = usualSettings();
  settings.totalTimeAllowedMs = 1000;

  PhoneWithOneRequest phone;
  phone.start(0, settings);

  CHECK(phone.request.nextWakeUpAtMs() == 1000);
  phone.timePasses(1);

  REQUIRE(phone.request.isFinished());
  CHECK(phone.report().durationMs == 1000);
  CHECK_FALSE(phone.report().extraTimeWasUsed);
}

TEST_CASE("a reading that arrives after the total time still ends the request") {
  // The platform timer was late, and the reading arrived first.
  PhoneWithOneRequest phone;
  phone.start(0);

  phone.gpsDelivers(12.5, readingNorth(12.5, 20, 8));

  REQUIRE(phone.request.isFinished());
  CHECK(hasReason(phone.report(), ReasonCode::OverallDeadlineReached));
  CHECK(phone.report().hasPosition);       // what is in memory is used
  CHECK(phone.report().readingsReceived == 1);
}

TEST_CASE("times that make no sense count as zero") {
  SUBCASE("no total time: finished at the start") {
    RequestSettings settings = usualSettings();
    settings.totalTimeAllowedMs = -5;

    PhoneWithOneRequest phone;
    phone.start(3, settings);

    REQUIRE(phone.request.isFinished());
    CHECK(phone.report().durationMs == 0);
    CHECK(hasReason(phone.report(), ReasonCode::OverallDeadlineReached));
  }

  SUBCASE("no first attempt: the extra time starts at once") {
    RequestSettings settings = usualSettings();
    settings.firstAttemptTimeMs = 0;

    PhoneWithOneRequest phone;
    phone.start(0, settings);

    CHECK_FALSE(phone.request.isFinished());
    CHECK(phone.request.nextWakeUpAtMs() == 7000);

    phone.gpsDelivers(1, readingNorth(1, 20, 8));
    REQUIRE(phone.request.isFinished());
    CHECK(phone.report().source == FixSource::Recovered);
    CHECK(phone.report().firstAttemptTimedOut);
  }

  SUBCASE("a negative extra time is no extra time") {
    RequestSettings settings = usualSettings();
    settings.extraTimeMs = -1;

    PhoneWithOneRequest phone;
    phone.start(0, settings);
    phone.timePasses(4.5);

    CHECK(phone.request.isFinished());
  }
}

TEST_CASE("the timer firing early or more than once is harmless") {
  PhoneWithOneRequest phone;
  phone.start(0);

  phone.timePasses(1);
  phone.timePasses(1);
  phone.timePasses(4.4);
  CHECK_FALSE(phone.request.isFinished());
  CHECK(phone.request.nextWakeUpAtMs() == 4500);

  phone.timePasses(4.5);
  phone.timePasses(4.5);
  CHECK(phone.request.nextWakeUpAtMs() == 11500);
}

// --- The GPS cannot be used at all ----------------------------------------------------------

TEST_CASE("PLT-003 PLT-004 without the permission the request ends at the start") {
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
    RequestSettings settings = usualSettings();
    settings.device.locationPermission = oneCase.permission;

    PhoneWithOneRequest phone;
    phone.history.store(readingNorth(0, 20, 8), 0);       // a good reading in memory
    phone.start(1, settings);

    REQUIRE(phone.request.isFinished());
    CHECK_FALSE(phone.request.wantsLocationUpdates());       // the GPS is never turned on
    CHECK(phone.report().status == ReportStatus::Unavailable);
    CHECK_FALSE(phone.report().hasPosition);       // and the memory is not used
    CHECK(phone.report().score.qualityScore == 0);
    CHECK(hasReason(phone.report(), oneCase.reason));
    CHECK(phone.report().durationMs == 0);
    CHECK(stepsOf(phone.report()) == Steps{Started, AnswerFromHistory, Finished});
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
    RequestSettings settings = usualSettings();
    oneCase.breakTheDevice(settings.device);

    SUBCASE("nothing remembered: unavailable") {
      PhoneWithOneRequest phone;
      phone.start(0, settings);

      REQUIRE(phone.request.isFinished());
      CHECK_FALSE(phone.request.wantsLocationUpdates());
      CHECK(phone.report().status == ReportStatus::Unavailable);
      CHECK(phone.report().score.qualityScore == 0);
      CHECK(hasReason(phone.report(), oneCase.reason));
      CHECK(hasReason(phone.report(), ReasonCode::NoUsableFix));
    }

    SUBCASE("a recent reading remembered: degraded, with the reason kept") {
      PhoneWithOneRequest phone;
      phone.history.store(readingNorth(0, 20, 8), 0);
      phone.start(2, settings);

      REQUIRE(phone.request.isFinished());
      CHECK(phone.report().status == ReportStatus::Degraded);
      CHECK(phone.report().source == FixSource::Cached);
      CHECK(phone.report().hasPosition);
      CHECK(hasReason(phone.report(), oneCase.reason));
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
    RequestSettings settings = usualSettings();
    settings.asked.usePhoneActivity = true;
    settings.device.phoneActivity = oneCase.support;

    PhoneWithOneRequest phone;
    phone.start(0, settings);

    CHECK_FALSE(phone.request.isFinished());
    CHECK_FALSE(phone.request.wantsPhoneActivityUpdates());       // nothing is asked from the user

    phone.gpsDelivers(1, readingNorth(1, 20, 8));
    REQUIRE(phone.request.isFinished());
    CHECK(phone.report().status == ReportStatus::Available);
    CHECK(hasReason(phone.report(), oneCase.reason));
  }
}

TEST_CASE("phone activity updates are wanted only while they can be used") {
  RequestSettings settings = usualSettings();
  settings.asked.usePhoneActivity = true;
  settings.device.phoneActivity = PhoneActivitySupport::Available;

  PhoneWithOneRequest phone;
  CHECK_FALSE(phone.request.wantsPhoneActivityUpdates());       // not started yet

  phone.start(0, settings);
  CHECK(phone.request.wantsPhoneActivityUpdates());
  CHECK_FALSE(hasReason(phone.report(), ReasonCode::MotionUnavailable));

  phone.stop(1, WhyTheRequestStopped::CancelledByTheApp);
  CHECK_FALSE(phone.request.wantsPhoneActivityUpdates());

  // The app does not want it: never asked for.
  PhoneWithOneRequest withoutIt;
  withoutIt.start(0, usualSettings());
  CHECK_FALSE(withoutIt.request.wantsPhoneActivityUpdates());
  withoutIt.gpsDelivers(1, readingNorth(1, 20, 8));
  CHECK_FALSE(hasReason(withoutIt.report(), ReasonCode::MotionUnavailable));
}

// --- Stopped from the outside ---------------------------------------------------------------

TEST_CASE("CON-004 CON-009 CON-010 CON-007 CON-021 the ways a request is stopped") {
  struct Case {
    WhyTheRequestStopped why;
    ReportStatus status;
    ReasonCode reason;
  };
  const Case cases[] = {
      {WhyTheRequestStopped::CancelledByTheApp, ReportStatus::Cancelled,
       ReasonCode::RequestCancelled},
      {WhyTheRequestStopped::AppWentToBackground, ReportStatus::Cancelled,
       ReasonCode::AppBackgrounded},
      {WhyTheRequestStopped::SessionWasClosed, ReportStatus::Cancelled,
       ReasonCode::SessionStopped},
      {WhyTheRequestStopped::LocationPermissionWasLost, ReportStatus::Unavailable,
       ReasonCode::PermissionDenied},
  };

  for (const Case& oneCase : cases) {
    SUBCASE("during the first attempt") {
      PhoneWithOneRequest phone;
      phone.history.store(readingNorth(0, 20, 8), 0);
      phone.start(0);
      phone.stop(2, oneCase.why);

      REQUIRE(phone.request.isFinished());
      CHECK_FALSE(phone.request.wantsLocationUpdates());
      CHECK(phone.report().status == oneCase.status);
      CHECK_FALSE(phone.report().hasPosition);       // nothing remembered is handed out
      CHECK(phone.report().score.qualityScore == 0);
      CHECK(phone.report().relation == EvidenceRelation::Inconclusive);
      CHECK(phone.report().radiusMeters == 100.0);
      CHECK(phone.report().permissionPrecision == PermissionPrecision::Precise);
      CHECK(hasReason(phone.report(), oneCase.reason));
    }

    SUBCASE("during the extra time") {
      PhoneWithOneRequest phone;
      phone.start(0);
      phone.timePasses(4.5);
      phone.stop(9.5, oneCase.why);

      REQUIRE(phone.request.isFinished());
      CHECK(phone.report().status == oneCase.status);
      CHECK(phone.report().extraTimeWasUsed);
      CHECK(phone.report().durationMs == 9500);
      CHECK(hasReason(phone.report(), oneCase.reason));
      CHECK(hasReason(phone.report(), ReasonCode::InitialFixTimeout));
      CHECK(stepsOf(phone.report()) == Steps{Started, FirstAttempt, ExtraTime, Finished});
    }
  }
}

TEST_CASE("REC-025 the GPS breaks, but a recent reading is remembered") {
  PhoneWithOneRequest phone;
  phone.history.store(readingNorth(0, 20, 8), 0);
  phone.start(3);

  phone.stop(4, WhyTheRequestStopped::LocationProviderFailed);

  REQUIRE(phone.request.isFinished());
  const Report& report = phone.report();
  CHECK(report.status == ReportStatus::Degraded);
  CHECK(report.source == FixSource::Cached);
  CHECK(report.hasPosition);
  CHECK(hasReason(report, ReasonCode::NativeProviderUnavailable));
  CHECK(stepsOf(report) == Steps{Started, FirstAttempt, AnswerFromHistory, Finished});

  // With nothing remembered, the same failure gives "unavailable".
  PhoneWithOneRequest withNothing;
  withNothing.start(3);
  withNothing.stop(4, WhyTheRequestStopped::LocationProviderFailed);
  CHECK(withNothing.report().status == ReportStatus::Unavailable);
  CHECK(hasReason(withNothing.report(), ReasonCode::NativeProviderUnavailable));
}

TEST_CASE("CON-019 CON-020 a timer and a reading that race give one answer") {
  SUBCASE("the timer first, then a good reading") {
    PhoneWithOneRequest phone;
    phone.start(0);
    phone.timePasses(4.5);
    phone.timePasses(11.5);       // finished: unavailable
    REQUIRE(phone.request.isFinished());

    phone.gpsDelivers(11.6, readingNorth(11.6, 20, 8));

    CHECK(phone.report().status == ReportStatus::Unavailable);
    CHECK_FALSE(phone.report().hasPosition);
    CHECK(phone.report().readingsReceived == 0);
  }

  SUBCASE("a good reading first, then the timer") {
    PhoneWithOneRequest phone;
    phone.start(0);
    phone.gpsDelivers(4.4, readingNorth(4.4, 20, 8));
    REQUIRE(phone.request.isFinished());

    phone.timePasses(4.5);
    phone.timePasses(12);

    CHECK(phone.report().status == ReportStatus::Available);
    CHECK(phone.report().durationMs == 4400);
    CHECK_FALSE(phone.report().firstAttemptTimedOut);
    CHECK_FALSE(hasReason(phone.report(), ReasonCode::InitialFixTimeout));
  }
}

// --- Readings that were thrown away ------------------------------------------------------------

TEST_CASE("REC-028 readings with a broken time leave nothing to use") {
  PhoneWithOneRequest phone;
  phone.start(0);

  phone.gpsDeliversABrokenReading(1, ReasonCode::LocationAgeUnreliable);

  // Something arrived, but it was not good: the extra time starts.
  CHECK_FALSE(phone.request.isFinished());
  CHECK(phone.request.nextWakeUpAtMs() == 8000);

  phone.timePasses(8);
  REQUIRE(phone.request.isFinished());

  const Report& report = phone.report();
  CHECK(report.status == ReportStatus::Unavailable);
  CHECK(report.score.qualityScore == 0);
  CHECK(report.ageReliability == AgeReliability::Unreliable);
  CHECK(report.readingsReceived == 1);
  CHECK(report.readingsKept == 0);
  CHECK(report.fallbackCause == ReasonCode::NoUsableFix);
  CHECK(hasReason(report, ReasonCode::LocationAgeUnreliable));
}

TEST_CASE("a reading with a time before the clock started also makes the age unreliable") {
  PhoneWithOneRequest phone;
  phone.start(0);
  phone.gpsDeliversABrokenReading(1, ReasonCode::LocationInvalidTimestamp);
  phone.timePasses(8);

  REQUIRE(phone.request.isFinished());
  CHECK(phone.report().ageReliability == AgeReliability::Unreliable);
}

TEST_CASE("a reading thrown away for another reason leaves the age alone") {
  PhoneWithOneRequest phone;
  phone.start(0);
  phone.gpsDeliversABrokenReading(1, ReasonCode::LocationInvalidCoordinates);
  phone.timePasses(8);

  REQUIRE(phone.request.isFinished());
  CHECK(phone.report().status == ReportStatus::Unavailable);
  CHECK(phone.report().ageReliability == AgeReliability::NotApplicable);
}

TEST_CASE("a broken reading among good ones does not spoil the age") {
  PhoneWithOneRequest phone;
  phone.start(0);
  phone.gpsDeliversABrokenReading(1, ReasonCode::LocationInvalidTimestamp);
  phone.gpsDelivers(2, readingNorth(2, 20, 8));

  REQUIRE(phone.request.isFinished());
  CHECK(phone.report().status == ReportStatus::Available);
  CHECK(phone.report().ageReliability == AgeReliability::Monotonic);
  CHECK(phone.report().readingsReceived == 2);
  CHECK(phone.report().readingsKept == 1);
  CHECK(hasReason(phone.report(), ReasonCode::LocationInvalidTimestamp));
}

TEST_CASE("a delivery with nothing in it does not start the extra time") {
  PhoneWithOneRequest phone;
  phone.start(0);

  NotesAboutNewReadings nothing;
  phone.request.whenReadingsArrive(phone.history, phone.phoneActivity, 1000,
                                        nothing);

  CHECK_FALSE(phone.request.isFinished());
  CHECK(phone.request.nextWakeUpAtMs() == 4500);
}

TEST_CASE("the reason the first attempt was not enough is the most telling one") {
  SUBCASE("a reading marked as simulated") {
    LocationReading simulated = readingNorth(1, 20, 8);
    simulated.mockSignal = MockSignal::FlaggedByOs;

    PhoneWithOneRequest phone;
    phone.start(0);
    phone.gpsDelivers(1, simulated);

    CHECK_FALSE(phone.request.isFinished());
    phone.timePasses(8);
    CHECK(phone.report().fallbackCause == ReasonCode::PossibleMockLocation);
    CHECK(phone.report().score.qualityScore == 0);
  }

  SUBCASE("an approximate location") {
    LocationReading blurred = readingNorth(1, 20, 8);
    blurred.onlyApproximatePermission = true;

    PhoneWithOneRequest phone;
    phone.start(0);
    phone.gpsDelivers(1, blurred);

    phone.timePasses(8);
    CHECK(phone.report().fallbackCause == ReasonCode::PrecisionReduced);
  }

  SUBCASE("an old reading handed over by the system") {
    PhoneWithOneRequest phone;
    phone.start(30);
    phone.gpsDelivers(31, readingNorth(11, 20, 8));       // 20 seconds old

    CHECK_FALSE(phone.request.isFinished());
    phone.timePasses(38);
    CHECK(phone.report().fallbackCause == ReasonCode::LocationStale);
    CHECK(phone.report().source == FixSource::Cached);
  }
}

// --- A request that was never started -------------------------------------------------------------

TEST_CASE("a request that was never started is finished and wants nothing") {
  ProximityRequest request;

  CHECK(request.isFinished());
  CHECK_FALSE(request.wantsLocationUpdates());
  CHECK_FALSE(request.wantsPhoneActivityUpdates());
  CHECK(request.report().status == ReportStatus::Unavailable);
}

TEST_CASE("starting again forgets the request before it") {
  PhoneWithOneRequest phone;
  phone.start(0);
  phone.gpsDelivers(1, readingNorth(1, 20, 500));
  phone.timePasses(8);
  REQUIRE(phone.request.isFinished());
  REQUIRE(phone.report().extraTimeWasUsed);

  // The same object is used for a new request.
  phone.request.start(8, 3, usualSettings(), phone.history, phone.phoneActivity,
                           20000);

  CHECK_FALSE(phone.request.isFinished());
  CHECK(phone.request.requestId() == 8);
  CHECK(phone.request.sessionId() == 3);
  CHECK(phone.request.nextWakeUpAtMs() == 24500);

  phone.gpsDelivers(21, readingNorth(21, 20, 8));
  REQUIRE(phone.request.isFinished());
  CHECK(phone.report().requestId == 8);
  CHECK(phone.report().sessionId == 3);
  CHECK_FALSE(phone.report().extraTimeWasUsed);
  CHECK(phone.report().readingsReceived == 1);
  CHECK(phone.report().durationMs == 1000);
  CHECK_FALSE(hasReason(phone.report(), ReasonCode::RecoveryWindowTimeout));
}

// --- Properties ---------------------------------------------------------------------------------------

TEST_CASE("NFR-04 whatever happens, a request ends by its total time and only once") {
  DeterministicRng rng(1204);

  for (int round = 0; round < 3000; ++round) {
    RequestSettings settings = usualSettings();
    settings.totalTimeAllowedMs = static_cast<std::int64_t>(rng.below(20000));
    settings.firstAttemptTimeMs = static_cast<std::int64_t>(rng.below(8000));
    settings.extraTimeMs = static_cast<std::int64_t>(rng.below(10000));

    PhoneWithOneRequest phone;
    phone.start(0, settings);

    // A random mix of readings (good and weak), timers and the odd stop.
    double seconds = 0.0;
    bool sawItFinished = phone.request.isFinished();
    Report reportWhenItFinished = phone.report();

    for (int event = 0; event < 12; ++event) {
      seconds += rng.uniform(0.0, 3.0);
      const std::uint64_t kind = rng.below(10);

      if (kind < 4) {
        const double gpsError = (rng.below(2) == 0) ? 8.0 : 300.0;
        phone.gpsDelivers(seconds, readingNorth(seconds, rng.uniform(0.0, 50.0), gpsError));
      } else if (kind < 9) {
        phone.timePasses(seconds);
      } else {
        phone.stop(seconds, WhyTheRequestStopped::AppWentToBackground);
      }

      if (phone.request.isFinished()) {
        if (!sawItFinished) {
          sawItFinished = true;
          reportWhenItFinished = phone.report();
        }
        // Finished means finished: nothing changes the report afterwards.
        REQUIRE(phone.report().status == reportWhenItFinished.status);
        REQUIRE(phone.report().durationMs == reportWhenItFinished.durationMs);
        REQUIRE(phone.report().score.qualityScore ==
                reportWhenItFinished.score.qualityScore);
        REQUIRE(phone.report().howManySteps == reportWhenItFinished.howManySteps);
        REQUIRE_FALSE(phone.request.wantsLocationUpdates());
      } else {
        // While it runs, the next wake up is never after the total time.
        REQUIRE(phone.request.nextWakeUpAtMs() <= settings.totalTimeAllowedMs);
      }
    }

    // One timer at the end of the total time finishes whatever is left.
    phone.timePasses(static_cast<double>(settings.totalTimeAllowedMs) / 1000.0 + 0.001);
    REQUIRE(phone.request.isFinished());

    // The last step of every report is "finished".
    const Report& report = phone.report();
    REQUIRE(report.howManySteps >= 2);
    REQUIRE(report.steps[report.howManySteps - 1] == RequestStep::Finished);
  }
}
