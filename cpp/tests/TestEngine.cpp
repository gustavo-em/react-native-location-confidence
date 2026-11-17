// Tests of the engine (Engine.cpp) with a pretend phone.
//
// FakePhone plays the part of the platform code (Kotlin or Swift). After
// every call it does exactly what the real code will do:
//
//   - it takes the finished reports;
//   - it turns its pretend GPS on or off, as the engine says;
//   - it remembers when the engine wants to be woken up.
//
// Time only moves when a test says so, and the pretend GPS only delivers a
// reading while it is on. No test waits for real time.
//
// The address is the starting point and the delivery radius is 100 meters.
#include "third_party/doctest.h"

#include <cstdint>
#include <memory>
#include <vector>

#include "Engine.hpp"
#include "TestSupport.hpp"

using namespace lce;
using lce_test::DeterministicRng;
using lce_test::readingNorth;
using lce_test::secondsToMs;

namespace {

DeviceState phoneWhereEverythingWorks() {
  DeviceState device;
  device.locationPermission = LocationPermission::Granted;
  device.permissionPrecision = PermissionPrecision::Precise;
  device.locationIsTurnedOn = true;
  device.locationProvider = LocationProviderState::Available;
  device.appIsInForeground = true;
  device.phoneActivity = PhoneActivitySupport::Available;
  return device;
}

RequestSettings usualRequest() {
  RequestSettings settings;
  settings.asked.destination = Coordinates{0.0, 0.0};
  settings.asked.radiusMeters = 100.0;
  settings.device = phoneWhereEverythingWorks();
  return settings;
}

SessionSettings sessionThatOnlyRemembers() {
  SessionSettings settings;
  settings.device = phoneWhereEverythingWorks();
  return settings;
}

SessionSettings sessionThatKeepsCollecting(std::int64_t timeBetweenReadingsMs = 5000) {
  SessionSettings settings;
  settings.keepCollectingReadings = true;
  settings.timeBetweenReadingsMs = timeBetweenReadingsMs;
  settings.device = phoneWhereEverythingWorks();
  return settings;
}

// A reading as the GPS hands it over, not checked yet.
UncheckedReading fromTheGps(double seconds, double metersNorth, double gpsErrorMeters = 8.0) {
  const LocationReading where = readingNorth(seconds, metersNorth, gpsErrorMeters);

  UncheckedReading unchecked;
  unchecked.latitudeDegrees = where.position.latitudeDegrees;
  unchecked.longitudeDegrees = where.position.longitudeDegrees;
  unchecked.hasAccuracy = true;
  unchecked.horizontalAccuracyMeters = gpsErrorMeters;
  unchecked.capturedAtMs = where.capturedAtMs;
  unchecked.timeReliable = true;
  return unchecked;
}

class FakePhone {
 public:
  // The engine is big (it holds every history), so it lives outside the stack.
  std::unique_ptr<Engine> engine = std::make_unique<Engine>();

  std::int64_t nowMs = 0;

  // The pretend hardware, driven only by what the engine says.
  bool gpsIsOn = false;
  std::int64_t gpsTimeBetweenReadingsMs = 0;
  bool activityUpdatesAreOn = false;
  bool timerIsSet = false;
  std::int64_t timerFiresAtMs = 0;
  int timesTheGpsWasTurnedOn = 0;
  int timesTheGpsWasTurnedOff = 0;

  // Every report the engine finished, in the order it came out.
  std::vector<Report> reports;

  // What the real platform code does after every call into the engine.
  void obeyTheEngine() {
    Report report;
    while (engine->takeFinishedReport(report)) {
      reports.push_back(report);
    }

    const PlatformWork work = engine->whatThePlatformMustDoNow();

    if (work.locationUpdatesAreWanted && !gpsIsOn) {
      gpsIsOn = true;
      timesTheGpsWasTurnedOn += 1;
    } else if (!work.locationUpdatesAreWanted && gpsIsOn) {
      gpsIsOn = false;
      timesTheGpsWasTurnedOff += 1;
    }
    if (gpsIsOn) {
      gpsTimeBetweenReadingsMs = work.timeBetweenLocationUpdatesMs;
    }

    activityUpdatesAreOn = work.phoneActivityUpdatesAreWanted;
    timerIsSet = work.wakeUpIsWanted;
    timerFiresAtMs = work.wakeUpAtMs;
  }

  // Moves the clock to `seconds`, firing the timer every time it is due on
  // the way.
  void goTo(double seconds) {
    const std::int64_t targetMs = secondsToMs(seconds);
    while (timerIsSet && timerFiresAtMs <= targetMs) {
      if (timerFiresAtMs > nowMs) {
        nowMs = timerFiresAtMs;
      }
      engine->whenTimePasses(nowMs);
      obeyTheEngine();
    }
    if (targetMs > nowMs) {
      nowMs = targetMs;
    }
  }

  // --- What the app does ---
  SessionOpened openSession(const SessionSettings& settings = sessionThatOnlyRemembers()) {
    const SessionOpened opened = engine->openSession(settings, nowMs);
    obeyTheEngine();
    return opened;
  }

  void closeSession(std::uint32_t sessionId) {
    engine->closeSession(sessionId, nowMs);
    obeyTheEngine();
  }

  std::uint32_t startRequest(std::uint32_t sessionId = 0,
                                const RequestSettings& settings = usualRequest()) {
    const RequestStarted started = engine->startRequest(sessionId, settings, nowMs);
    REQUIRE(started.result == RequestStarted::Result::Accepted);
    obeyTheEngine();
    return started.requestId;
  }

  void cancelRequest(std::uint32_t requestId) {
    engine->cancelRequest(requestId, nowMs);
    obeyTheEngine();
  }

  // --- What the phone does ---
  // The GPS has a reading. It only reaches the engine while the GPS is on.
  // Returns true when it was delivered.
  bool gpsHasAReading(double metersNorth, double gpsErrorMeters = 8.0) {
    return gpsHasThisReading(
        fromTheGps(static_cast<double>(nowMs) / 1000.0, metersNorth, gpsErrorMeters));
  }

  bool gpsHasThisReading(const UncheckedReading& reading) {
    if (!gpsIsOn) {
      return false;
    }
    engine->whenReadingsArrive(&reading, 1, nowMs);
    obeyTheEngine();
    return true;
  }

  // A reading that arrives although the GPS was already turned off: a late
  // delivery from the system.
  void lateReadingArrives(double metersNorth) {
    const UncheckedReading reading =
        fromTheGps(static_cast<double>(nowMs) / 1000.0, metersNorth, 8.0);
    engine->whenReadingsArrive(&reading, 1, nowMs);
    obeyTheEngine();
  }

  void phoneActivityChanges(PhoneActivity activity, double sinceSeconds) {
    engine->whenPhoneActivityChanges(activity, PhoneActivityConfidence::High,
                                     secondsToMs(sinceSeconds), nowMs);
    obeyTheEngine();
  }

  void appGoesToBackground() {
    engine->whenAppGoesToBackground(nowMs);
    obeyTheEngine();
  }

  void locationPermissionIsLost() {
    engine->whenLocationPermissionIsLost(nowMs);
    obeyTheEngine();
  }

  void locationProviderFails() {
    engine->whenLocationProviderFails(nowMs);
    obeyTheEngine();
  }

  // --- Looking at the reports ---
  const Report& lastReport() const {
    REQUIRE_FALSE(reports.empty());
    return reports.back();
  }

  const Report& reportOf(std::uint32_t requestId) const {
    for (const Report& report : reports) {
      if (report.requestId == requestId) {
        return report;
      }
    }
    FAIL("no report for that request");
    return reports.front();
  }

  int howManyReportsOf(std::uint32_t requestId) const {
    int howMany = 0;
    for (const Report& report : reports) {
      if (report.requestId == requestId) {
        howMany += 1;
      }
    }
    return howMany;
  }

  // Nothing is left running: no request, no GPS, no timer.
  bool everythingIsQuiet() const {
    return !gpsIsOn && !timerIsSet && !activityUpdatesAreOn &&
           engine->howManyRequestsAreRunning() == 0 && engine->howManyReportsAreWaiting() == 0;
  }
};

bool hasReason(const Report& report, ReasonCode reason) {
  return report.reasons.contains(reason);
}

}  // namespace

// --- The examples at the top of Engine.cpp ---------------------------------------

TEST_CASE("one request with no session: the GPS goes on, a reading comes, it goes off") {
  FakePhone phone;
  CHECK(phone.everythingIsQuiet());

  const std::uint32_t request = phone.startRequest();

  CHECK(phone.gpsIsOn);
  CHECK(phone.gpsTimeBetweenReadingsMs == 1000);
  CHECK(phone.timerIsSet);
  CHECK(phone.timerFiresAtMs == 4500);
  CHECK(phone.reports.empty());
  CHECK(phone.engine->howManyRequestsAreRunning() == 1);

  phone.goTo(1.2);
  CHECK(phone.gpsHasAReading(20));

  REQUIRE(phone.reports.size() == 1);
  const Report& report = phone.lastReport();
  CHECK(report.requestId == request);
  CHECK(report.sessionId == 0);
  CHECK(report.status == ReportStatus::Available);
  CHECK(report.source == FixSource::Fresh);
  CHECK(report.relation == EvidenceRelation::SupportsInside);
  CHECK(report.durationMs == 1200);
  CHECK(report.readingsReceived == 1);
  CHECK(report.readingsKept == 1);
  CHECK(report.readingsExamined == 1);

  CHECK(phone.everythingIsQuiet());
  CHECK(phone.timesTheGpsWasTurnedOn == 1);
  CHECK(phone.timesTheGpsWasTurnedOff == 1);
}

TEST_CASE("CON-013 two requests share the GPS; cancelling one keeps it on") {
  FakePhone phone;
  const std::uint32_t session = phone.openSession().sessionId;

  const std::uint32_t first = phone.startRequest(session);
  phone.goTo(1);
  const std::uint32_t second = phone.startRequest(session);
  CHECK(phone.timesTheGpsWasTurnedOn == 1);  // it was already on
  CHECK(phone.timerFiresAtMs == 4500);       // the earlier of the two wake ups

  phone.goTo(2);
  phone.cancelRequest(first);

  REQUIRE(phone.reports.size() == 1);
  CHECK(phone.reportOf(first).status == ReportStatus::Cancelled);
  CHECK(phone.gpsIsOn);  // the second one still needs it
  CHECK(phone.timesTheGpsWasTurnedOff == 0);

  phone.goTo(3);
  phone.gpsHasAReading(20);

  REQUIRE(phone.reports.size() == 2);
  CHECK(phone.reportOf(second).status == ReportStatus::Available);
  CHECK(phone.everythingIsQuiet());
  CHECK(phone.timesTheGpsWasTurnedOff == 1);
}

TEST_CASE("SES-002 SES-004 a session that keeps collecting has a history ready") {
  FakePhone phone;

  const SessionOpened opened = phone.openSession(sessionThatKeepsCollecting(5000));
  REQUIRE(opened.sessionId != 0);
  CHECK(opened.collecting == CollectingState::Active);
  CHECK(opened.reasons.empty());
  CHECK(phone.gpsIsOn);
  CHECK(phone.gpsTimeBetweenReadingsMs == 5000);
  CHECK(phone.timerFiresAtMs == 900000);  // the collecting stops after 15 minutes

  // Three readings, 5 seconds apart.
  phone.gpsHasAReading(20);
  phone.goTo(5);
  phone.gpsHasAReading(20);
  phone.goTo(10);
  phone.gpsHasAReading(20);
  CHECK(phone.engine->howManyReadingsSessionRemembers(opened.sessionId) == 3);

  // A request starts and the GPS gives nothing new.
  phone.goTo(12);
  const std::uint32_t request = phone.startRequest(opened.sessionId);
  CHECK(phone.gpsTimeBetweenReadingsMs == 1000);  // faster while a request waits

  phone.goTo(40);

  REQUIRE(phone.howManyReportsOf(request) == 1);
  const Report& report = phone.reportOf(request);
  CHECK(report.source == FixSource::Cached);  // the answer came from the history
  CHECK(report.status == ReportStatus::Degraded);
  CHECK(report.hasPosition);
  CHECK(report.readingsExamined == 3);
  CHECK(report.readingsReceived == 0);
  CHECK(report.durationMs == 11500);

  // The session is still collecting, at its own pace again.
  CHECK(phone.gpsIsOn);
  CHECK(phone.gpsTimeBetweenReadingsMs == 5000);
}

// --- Sessions ------------------------------------------------------------------------

TEST_CASE("SES-001 HIS-001 a usual session does not turn anything on") {
  FakePhone phone;

  const SessionOpened opened = phone.openSession();

  CHECK(opened.sessionId == 1);
  CHECK(opened.collecting == CollectingState::NotRequested);
  CHECK(opened.reasons.empty());
  CHECK(phone.engine->howManySessionsAreOpen() == 1);
  CHECK(phone.engine->howManyReadingsSessionRemembers(opened.sessionId) == 0);
  CHECK(phone.everythingIsQuiet());
  CHECK(phone.timesTheGpsWasTurnedOn == 0);
}

TEST_CASE("SES-005 a usual session only remembers the readings of its requests") {
  FakePhone phone;
  const std::uint32_t session = phone.openSession().sessionId;

  // No request is running: the GPS is off and nothing can arrive.
  CHECK_FALSE(phone.gpsHasAReading(20));
  CHECK(phone.engine->howManyReadingsSessionRemembers(session) == 0);

  // A request obtains one reading; the session remembers it.
  const std::uint32_t first = phone.startRequest(session);
  phone.goTo(1);
  phone.gpsHasAReading(20);
  CHECK(phone.engine->howManyReadingsSessionRemembers(session) == 1);
  CHECK(phone.reportOf(first).readingsReceived == 1);
  CHECK(phone.reportOf(first).readingsKept == 1);

  // The next request finds it there. The GPS says nothing this time.
  phone.goTo(5);
  const std::uint32_t second = phone.startRequest(session);
  phone.goTo(30);
  CHECK(phone.reportOf(second).source == FixSource::Cached);
  CHECK(phone.reportOf(second).readingsExamined == 1);
}

TEST_CASE("SES-003 collecting without what it needs is blocked, and nothing is asked") {
  struct Case {
    const char* name;
    ReasonCode reason;
    void (*breakTheDevice)(DeviceState&);
  };
  const Case cases[] = {
      {"permission never asked", ReasonCode::PermissionNotDetermined,
       [](DeviceState& device) { device.locationPermission = LocationPermission::NotDetermined; }},
      {"permission denied", ReasonCode::PermissionDenied,
       [](DeviceState& device) { device.locationPermission = LocationPermission::Denied; }},
      {"permission restricted", ReasonCode::PermissionRestricted,
       [](DeviceState& device) { device.locationPermission = LocationPermission::Restricted; }},
      {"location turned off", ReasonCode::LocationServicesDisabled,
       [](DeviceState& device) { device.locationIsTurnedOn = false; }},
      {"no Google Play services", ReasonCode::PlayServicesUnavailable,
       [](DeviceState& device) {
         device.locationProvider = LocationProviderState::PlayServicesMissing;
       }},
      {"no location provider", ReasonCode::NativeProviderUnavailable,
       [](DeviceState& device) { device.locationProvider = LocationProviderState::Unavailable; }},
      {"app not in the foreground", ReasonCode::ForegroundRequired,
       [](DeviceState& device) { device.appIsInForeground = false; }},
  };

  for (const Case& oneCase : cases) {
    CAPTURE(oneCase.name);
    FakePhone phone;
    SessionSettings settings = sessionThatKeepsCollecting();
    oneCase.breakTheDevice(settings.device);

    const SessionOpened opened = phone.openSession(settings);

    // The session exists, but it does not collect.
    CHECK(opened.sessionId != 0);
    CHECK(opened.collecting == CollectingState::Blocked);
    CHECK(opened.reasons.contains(oneCase.reason));
    CHECK(phone.everythingIsQuiet());
    CHECK(phone.timesTheGpsWasTurnedOn == 0);
  }
}

TEST_CASE("SES-007 collecting stops by itself when its time is over") {
  FakePhone phone;
  SessionSettings settings = sessionThatKeepsCollecting();
  settings.longestCollectingTimeMs = 60000;

  const std::uint32_t session = phone.openSession(settings).sessionId;
  CHECK(phone.timerFiresAtMs == 60000);

  phone.goTo(59);
  CHECK(phone.gpsIsOn);

  phone.goTo(60);
  CHECK(phone.engine->collectingStateOfSession(session) == CollectingState::Stopped);
  CHECK(phone.everythingIsQuiet());  // no GPS and no timer left

  // The session is still open, and its reports say why it stopped collecting.
  CHECK(phone.engine->howManySessionsAreOpen() == 1);
  phone.goTo(70);
  const std::uint32_t request = phone.startRequest(session);
  phone.goTo(71);
  phone.gpsHasAReading(20);
  CHECK(hasReason(phone.reportOf(request), ReasonCode::MonitoringDurationReached));
}

TEST_CASE("SES-006 CON-010 CON-011 the background stops everything and nothing comes back") {
  FakePhone phone;
  const std::uint32_t session = phone.openSession(sessionThatKeepsCollecting()).sessionId;
  phone.gpsHasAReading(20);
  const std::uint32_t request = phone.startRequest(session, [] {
    RequestSettings settings = usualRequest();
    settings.asked.worstGpsErrorAcceptedMeters = 1.0;  // nothing will be good enough
    return settings;
  }());
  CHECK(phone.gpsIsOn);

  phone.goTo(2);
  phone.appGoesToBackground();

  CHECK(phone.reportOf(request).status == ReportStatus::Cancelled);
  CHECK(hasReason(phone.reportOf(request), ReasonCode::AppBackgrounded));
  CHECK_FALSE(phone.reportOf(request).hasPosition);
  CHECK(phone.engine->collectingStateOfSession(session) == CollectingState::Stopped);
  CHECK(phone.everythingIsQuiet());

  // Time passes with the app back in the foreground: nothing restarts.
  phone.goTo(600);
  CHECK(phone.everythingIsQuiet());
  CHECK(phone.timesTheGpsWasTurnedOn == 1);

  // The app has to ask again. The session and its history are still there.
  const std::uint32_t again = phone.startRequest(session);
  CHECK(phone.gpsIsOn);
  phone.gpsHasAReading(20);
  CHECK(phone.reportOf(again).status == ReportStatus::Available);
  CHECK_FALSE(hasReason(phone.reportOf(again), ReasonCode::AppBackgrounded));
  // The collecting did not come back.
  CHECK(phone.engine->collectingStateOfSession(session) == CollectingState::Stopped);
  CHECK(phone.everythingIsQuiet());
}

TEST_CASE("SES-008 HIS-011 HIS-013 PUB-005 closing a session forgets it; twice is harmless") {
  FakePhone phone;
  const std::uint32_t session = phone.openSession(sessionThatKeepsCollecting()).sessionId;
  phone.gpsHasAReading(20);
  CHECK(phone.engine->howManyReadingsSessionRemembers(session) == 1);

  phone.closeSession(session);

  CHECK(phone.engine->howManySessionsAreOpen() == 0);
  CHECK(phone.engine->howManyReadingsSessionRemembers(session) == 0);
  CHECK(phone.everythingIsQuiet());

  phone.closeSession(session);  // again
  phone.closeSession(9999);     // one that never existed
  CHECK(phone.engine->howManySessionsAreOpen() == 0);
  CHECK(phone.reports.empty());
}

TEST_CASE("CON-007 closing a session stops its requests, and only its own") {
  FakePhone phone;
  const std::uint32_t sessionA = phone.openSession().sessionId;
  const std::uint32_t sessionB = phone.openSession().sessionId;
  const std::uint32_t ofA = phone.startRequest(sessionA);
  const std::uint32_t ofB = phone.startRequest(sessionB);
  const std::uint32_t withNoSession = phone.startRequest();

  phone.goTo(1);
  phone.closeSession(sessionA);

  REQUIRE(phone.reports.size() == 1);
  CHECK(phone.reportOf(ofA).status == ReportStatus::Cancelled);
  CHECK(hasReason(phone.reportOf(ofA), ReasonCode::SessionStopped));
  CHECK(phone.gpsIsOn);
  CHECK(phone.engine->howManyRequestsAreRunning() == 2);

  phone.goTo(2);
  phone.gpsHasAReading(20);
  CHECK(phone.reportOf(ofB).status == ReportStatus::Available);
  CHECK(phone.reportOf(withNoSession).status == ReportStatus::Available);
  CHECK(phone.everythingIsQuiet());
}

TEST_CASE("HIS-010 SES-012 two sessions never see each other's readings") {
  FakePhone phone;
  const std::uint32_t sessionA = phone.openSession().sessionId;
  const std::uint32_t sessionB = phone.openSession().sessionId;

  // Only A has a request running when the reading arrives.
  phone.startRequest(sessionA);
  phone.goTo(1);
  phone.gpsHasAReading(20);

  CHECK(phone.engine->howManyReadingsSessionRemembers(sessionA) == 1);
  CHECK(phone.engine->howManyReadingsSessionRemembers(sessionB) == 0);

  // A request in B that gets nothing new has nothing to fall back on.
  phone.goTo(2);
  const std::uint32_t ofB = phone.startRequest(sessionB);
  phone.goTo(20);
  CHECK(phone.reportOf(ofB).status == ReportStatus::Unavailable);
  CHECK(phone.reportOf(ofB).readingsExamined == 0);
}

TEST_CASE("two sessions that both collect receive the same readings, each in its own history") {
  FakePhone phone;
  const std::uint32_t sessionA = phone.openSession(sessionThatKeepsCollecting(5000)).sessionId;
  const std::uint32_t sessionB = phone.openSession(sessionThatKeepsCollecting(2000)).sessionId;

  CHECK(phone.timesTheGpsWasTurnedOn == 1);         // one GPS for both
  CHECK(phone.gpsTimeBetweenReadingsMs == 2000);    // the shorter time wins

  for (int second = 0; second <= 10; ++second) {
    phone.goTo(second);
    phone.gpsHasAReading(20);
  }

  // A keeps one every 5 seconds (0, 5, 10); B one every 2 (0, 2, 4, 6, 8, 10).
  CHECK(phone.engine->howManyReadingsSessionRemembers(sessionA) == 3);
  CHECK(phone.engine->howManyReadingsSessionRemembers(sessionB) == 6);

  phone.closeSession(sessionB);
  CHECK(phone.gpsIsOn);
  CHECK(phone.gpsTimeBetweenReadingsMs == 5000);

  phone.closeSession(sessionA);
  CHECK(phone.everythingIsQuiet());
}

TEST_CASE("HIS-014 HIS-016 a new session starts empty and old numbers point at nothing") {
  FakePhone phone;
  const std::uint32_t oldSession = phone.openSession(sessionThatKeepsCollecting()).sessionId;
  phone.gpsHasAReading(20);
  phone.closeSession(oldSession);

  const std::uint32_t newSession = phone.openSession().sessionId;

  CHECK(newSession != oldSession);
  CHECK(phone.engine->howManyReadingsSessionRemembers(newSession) == 0);

  // A reading that the system delivers late, for the session that is gone.
  phone.lateReadingArrives(20);
  CHECK(phone.engine->howManyReadingsSessionRemembers(newSession) == 0);

  // Using the old number gives a report that says so, and starts nothing.
  const std::uint32_t request = phone.startRequest(oldSession);
  REQUIRE(phone.howManyReportsOf(request) == 1);
  CHECK(phone.reportOf(request).status == ReportStatus::Unavailable);
  CHECK(phone.reportOf(request).sessionId == oldSession);
  CHECK(phone.reportOf(request).howManySteps == 2);
  CHECK(phone.reportOf(request).radiusMeters == 100.0);
  CHECK(phone.reportOf(request).permissionPrecision == PermissionPrecision::Precise);
  CHECK(phone.reportOf(request).score.qualityScore == 0);
  CHECK(hasReason(phone.reportOf(request), ReasonCode::UnknownSession));
  CHECK(phone.everythingIsQuiet());
}

TEST_CASE("PUB-004 HIS-012 clearing the history keeps the session open") {
  FakePhone phone;
  const std::uint32_t session = phone.openSession(sessionThatKeepsCollecting()).sessionId;
  phone.gpsHasAReading(20);
  REQUIRE(phone.engine->howManyReadingsSessionRemembers(session) == 1);

  CHECK(phone.engine->clearSessionHistory(session));
  CHECK(phone.engine->clearSessionHistory(session));  // twice
  CHECK_FALSE(phone.engine->clearSessionHistory(9999));

  CHECK(phone.engine->howManyReadingsSessionRemembers(session) == 0);
  CHECK(phone.engine->howManySessionsAreOpen() == 1);
  CHECK(phone.engine->collectingStateOfSession(session) == CollectingState::Active);

  // It goes on collecting: the next reading is kept right away.
  phone.goTo(1);
  phone.gpsHasAReading(20);
  CHECK(phone.engine->howManyReadingsSessionRemembers(session) == 1);
}

TEST_CASE("SES-009 CON-021 losing the permission stops everything and forgets the readings") {
  FakePhone phone;
  const std::uint32_t session = phone.openSession(sessionThatKeepsCollecting()).sessionId;
  phone.gpsHasAReading(20);
  phone.goTo(1);
  const std::uint32_t request = phone.startRequest(session, [] {
    RequestSettings settings = usualRequest();
    settings.asked.worstGpsErrorAcceptedMeters = 1.0;
    return settings;
  }());

  phone.goTo(3);
  phone.locationPermissionIsLost();

  CHECK(phone.reportOf(request).status == ReportStatus::Unavailable);
  CHECK(hasReason(phone.reportOf(request), ReasonCode::PermissionDenied));
  CHECK_FALSE(phone.reportOf(request).hasPosition);
  CHECK(phone.engine->howManyReadingsSessionRemembers(session) == 0);
  CHECK(phone.engine->collectingStateOfSession(session) == CollectingState::Stopped);
  CHECK(phone.everythingIsQuiet());
}

TEST_CASE("REC-025 a GPS failure ends the requests with what they remember") {
  FakePhone phone;
  const std::uint32_t session = phone.openSession(sessionThatKeepsCollecting()).sessionId;
  phone.gpsHasAReading(20);
  phone.goTo(2);
  const std::uint32_t request = phone.startRequest(session);

  phone.goTo(3);
  phone.locationProviderFails();

  const Report& report = phone.reportOf(request);
  CHECK(report.status == ReportStatus::Degraded);
  CHECK(report.source == FixSource::Cached);
  CHECK(hasReason(report, ReasonCode::NativeProviderUnavailable));
  CHECK(phone.engine->collectingStateOfSession(session) == CollectingState::Stopped);
  CHECK(phone.everythingIsQuiet());
}

// --- Requests -----------------------------------------------------------------------

TEST_CASE("REC-017 a request with no session starts empty and leaves nothing behind") {
  FakePhone phone;

  const std::uint32_t first = phone.startRequest();
  phone.goTo(1);
  phone.gpsHasAReading(20);
  REQUIRE(phone.reportOf(first).status == ReportStatus::Available);

  // The next one does not find that reading.
  phone.goTo(2);
  const std::uint32_t second = phone.startRequest();
  phone.goTo(20);

  CHECK(phone.reportOf(second).status == ReportStatus::Unavailable);
  CHECK(phone.reportOf(second).readingsExamined == 0);
  CHECK(hasReason(phone.reportOf(second), ReasonCode::HistoryEmpty));
}

TEST_CASE("CON-012 INV-11 two requests with different addresses do not mix") {
  FakePhone phone;
  const std::uint32_t session = phone.openSession().sessionId;

  RequestSettings nearAddress = usualRequest();
  RequestSettings farAddress = usualRequest();
  farAddress.asked.destination = Coordinates{0.0, 0.01};  // about 1.1 km east
  farAddress.asked.radiusMeters = 50.0;

  const std::uint32_t nearRequest = phone.startRequest(session, nearAddress);
  const std::uint32_t farRequest = phone.startRequest(session, farAddress);

  phone.goTo(1);
  phone.gpsHasAReading(20);

  REQUIRE(phone.reports.size() == 2);
  const Report& near = phone.reportOf(nearRequest);
  const Report& far = phone.reportOf(farRequest);

  CHECK(near.radiusMeters == 100.0);
  CHECK(near.relation == EvidenceRelation::SupportsInside);
  CHECK(near.distanceToDestinationMeters == doctest::Approx(20.0));

  CHECK(far.radiusMeters == 50.0);
  CHECK(far.relation == EvidenceRelation::SupportsOutside);
  CHECK(far.distanceToDestinationMeters > 1000.0);

  // The same reading and the same score for both.
  CHECK(near.score.qualityScore == far.score.qualityScore);
  CHECK(near.sessionId == session);
  CHECK(far.sessionId == session);
}

TEST_CASE("CON-005 CON-006 cancelling after the end, twice, or something that never existed") {
  FakePhone phone;
  const std::uint32_t request = phone.startRequest();
  phone.goTo(1);
  phone.gpsHasAReading(20);
  REQUIRE(phone.reports.size() == 1);

  phone.cancelRequest(request);
  phone.cancelRequest(request);
  phone.cancelRequest(424242);

  CHECK(phone.reports.size() == 1);
  CHECK(phone.lastReport().status == ReportStatus::Available);
  CHECK(phone.everythingIsQuiet());
}

TEST_CASE("the address and the radius are checked before anything starts") {
  FakePhone phone;

  SUBCASE("GEO-012 a latitude that does not exist") {
    RequestSettings settings = usualRequest();
    settings.asked.destination = Coordinates{90.00001, 0.0};
    const RequestStarted started = phone.engine->startRequest(0, settings, 0);
    CHECK(started.result == RequestStarted::Result::InvalidRequest);
    CHECK(started.requestId == 0);
  }

  SUBCASE("GEO-013 a longitude that does not exist") {
    RequestSettings settings = usualRequest();
    settings.asked.destination = Coordinates{0.0, 180.00001};
    CHECK(phone.engine->startRequest(0, settings, 0).result ==
          RequestStarted::Result::InvalidRequest);
  }

  SUBCASE("GEO-016 GEO-017 GEO-019 a radius that is zero, negative or above 100 km") {
    for (const double radius : {0.0, -1.0, 100001.0}) {
      RequestSettings settings = usualRequest();
      settings.asked.radiusMeters = radius;
      CHECK(phone.engine->startRequest(0, settings, 0).result ==
            RequestStarted::Result::InvalidRequest);
    }
  }

  SUBCASE("GEO-018 a radius of exactly 100 km is accepted") {
    RequestSettings settings = usualRequest();
    settings.asked.radiusMeters = 100000.0;
    CHECK(phone.engine->startRequest(0, settings, 0).result ==
          RequestStarted::Result::Accepted);
  }

  phone.obeyTheEngine();
  // A request that was refused turned nothing on and produced no report.
  if (phone.engine->howManyRequestsAreRunning() == 0) {
    CHECK(phone.everythingIsQuiet());
    CHECK(phone.reports.empty());
  }
}

TEST_CASE("CON-014 too many requests at once get a report that says 'busy'") {
  FakePhone phone;
  const std::uint32_t session = phone.openSession().sessionId;

  SUBCASE("more than 4 in the same session") {
    for (int count = 0; count < 4; ++count) {
      phone.startRequest(session);
    }
    CHECK(phone.engine->howManyRequestsAreRunning() == 4);

    const std::uint32_t oneTooMany = phone.startRequest(session);

    REQUIRE(phone.howManyReportsOf(oneTooMany) == 1);
    CHECK(phone.reportOf(oneTooMany).status == ReportStatus::Unavailable);
    CHECK(hasReason(phone.reportOf(oneTooMany), ReasonCode::Busy));
    CHECK(phone.engine->howManyRequestsAreRunning() == 4);

    // Another session, or no session, still has room.
    phone.startRequest();
    CHECK(phone.engine->howManyRequestsAreRunning() == 5);
  }

  SUBCASE("more than 16 in total") {
    for (int count = 0; count < 16; ++count) {
      phone.startRequest();
    }
    CHECK(phone.engine->howManyRequestsAreRunning() == 16);

    const std::uint32_t oneTooMany = phone.startRequest();
    CHECK(hasReason(phone.reportOf(oneTooMany), ReasonCode::Busy));

    // One reading ends all 16, and everything goes quiet.
    phone.goTo(1);
    phone.gpsHasAReading(20);
    CHECK(phone.reports.size() == 17);
    CHECK(phone.everythingIsQuiet());
  }
}

TEST_CASE("more than 8 sessions are refused") {
  FakePhone phone;
  for (int count = 0; count < 8; ++count) {
    CHECK(phone.openSession().sessionId != 0);
  }

  const SessionOpened refused = phone.openSession();
  CHECK(refused.sessionId == 0);
  CHECK(refused.reasons.contains(ReasonCode::Busy));
  CHECK(phone.engine->howManySessionsAreOpen() == 8);

  // Closing one makes room again, and the new one gets a new number.
  phone.closeSession(3);
  const SessionOpened accepted = phone.openSession();
  CHECK(accepted.sessionId == 9);
}

TEST_CASE("when the platform does not take the reports, new requests are refused") {
  Engine* engine = new Engine();
  const RequestSettings settings = usualRequest();

  // Nobody calls takeFinishedReport(). An unknown session gives a report at
  // once, and each of them stays in the waiting list.
  std::size_t accepted = 0;
  for (int count = 0; count < 100; ++count) {
    const RequestStarted started = engine->startRequest(77, settings, 0);
    if (started.result == RequestStarted::Result::Accepted) {
      accepted += 1;
    } else {
      CHECK(started.result == RequestStarted::Result::TooManyReportsWaiting);
    }
  }

  CHECK(accepted == MAX_REPORTS_WAITING);
  CHECK(engine->howManyReportsAreWaiting() == MAX_REPORTS_WAITING);

  // Taking them makes room again, and they come out in order.
  Report report;
  std::uint32_t expectedRequestId = 1;
  while (engine->takeFinishedReport(report)) {
    CHECK(report.requestId == expectedRequestId);
    expectedRequestId += 1;
  }
  CHECK(engine->startRequest(77, settings, 0).result == RequestStarted::Result::Accepted);

  delete engine;
}

TEST_CASE("running requests always keep room for their own reports") {
  Engine* engine = new Engine();
  const RequestSettings settings = usualRequest();

  // 16 requests run, and nobody takes any report.
  for (int count = 0; count < 16; ++count) {
    REQUIRE(engine->startRequest(0, settings, 0).result ==
            RequestStarted::Result::Accepted);
  }
  // 4 more reports fit in the list beside the 16 that are still to come.
  for (int count = 0; count < 4; ++count) {
    CHECK(engine->startRequest(77, settings, 0).result ==
          RequestStarted::Result::Accepted);
  }
  CHECK(engine->startRequest(77, settings, 0).result ==
        RequestStarted::Result::TooManyReportsWaiting);

  // Everything ends: all 20 reports are there, none was lost.
  engine->whenAppGoesToBackground(1000);
  CHECK(engine->howManyReportsAreWaiting() == 20);
  CHECK(engine->howManyRequestsAreRunning() == 0);

  delete engine;
}

// --- Readings ----------------------------------------------------------------------------

TEST_CASE("REC-027 a delivery with good and broken readings is counted correctly") {
  FakePhone phone;
  const std::uint32_t request = phone.startRequest(0, [] {
    RequestSettings settings = usualRequest();
    settings.asked.worstGpsErrorAcceptedMeters = 1.0;  // keep the request running
    return settings;
  }());

  phone.goTo(1);
  UncheckedReading readings[4] = {
      fromTheGps(0.5, 20),
      fromTheGps(0.6, 20),
      fromTheGps(0.7, 20),
      fromTheGps(0.8, 20),
  };
  readings[1].latitudeDegrees = 91.0;  // not a place on Earth
  readings[3].hasAccuracy = false;     // no GPS error given

  phone.engine->whenReadingsArrive(readings, 4, phone.nowMs);
  phone.obeyTheEngine();
  phone.goTo(20);

  const Report& report = phone.reportOf(request);
  CHECK(report.readingsReceived == 4);
  CHECK(report.readingsKept == 2);
  CHECK(report.readingsExamined == 2);
  CHECK(report.hasPosition);
  CHECK(hasReason(report, ReasonCode::LocationInvalidCoordinates));
  CHECK(hasReason(report, ReasonCode::LocationAccuracyUnavailable));
}

TEST_CASE("HIS-006 VAL-014 the same reading twice, and a reading that arrives late") {
  FakePhone phone;
  const std::uint32_t request = phone.startRequest(0, [] {
    RequestSettings settings = usualRequest();
    settings.asked.worstGpsErrorAcceptedMeters = 1.0;
    return settings;
  }());

  phone.goTo(3);
  const UncheckedReading newer = fromTheGps(3, 20);
  const UncheckedReading older = fromTheGps(2, 20);
  phone.gpsHasThisReading(newer);
  phone.gpsHasThisReading(newer);  // delivered again
  phone.gpsHasThisReading(older);  // taken before, delivered after
  phone.goTo(20);

  const Report& report = phone.reportOf(request);
  CHECK(report.readingsReceived == 3);
  CHECK(report.readingsKept == 2);
  CHECK(report.readingsExamined == 2);
  CHECK(hasReason(report, ReasonCode::SamplesDuplicate));
  CHECK(hasReason(report, ReasonCode::SamplesOutOfOrder));
  // Two readings that agree, not three.
  CHECK(report.score.consistencyFactor == doctest::Approx(0.80));
}

TEST_CASE("HIS-027 ten poor readings do not push out the only good one") {
  FakePhone phone;
  const std::uint32_t session = phone.openSession(sessionThatKeepsCollecting(1000)).sessionId;

  // The phone stands still the whole time, so a good reading stays good.
  phone.phoneActivityChanges(PhoneActivity::StandingStill, 0);

  // One good reading, then eleven poor ones, one per second.
  phone.gpsHasAReading(20, 5);
  for (int second = 1; second <= 11; ++second) {
    phone.goTo(second);
    phone.gpsHasAReading(20, 400);
  }
  CHECK(phone.engine->howManyReadingsSessionRemembers(session) == 10);

  // A request that gets nothing new answers with the good reading.
  phone.goTo(12);
  const std::uint32_t request = phone.startRequest(session);
  phone.goTo(40);

  const Report& report = phone.reportOf(request);
  REQUIRE(report.hasPosition);
  CHECK(report.gpsErrorMeters == 5.0);
  CHECK(report.source == FixSource::Cached);
}

TEST_CASE("HIS-025 thousands of readings never grow the memory") {
  FakePhone phone;
  const std::uint32_t session = phone.openSession(sessionThatKeepsCollecting(0)).sessionId;

  for (int count = 0; count < 12000; ++count) {
    phone.nowMs += 100;
    phone.gpsHasAReading(20);
  }

  CHECK(phone.engine->howManyReadingsSessionRemembers(session) == 10);
}

TEST_CASE("HIS-005 a session forgets readings older than 2 minutes") {
  FakePhone phone;
  SessionSettings settings = sessionThatKeepsCollecting();
  const std::uint32_t session = phone.openSession(settings).sessionId;
  phone.gpsHasAReading(20);
  CHECK(phone.engine->howManyReadingsSessionRemembers(session) == 1);

  phone.goTo(121);
  phone.engine->whenTimePasses(phone.nowMs);

  CHECK(phone.engine->howManyReadingsSessionRemembers(session) == 0);
}

TEST_CASE("VAL-008 a reading slightly from the future is used, and the report says so") {
  FakePhone phone;
  const std::uint32_t request = phone.startRequest();

  phone.goTo(1);
  // Taken "half a second from now": two clocks of the phone disagree a little.
  phone.gpsHasThisReading(fromTheGps(1.5, 20));

  const Report& report = phone.reportOf(request);
  CHECK(report.status == ReportStatus::Available);
  CHECK(report.readingAgeMs == 0);  // counted as taken right now
  CHECK(hasReason(report, ReasonCode::LocationFutureTimestamp));
}

TEST_CASE("VAL-007 a reading far from the future is thrown away") {
  FakePhone phone;
  const std::uint32_t request = phone.startRequest();

  phone.goTo(1);
  phone.gpsHasThisReading(fromTheGps(121, 20));  // two minutes ahead
  phone.goTo(20);

  const Report& report = phone.reportOf(request);
  CHECK(report.status == ReportStatus::Unavailable);
  CHECK(report.score.qualityScore == 0);
  CHECK(report.readingsReceived == 1);
  CHECK(report.readingsKept == 0);
  CHECK(hasReason(report, ReasonCode::LocationFutureTimestamp));
}

TEST_CASE("a reading that arrives already older than 2 minutes is not kept") {
  FakePhone phone;
  phone.goTo(200);
  const std::uint32_t request = phone.startRequest();

  phone.goTo(201);
  phone.gpsHasThisReading(fromTheGps(10, 20));  // 191 seconds old
  phone.goTo(220);

  const Report& report = phone.reportOf(request);
  CHECK(report.status == ReportStatus::Unavailable);
  CHECK(report.readingsReceived == 1);
  CHECK(report.readingsKept == 0);
  CHECK(report.readingsExamined == 0);
  CHECK(hasReason(report, ReasonCode::LocationStale));
}

TEST_CASE("a reading that arrives when nobody wants readings is dropped") {
  FakePhone phone;
  const std::uint32_t session = phone.openSession().sessionId;

  phone.lateReadingArrives(20);

  CHECK(phone.engine->howManyReadingsSessionRemembers(session) == 0);
  CHECK(phone.reports.empty());
  CHECK(phone.everythingIsQuiet());
}

TEST_CASE("readings the app made up are marked in every report") {
  FakePhone phone;
  phone.engine->setReadingsAreSimulated(true);

  const std::uint32_t request = phone.startRequest();
  phone.goTo(1);
  phone.gpsHasAReading(20);
  CHECK(hasReason(phone.reportOf(request), ReasonCode::SimulatedLocationSource));

  // Also in a report that used no reading at all.
  const std::uint32_t unknown = phone.startRequest(555);
  CHECK(hasReason(phone.reportOf(unknown), ReasonCode::SimulatedLocationSource));

  phone.engine->setReadingsAreSimulated(false);
  const std::uint32_t real = phone.startRequest();
  phone.goTo(2);
  phone.gpsHasAReading(20);
  CHECK_FALSE(hasReason(phone.reportOf(real), ReasonCode::SimulatedLocationSource));
}

// --- The phone activity -------------------------------------------------------------------

TEST_CASE("activity updates are on only while a request can use them") {
  FakePhone phone;
  phone.openSession(sessionThatKeepsCollecting());
  CHECK_FALSE(phone.activityUpdatesAreOn);  // collecting alone does not need them

  const std::uint32_t request = phone.startRequest();
  CHECK(phone.activityUpdatesAreOn);

  phone.cancelRequest(request);
  CHECK_FALSE(phone.activityUpdatesAreOn);

  // The app does not want the activity, or the phone cannot give it.
  RequestSettings withoutActivity = usualRequest();
  withoutActivity.asked.usePhoneActivity = false;
  phone.startRequest(0, withoutActivity);
  CHECK_FALSE(phone.activityUpdatesAreOn);

  RequestSettings phoneCannot = usualRequest();
  phoneCannot.device.phoneActivity = PhoneActivitySupport::PermissionNotAsked;
  phone.startRequest(0, phoneCannot);
  CHECK_FALSE(phone.activityUpdatesAreOn);
}

TEST_CASE("MOT-031 the activity that is going on counts until now") {
  FakePhone phone;
  const std::uint32_t session = phone.openSession(sessionThatKeepsCollecting()).sessionId;
  phone.gpsHasAReading(20, 5);

  // One report, at second 1: "standing still since second 0". Nothing else.
  phone.goTo(1);
  phone.phoneActivityChanges(PhoneActivity::StandingStill, 0);

  // 30 seconds later a request gets nothing new from the GPS.
  phone.goTo(30);
  const std::uint32_t request = phone.startRequest(session);
  phone.goTo(40);

  const Report& report = phone.reportOf(request);
  REQUIRE(report.hasPosition);
  CHECK(report.readingAgeMs == 34500);  // it ended when the first attempt ended
  CHECK(report.phoneActivity.standingStillSinceTheReading);
  CHECK(report.phoneActivity.activityNow == PhoneActivity::StandingStill);
  CHECK(report.howFarPhoneCouldBeMeters == doctest::Approx(5.0 + 0.5 * 34.5));
  CHECK(report.relation == EvidenceRelation::SupportsInside);
}

TEST_CASE("MOT-020 a new activity ends the one before it") {
  FakePhone phone;
  const std::uint32_t session = phone.openSession(sessionThatKeepsCollecting()).sessionId;
  phone.gpsHasAReading(20, 5);
  phone.phoneActivityChanges(PhoneActivity::InVehicle, 0);

  // The vehicle parked at second 28.
  phone.goTo(28);
  phone.phoneActivityChanges(PhoneActivity::StandingStill, 28);

  phone.goTo(30);
  const std::uint32_t request = phone.startRequest(session);
  phone.goTo(50);

  const Report& report = phone.reportOf(request);
  REQUIRE(report.hasPosition);
  // 28 seconds in a vehicle and the rest standing still: the old reading is
  // not saved by the fact that the phone is standing still now.
  CHECK_FALSE(report.phoneActivity.standingStillSinceTheReading);
  CHECK(report.phoneActivity.activityNow == PhoneActivity::StandingStill);
  CHECK(report.howFarPhoneCouldBeMeters > 1260.0);  // 45 x 28
  CHECK(report.relation == EvidenceRelation::Inconclusive);
}

TEST_CASE("MOT-024 MOT-025 after the updates stop, the activity is unknown again") {
  FakePhone phone;
  const std::uint32_t session = phone.openSession(sessionThatKeepsCollecting()).sessionId;
  phone.gpsHasAReading(20, 5);
  phone.phoneActivityChanges(PhoneActivity::StandingStill, 0);

  phone.goTo(10);
  phone.engine->whenPhoneActivityUpdatesStop(phone.nowMs);

  phone.goTo(20);
  const std::uint32_t request = phone.startRequest(session);
  phone.goTo(40);

  const Report& report = phone.reportOf(request);
  REQUIRE(report.hasPosition);
  // Standing still for 10 seconds, then nothing known for the rest.
  CHECK_FALSE(report.phoneActivity.standingStillSinceTheReading);
  CHECK(report.phoneActivity.activityNow == PhoneActivity::Unknown);
  CHECK(report.phoneActivity.hasGaps);
  CHECK(hasReason(report, ReasonCode::MotionInsufficientCoverage));
}

TEST_CASE("MOT-012 an activity report that arrives after the end changes nothing") {
  FakePhone phone;
  const std::uint32_t request = phone.startRequest();
  phone.goTo(1);
  phone.gpsHasAReading(20);
  REQUIRE(phone.reports.size() == 1);
  const int scoreBefore = phone.reportOf(request).score.qualityScore;

  phone.phoneActivityChanges(PhoneActivity::InVehicle, 0);

  CHECK(phone.reports.size() == 1);
  CHECK(phone.reportOf(request).score.qualityScore == scoreBefore);
  CHECK(phone.everythingIsQuiet());
}

TEST_CASE("a 'since' in the future counts from now") {
  FakePhone phone;
  const std::uint32_t session = phone.openSession(sessionThatKeepsCollecting()).sessionId;
  phone.goTo(5);
  phone.gpsHasAReading(20, 5);

  // "Since second 99", said at second 5: it counts from second 5.
  phone.phoneActivityChanges(PhoneActivity::StandingStill, 99);

  phone.goTo(10);
  const std::uint32_t request = phone.startRequest(session);
  phone.goTo(30);

  CHECK(phone.reportOf(request).phoneActivity.standingStillSinceTheReading);
}

TEST_CASE("the activity before a change is remembered") {
  FakePhone phone;
  const std::uint32_t session = phone.openSession(sessionThatKeepsCollecting()).sessionId;
  phone.gpsHasAReading(20, 5);

  // Walking from second 0 to second 10, standing still after that.
  phone.phoneActivityChanges(PhoneActivity::Walking, 0);
  phone.goTo(10);
  phone.phoneActivityChanges(PhoneActivity::StandingStill, 10);

  phone.goTo(20);
  const std::uint32_t request = phone.startRequest(session);
  phone.goTo(40);

  // The request ends at second 31.5: 10 s walking and 21.5 s standing still.
  const Report& report = phone.reportOf(request);
  REQUIRE(report.hasPosition);
  CHECK(report.readingAgeMs == 31500);
  CHECK(report.howFarPhoneCouldBeMeters == doctest::Approx(5.0 + 3.0 * 10.0 + 0.5 * 21.5));
  CHECK_FALSE(report.phoneActivity.hasGaps);
}

TEST_CASE("MOT-022 an activity the phone is not very sure about does not shrink the distance") {
  FakePhone phone;
  const std::uint32_t session = phone.openSession(sessionThatKeepsCollecting()).sessionId;
  phone.gpsHasAReading(20, 5);
  phone.engine->whenPhoneActivityChanges(PhoneActivity::StandingStill,
                                         PhoneActivityConfidence::Medium, 0, phone.nowMs);

  phone.goTo(10);
  const std::uint32_t request = phone.startRequest(session);
  phone.goTo(30);

  const Report& report = phone.reportOf(request);
  REQUIRE(report.hasPosition);
  CHECK_FALSE(report.phoneActivity.standingStillSinceTheReading);
  CHECK(report.phoneActivity.activityNow == PhoneActivity::StandingStill);
  CHECK(report.phoneActivity.confidenceNow == PhoneActivityConfidence::Medium);
  CHECK(report.howFarPhoneCouldBeMeters == doctest::Approx(5.0 + 45.0 * 21.5));
}

TEST_CASE("the background also ends what is known about the phone activity") {
  FakePhone phone;
  const std::uint32_t session = phone.openSession(sessionThatKeepsCollecting()).sessionId;
  phone.gpsHasAReading(20, 5);
  phone.phoneActivityChanges(PhoneActivity::StandingStill, 0);

  phone.goTo(10);
  phone.appGoesToBackground();

  // Back in the foreground, a request gets nothing new from the GPS.
  phone.goTo(20);
  const std::uint32_t request = phone.startRequest(session);
  phone.goTo(40);

  const Report& report = phone.reportOf(request);
  REQUIRE(report.hasPosition);
  // Standing still until second 10; after that, nobody was watching.
  CHECK_FALSE(report.phoneActivity.standingStillSinceTheReading);
  CHECK(report.phoneActivity.activityNow == PhoneActivity::Unknown);
  CHECK(report.howFarPhoneCouldBeMeters == doctest::Approx(5.0 + 0.5 * 10.0 + 45.0 * 21.5));
}

TEST_CASE("the engine keeps accepting activity reports during a long session") {
  FakePhone phone;
  const std::uint32_t session = phone.openSession(sessionThatKeepsCollecting(0)).sessionId;

  // 500 changes of activity, one per second: far more than the 32 it keeps.
  for (int second = 0; second < 500; ++second) {
    phone.goTo(second);
    const PhoneActivity activity =
        (second % 2 == 0) ? PhoneActivity::Walking : PhoneActivity::StandingStill;
    phone.phoneActivityChanges(activity, second);
    if (second % 10 == 0) {
      phone.gpsHasAReading(20, 5);
    }
  }

  // The latest readings are judged with the latest reports.
  phone.goTo(500);
  const std::uint32_t request = phone.startRequest(session);
  phone.goTo(520);

  const Report& report = phone.reportOf(request);
  REQUIRE(report.hasPosition);
  CHECK(report.phoneActivity.knownUntilNow);
  CHECK_FALSE(report.phoneActivity.hasGaps);  // every second since the reading is known
  CHECK(report.phoneActivity.activityNow == PhoneActivity::StandingStill);
}

// --- Stress ---------------------------------------------------------------------------------

TEST_CASE("CON-015 a hundred sessions opened and closed leave nothing behind") {
  FakePhone phone;

  for (int round = 0; round < 100; ++round) {
    const std::uint32_t session = phone.openSession(sessionThatKeepsCollecting()).sessionId;
    REQUIRE(session != 0);
    phone.gpsHasAReading(20);
    phone.startRequest(session);
    phone.nowMs += 500;
    phone.closeSession(session);

    REQUIRE(phone.everythingIsQuiet());
    REQUIRE(phone.engine->howManySessionsAreOpen() == 0);
  }

  CHECK(phone.reports.size() == 100);
  CHECK(phone.timesTheGpsWasTurnedOn == 100);
  CHECK(phone.timesTheGpsWasTurnedOff == 100);
}

TEST_CASE("CON-016 CON-018 a hundred cancels with late deliveries: one report each") {
  FakePhone phone;

  for (int round = 0; round < 100; ++round) {
    const std::uint32_t request = phone.startRequest();
    phone.nowMs += 300;
    phone.cancelRequest(request);

    // The system delivers a reading twice after the cancel, and the timer
    // fires late.
    phone.lateReadingArrives(20);
    phone.lateReadingArrives(20);
    phone.engine->whenTimePasses(phone.nowMs + 20000);
    phone.obeyTheEngine();
    phone.cancelRequest(request);

    REQUIRE(phone.howManyReportsOf(request) == 1);
    REQUIRE(phone.reportOf(request).status == ReportStatus::Cancelled);
    REQUIRE(phone.everythingIsQuiet());
  }
}

TEST_CASE("INV-10 NFR-05 whatever the app and the phone do, everything ends quiet") {
  DeterministicRng rng(515);

  for (int round = 0; round < 300; ++round) {
    FakePhone phone;
    std::vector<std::uint32_t> sessions;
    std::vector<std::uint32_t> requests;

    for (int step = 0; step < 40; ++step) {
      phone.goTo(static_cast<double>(phone.nowMs) / 1000.0 + rng.uniform(0.0, 2.0));
      const std::uint64_t what = rng.below(12);

      if (what < 2 && sessions.size() < 6) {
        const bool collects = (rng.below(2) == 0);
        const SessionOpened opened = phone.openSession(
            collects ? sessionThatKeepsCollecting(1000) : sessionThatOnlyRemembers());
        if (opened.sessionId != 0) {
          sessions.push_back(opened.sessionId);
        }
      } else if (what < 5) {
        const std::uint32_t session =
            sessions.empty() ? 0 : sessions[rng.below(sessions.size())];
        const RequestStarted started =
            phone.engine->startRequest(session, usualRequest(), phone.nowMs);
        phone.obeyTheEngine();
        if (started.result == RequestStarted::Result::Accepted) {
          requests.push_back(started.requestId);
        }
      } else if (what < 9) {
        const double gpsError = (rng.below(3) == 0) ? 300.0 : 8.0;
        phone.gpsHasAReading(rng.uniform(0.0, 60.0), gpsError);
      } else if (what == 9 && !requests.empty()) {
        phone.cancelRequest(requests[rng.below(requests.size())]);
      } else if (what == 10 && !sessions.empty()) {
        const std::size_t which = rng.below(sessions.size());
        phone.closeSession(sessions[which]);
        sessions.erase(sessions.begin() + static_cast<std::ptrdiff_t>(which));
      } else if (what == 11) {
        if (rng.below(3) == 0) {
          phone.appGoesToBackground();
        } else {
          phone.phoneActivityChanges(PhoneActivity::Walking,
                                     static_cast<double>(phone.nowMs) / 1000.0);
        }
      }

      // The GPS is on exactly when something needs it.
      const bool somethingNeedsTheGps = phone.engine->howManyRequestsAreRunning() > 0;
      if (somethingNeedsTheGps) {
        REQUIRE(phone.gpsIsOn);
        REQUIRE(phone.timerIsSet);
      }
    }

    // The app closes everything it opened, and then enough time passes.
    for (const std::uint32_t session : sessions) {
      phone.closeSession(session);
    }
    phone.goTo(static_cast<double>(phone.nowMs) / 1000.0 + 30.0);

    REQUIRE(phone.everythingIsQuiet());
    REQUIRE(phone.engine->howManySessionsAreOpen() == 0);

    // Every request that was accepted produced exactly one report.
    for (const std::uint32_t request : requests) {
      REQUIRE(phone.howManyReportsOf(request) == 1);
    }
    REQUIRE(phone.reports.size() == requests.size());
  }
}
