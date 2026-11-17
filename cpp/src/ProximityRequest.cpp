// =============================================================================
// THE ORDER OF THE CALLS IN THIS FILE
// =============================================================================
//
//   start()                                          <- the app asked
//     |-- the GPS cannot be used?   ->  finishWithWhatWeRemember()
//     `-- otherwise the FIRST ATTEMPT begins
//
//   whenReadingsArrive()                             <- the GPS delivered readings
//     |-- the total time is over?   ->  whenTimePasses()
//     |-- buildReport()                                 (Report.cpp)
//     |-- the answer is "available"?         ->  finishWith(that report)
//     `-- weak, and still the first attempt? ->  beginExtraTime()
//
//   whenTimePasses()                                 <- the timer of the platform fired
//     |-- the total time is over?   ->  finishWithWhatWeRemember()
//     |-- the first attempt is over?
//     |      |-- buildReport(): is a remembered reading good enough?
//     |      |-- yes, or there is no extra time  ->  finishWithWhatWeRemember()
//     |      `-- no                              ->  beginExtraTime()
//     `-- the extra time is over?   ->  finishWithWhatWeRemember()
//
//   stop(why)                                        <- cancel, background, ...
//     `-- finishWith(a report that says why)
//
//   finishWithWhatWeRemember()
//     |-- buildReport()
//     `-- finishWith()
//
//   finishWith(report)       the ONLY place where a request ends
//
// =============================================================================
// EXAMPLES, STEP BY STEP
// The usual times: first attempt 4.5 s, extra time 7 s, total 12 s.
// =============================================================================
//
// EXAMPLE 1 - a good reading arrives quickly
//
//   second 0.0   start()                  FIRST ATTEMPT, GPS on, wake up at 4.5
//   second 1.2   whenReadingsArrive()     one reading, GPS error 8
//                  buildReport()          -> Available, Fresh
//                  finishWith()           the GPS is no longer wanted
//
//   the report: steps "started, first_attempt, finished", 1.2 seconds
//
// EXAMPLE 2 - the first reading is weak, a good one comes later
//
//   second 0.0   start()                  FIRST ATTEMPT
//   second 1.0   whenReadingsArrive()     GPS error 500
//                  buildReport()          -> Degraded (imprecise)
//                  beginExtraTime()       EXTRA TIME, wake up at 8.0
//   second 3.0   whenReadingsArrive()     GPS error 8
//                  buildReport()          -> Available, Recovered
//                  finishWith()
//
// EXAMPLE 3 - the GPS never answers and nothing is remembered
//
//   second 0.0   start()                  FIRST ATTEMPT, wake up at 4.5
//   second 4.5   whenTimePasses()         the first attempt is over
//                  buildReport()          -> nothing remembered
//                  beginExtraTime()       EXTRA TIME, wake up at 11.5
//   second 11.5  whenTimePasses()         the extra time is over
//                  finishWithWhatWeRemember()  -> Unavailable, score 0
//
//   reasons: "initial_fix_timeout", "recovery_window_timeout", "no_usable_fix"
//
// EXAMPLE 4 - the GPS never answers, but a remembered reading is good enough
//
//   The history has a reading from 10 seconds ago with GPS error 8, and the
//   phone reported "standing still" during all of those 10 seconds.
//
//   second 0.0   start()                  FIRST ATTEMPT
//   second 4.5   whenTimePasses()         the first attempt is over
//                  buildReport()          -> a reading, the phone stood still
//                  finishWithWhatWeRemember()  -> Degraded, Cached
//
//   No extra time is spent: waiting 7 more seconds would not tell us more
//   than "the phone has not moved".
//
// EXAMPLE 5 - the app cancels
//
//   second 0.0   start()                  FIRST ATTEMPT
//   second 2.0   stop(CancelledByTheApp)
//                  finishWith()           -> Cancelled, "request_cancelled"
//   second 2.5   whenReadingsArrive()     a late reading: nothing changes
//
// =============================================================================
// WHAT THE REST OF THE ENGINE DOES WITH THIS
// =============================================================================
//
// Engine.cpp keeps a list of the requests that are running. Every time
// something happens (readings, the timer, the app going to the background) it
// tells each of them, and then it asks who is finished, whether the GPS is
// still wanted by someone, and when the next wake up must be.
// =============================================================================

#include "ProximityRequest.hpp"

#include "QualityScore.hpp"

namespace lce {

// =============================================================================
// HELPERS - used only inside this file
// =============================================================================
namespace {

// A time that makes no sense (negative) counts as zero.
std::int64_t notNegative(std::int64_t milliseconds) noexcept {
  return milliseconds > 0 ? milliseconds : 0;
}

// A report that is not "available" has a reason for it. This picks the one
// that explains best why the first attempt was not enough.
ReasonCode findWhyTheAnswerIsNotGoodEnough(const Report& report) noexcept {
  ReasonCode why = ReasonCode::NoUsableFix;

  if (report.reasons.contains(ReasonCode::LocationLowAccuracy)) {
    why = ReasonCode::LocationLowAccuracy;
  } else if (report.reasons.contains(ReasonCode::PossibleMockLocation)) {
    why = ReasonCode::PossibleMockLocation;
  } else if (report.reasons.contains(ReasonCode::PrecisionReduced)) {
    why = ReasonCode::PrecisionReduced;
  } else if (report.reasons.contains(ReasonCode::SuspectedGpsJump)) {
    why = ReasonCode::SuspectedGpsJump;
  } else if (report.reasons.contains(ReasonCode::LocationStale)) {
    why = ReasonCode::LocationStale;
  }

  return why;
}

// When the first attempt gave nothing: is a reading we remember good enough
// to answer right away, without spending the extra time?
// Yes when the phone stood still ever since that reading was taken, the
// reading is precise enough and nothing about it is suspicious.
bool rememberedReadingIsGoodEnough(const Report& remembered,
                                   const WhatTheAppAskedFor& asked) noexcept {
  const bool isPreciseEnough = (remembered.gpsErrorMeters <= asked.worstGpsErrorAcceptedMeters);
  const bool looksLikeAJump = remembered.score.wasLimitedBy(ScoreLimit::SuspectedJump);
  const bool blockedForBeingSimulated = remembered.score.wasLimitedBy(ScoreLimit::MockLocation);

  return remembered.hasPosition && remembered.phoneActivity.standingStillSinceTheReading &&
         isPreciseEnough && !looksLikeAJump && !blockedForBeingSimulated;
}

// A report with no position at all: unavailable, score 0, inconclusive.
Report reportWithNothing(const RequestSettings& settings) noexcept {
  Report report;
  report.radiusMeters = settings.asked.radiusMeters;
  report.permissionPrecision = settings.device.permissionPrecision;
  report.score = calculateQualityScore(ScoreInput{});
  return report;
}

}  // namespace

// =============================================================================
// FUNCTIONS OTHER FILES CAN CALL
//
// They are announced in ProximityRequest.hpp.
// =============================================================================

void ProximityRequest::start(std::uint32_t requestId,
                              std::uint32_t sessionId,
                              const RequestSettings& settings,
                              const LocationHistory& history,
                              const PhoneActivityHistory& activityHistory,
                              std::int64_t nowMs) noexcept {
  // Forget everything from the request that used this object before.
  *this = ProximityRequest{};

  requestId_ = requestId;
  sessionId_ = sessionId;
  settings_ = settings;
  settings_.totalTimeAllowedMs = notNegative(settings.totalTimeAllowedMs);
  settings_.firstAttemptTimeMs = notNegative(settings.firstAttemptTimeMs);
  settings_.extraTimeMs = notNegative(settings.extraTimeMs);

  startedAtMs_ = nowMs;
  mustFinishAtMs_ = nowMs + settings_.totalTimeAllowedMs;

  // From now on, every reading with this arrival number or a higher one was
  // obtained by this request.
  whatThisRequestKnows_.firstArrivalNumberOfThisRequest = history.arrivalNumberOfNextReading();
  whatThisRequestKnows_.locationPermission = settings.device.locationPermission;
  whatThisRequestKnows_.permissionPrecision = settings.device.permissionPrecision;

  rememberStep(RequestStep::Started);

  // The app wants the phone activity to be used, but the phone cannot give it.
  if (settings.asked.usePhoneActivity) {
    const PhoneActivitySupport support = settings.device.phoneActivity;
    if (support == PhoneActivitySupport::PermissionDenied) {
      reasons_.add(ReasonCode::MotionPermissionDenied);
    } else if (support != PhoneActivitySupport::Available) {
      reasons_.add(ReasonCode::MotionUnavailable);
    }
  }

  // --- Can the GPS be used at all? -------------------------------------------
  const DeviceState& device = settings.device;
  const bool permissionIsGranted = (device.locationPermission == LocationPermission::Granted);
  bool gpsCanBeUsed = true;

  if (!permissionIsGranted) {
    gpsCanBeUsed = false;  // buildReport() writes down which permission problem it is
  } else if (!device.locationIsTurnedOn) {
    gpsCanBeUsed = false;
    reasons_.add(ReasonCode::LocationServicesDisabled);
  } else if (device.locationProvider == LocationProviderState::PlayServicesMissing) {
    gpsCanBeUsed = false;
    reasons_.add(ReasonCode::PlayServicesUnavailable);
  } else if (device.locationProvider == LocationProviderState::Unavailable) {
    gpsCanBeUsed = false;
    reasons_.add(ReasonCode::NativeProviderUnavailable);
  } else if (!device.appIsInForeground) {
    gpsCanBeUsed = false;
    reasons_.add(ReasonCode::ForegroundRequired);
  }

  if (!gpsCanBeUsed) {
    finishWithWhatWeRemember(history, activityHistory, nowMs);
    return;
  }

  // --- The first attempt begins ----------------------------------------------
  phase_ = Phase::FirstAttempt;
  rememberStep(RequestStep::FirstAttempt);
  currentPhaseEndsAtMs_ = nowMs + settings_.firstAttemptTimeMs;

  // When the app gave zero milliseconds to the first attempt (or to the whole
  // request), that time is already over.
  whenTimePasses(history, activityHistory, nowMs);
}

void ProximityRequest::whenReadingsArrive(const LocationHistory& history,
                                           const PhoneActivityHistory& activityHistory,
                                           std::int64_t nowMs,
                                           const NotesAboutNewReadings& notes) noexcept {
  if (isFinished()) {
    return;  // a late delivery changes nothing
  }

  readingsReceived_ += notes.howManyArrived;
  readingsKept_ += notes.howManyWereKept;
  reasons_.merge(notes.reasons);

  if (theTotalTimeIsOver(nowMs)) {
    whenTimePasses(history, activityHistory, nowMs);
    return;
  }

  // Is the answer good enough now?
  const Report reportRightNow =
      buildReport(history, activityHistory, nowMs, settings_.asked, whatThisRequestKnows_);

  const bool answerIsGoodEnough = (reportRightNow.status == ReportStatus::Available);
  const bool stillInTheFirstAttempt = (phase_ == Phase::FirstAttempt);
  const bool somethingArrived = (notes.howManyArrived > 0);
  const bool thereIsExtraTime = (settings_.extraTimeMs > 0);

  if (answerIsGoodEnough) {
    finishWith(reportRightNow, nowMs);

  } else if (stillInTheFirstAttempt && somethingArrived && thereIsExtraTime) {
    // The GPS answered, but with something weak. The first attempt did not
    // work: the extra time starts now instead of waiting for the timer.
    hasFallbackCause_ = true;
    fallbackCause_ = findWhyTheAnswerIsNotGoodEnough(reportRightNow);
    beginExtraTime(history, nowMs);
  }
  // Otherwise: keep waiting.
}

void ProximityRequest::whenTimePasses(const LocationHistory& history,
                                       const PhoneActivityHistory& activityHistory,
                                       std::int64_t nowMs) noexcept {
  if (isFinished()) {
    return;
  }

  const bool currentPhaseIsOver = (nowMs >= currentPhaseEndsAtMs_);
  const bool firstAttemptIsOver = (phase_ == Phase::FirstAttempt && currentPhaseIsOver);
  const bool extraTimeIsOver = (phase_ == Phase::ExtraTime && currentPhaseIsOver);

  if (theTotalTimeIsOver(nowMs)) {
    reasons_.add(ReasonCode::OverallDeadlineReached);

    const bool noGoodReadingInTheFirstAttempt = (phase_ == Phase::FirstAttempt);
    if (noGoodReadingInTheFirstAttempt) {
      firstAttemptTimedOut_ = true;
      reasons_.add(ReasonCode::InitialFixTimeout);
      hasFallbackCause_ = true;
      fallbackCause_ = ReasonCode::InitialFixTimeout;
    }

    finishWithWhatWeRemember(history, activityHistory, nowMs);

  } else if (firstAttemptIsOver) {
    firstAttemptTimedOut_ = true;
    reasons_.add(ReasonCode::InitialFixTimeout);
    hasFallbackCause_ = true;
    fallbackCause_ = ReasonCode::InitialFixTimeout;

    // Before spending the extra time: is a reading we remember good enough?
    const Report remembered =
        buildReport(history, activityHistory, nowMs, settings_.asked, whatThisRequestKnows_);

    const bool thereIsNoExtraTime = (settings_.extraTimeMs == 0);

    if (rememberedReadingIsGoodEnough(remembered, settings_.asked) || thereIsNoExtraTime) {
      finishWithWhatWeRemember(history, activityHistory, nowMs);
    } else {
      beginExtraTime(history, nowMs);
    }

  } else if (extraTimeIsOver) {
    reasons_.add(ReasonCode::RecoveryWindowTimeout);
    finishWithWhatWeRemember(history, activityHistory, nowMs);
  }
  // Otherwise: nothing is over yet, keep waiting.
}

void ProximityRequest::stop(WhyTheRequestStopped why,
                             const LocationHistory& history,
                             const PhoneActivityHistory& activityHistory,
                             std::int64_t nowMs) noexcept {
  if (isFinished()) {
    return;  // stopping twice, or after the answer, changes nothing
  }

  if (why == WhyTheRequestStopped::LocationProviderFailed) {
    // The GPS broke, but what we remember may still be useful.
    reasons_.add(ReasonCode::NativeProviderUnavailable);
    finishWithWhatWeRemember(history, activityHistory, nowMs);
    return;
  }

  // Every other way of stopping gives a report with no position.
  Report report = reportWithNothing(settings_);

  if (why == WhyTheRequestStopped::CancelledByTheApp) {
    report.status = ReportStatus::Cancelled;
    reasons_.add(ReasonCode::RequestCancelled);

  } else if (why == WhyTheRequestStopped::AppWentToBackground) {
    report.status = ReportStatus::Cancelled;
    reasons_.add(ReasonCode::AppBackgrounded);

  } else if (why == WhyTheRequestStopped::SessionWasClosed) {
    report.status = ReportStatus::Cancelled;
    reasons_.add(ReasonCode::SessionStopped);

  } else {
    // LocationPermissionWasLost
    report.status = ReportStatus::Unavailable;
    reasons_.add(ReasonCode::PermissionDenied);
  }

  finishWith(report, nowMs);
}

bool ProximityRequest::wantsPhoneActivityUpdates() const noexcept {
  const bool phoneCanReportItsActivity =
      (settings_.device.phoneActivity == PhoneActivitySupport::Available);

  return !isFinished() && settings_.asked.usePhoneActivity && phoneCanReportItsActivity;
}

std::int64_t ProximityRequest::nextWakeUpAtMs() const noexcept {
  // Whichever comes first: the end of the current step or the end of
  // everything.
  std::int64_t wakeUpAtMs = currentPhaseEndsAtMs_;
  if (mustFinishAtMs_ < wakeUpAtMs) {
    wakeUpAtMs = mustFinishAtMs_;
  }
  return wakeUpAtMs;
}

// --- Private pieces ----------------------------------------------------------

void ProximityRequest::rememberStep(RequestStep step) noexcept {
  const bool thereIsRoom = (howManySteps_ < MAX_STEPS_IN_REPORT);
  if (thereIsRoom) {
    steps_[howManySteps_] = step;
    howManySteps_ += 1;
  }
}

void ProximityRequest::beginExtraTime(const LocationHistory& history,
                                       std::int64_t nowMs) noexcept {
  phase_ = Phase::ExtraTime;
  extraTimeWasUsed_ = true;
  rememberStep(RequestStep::ExtraTime);

  // A reading that arrives from now on was obtained during the extra time.
  whatThisRequestKnows_.firstArrivalNumberOfExtraTime = history.arrivalNumberOfNextReading();

  currentPhaseEndsAtMs_ = nowMs + settings_.extraTimeMs;
}

void ProximityRequest::finishWithWhatWeRemember(const LocationHistory& history,
                                                 const PhoneActivityHistory& activityHistory,
                                                 std::int64_t nowMs) noexcept {
  rememberStep(RequestStep::AnswerFromHistory);

  const Report report =
      buildReport(history, activityHistory, nowMs, settings_.asked, whatThisRequestKnows_);

  finishWith(report, nowMs);
}

void ProximityRequest::finishWith(Report report, std::int64_t nowMs) noexcept {
  rememberStep(RequestStep::Finished);

  report.requestId = requestId_;
  report.sessionId = sessionId_;

  report.steps = steps_;
  report.howManySteps = howManySteps_;
  report.firstAttemptTimedOut = firstAttemptTimedOut_;
  report.extraTimeWasUsed = extraTimeWasUsed_;
  report.hasFallbackCause = hasFallbackCause_;
  report.fallbackCause = fallbackCause_;
  report.readingsReceived = readingsReceived_;
  report.readingsKept = readingsKept_;
  report.durationMs = notNegative(nowMs - startedAtMs_);
  report.reasons.merge(reasons_);

  // Readings were thrown away because their time made no sense, and nothing
  // else could be used: say that the age is the problem.
  const bool readingsHadBrokenTime = reasons_.contains(ReasonCode::LocationAgeUnreliable) ||
                                     reasons_.contains(ReasonCode::LocationInvalidTimestamp);
  if (!report.hasPosition && readingsHadBrokenTime) {
    report.ageReliability = AgeReliability::Unreliable;
  }

  report_ = report;
  phase_ = Phase::Finished;
}

}  // namespace lce
