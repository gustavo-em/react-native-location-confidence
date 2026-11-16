// =============================================================================
// THE ORDER OF THE CALLS IN THIS FILE
// =============================================================================
//
//   startEvaluation(sessionId, settings, nowMs)            <- the app asked
//     |-- the address or the radius makes no sense?  ->  InvalidRequest
//     |-- no such session / too many evaluations?    ->  a report that says so
//     |      `-- putReportWithNoEvaluationInTheWaitingList()
//     |-- request.start()                                  (EvaluationRequest.cpp)
//     `-- collectFinishedEvaluations()
//
//   whenReadingsArrive(readings, howMany, nowMs)           <- the GPS delivered readings
//     |-- for each reading
//     |      |-- checkReading()                            (ReadingChecker.cpp)
//     |      `-- storeOneReading()    into every history that wants readings
//     |             |-- chooseBestReading()   which reading must keep its place
//     |             `-- history.store()                    (LocationHistory.cpp)
//     |-- for each running evaluation: request.whenReadingsArrive()
//     `-- collectFinishedEvaluations()
//
//   whenTimePasses(nowMs)                                  <- the timer of the platform fired
//     |-- for each running evaluation: request.whenTimePasses()
//     |-- sessions whose collecting time is over stop collecting
//     `-- collectFinishedEvaluations()
//
//   cancelEvaluation(), closeSession(), whenAppGoesToBackground(),
//   whenLocationPermissionIsLost(), whenLocationProviderFails()
//     |-- request.stop(why)      for the evaluations it concerns
//     `-- collectFinishedEvaluations()
//
//   collectFinishedEvaluations()
//     `-- for each evaluation that finished
//            |-- putReportInTheWaitingList()
//            `-- its slot is wiped and becomes free
//
//   After any of the calls above, the platform asks:
//     takeFinishedReport()          one report at a time, until there is none
//     whatThePlatformMustDoNow()    GPS on or off, activity updates, next wake up
//
// =============================================================================
// EXAMPLE, STEP BY STEP - one evaluation with no session
// =============================================================================
//
//   second 0.0   startEvaluation(0, settings, 0)
//                  request.start()                 the first attempt begins
//                whatThePlatformMustDoNow()
//                  -> GPS wanted, every 1000 ms; wake up at 4500
//                the platform turns the GPS on and sets one timer
//
//   second 1.2   the GPS delivers one reading
//                whenReadingsArrive(reading, 1, 1200)
//                  checkReading()                  -> accepted
//                  storeOneReading()               into the evaluation's own history
//                  request.whenReadingsArrive()    -> the answer is good: finished
//                  collectFinishedEvaluations()    the report goes to the waiting list
//                takeFinishedReport()              -> the report (Available, Fresh)
//                takeFinishedReport()              -> false, nothing else
//                whatThePlatformMustDoNow()
//                  -> GPS not wanted; no wake up
//                the platform turns the GPS off and cancels the timer
//
// =============================================================================
// EXAMPLE - two evaluations share the GPS
// =============================================================================
//
//   second 0   startEvaluation(session 1, address A)    GPS wanted
//   second 1   startEvaluation(session 1, address B)    GPS still wanted (already on)
//   second 2   cancelEvaluation(the first one)
//                whatThePlatformMustDoNow()  -> GPS STILL wanted: the second
//                                               evaluation is running
//   second 3   a good reading arrives        -> the second one finishes
//                whatThePlatformMustDoNow()  -> GPS not wanted
//
//   The platform never counts who is using the GPS. It only looks at the
//   answer after each call.
//
// =============================================================================
// EXAMPLE - a session that keeps collecting
// =============================================================================
//
//   second 0    openSession(keepCollectingReadings, one reading every 5 s)
//                 whatThePlatformMustDoNow()  -> GPS wanted, every 5000 ms
//   second 0-60 readings arrive; one is kept every 5 seconds
//   second 60   startEvaluation(that session)
//                 the history already has readings: if the GPS gives nothing
//                 new, the answer can come from them ("cached")
//                 whatThePlatformMustDoNow()  -> GPS wanted, every 1000 ms
//   second 900  whenTimePasses()              -> the collecting time is over
//                 whatThePlatformMustDoNow()  -> GPS not wanted
//
// =============================================================================

#include "Engine.hpp"

#include <optional>

#include "GeoMath.hpp"
#include "ProximityRelation.hpp"
#include "QualityScore.hpp"
#include "ReadingSelection.hpp"

namespace lce {

// =============================================================================
// HELPERS - used only inside this file
// =============================================================================
namespace {

// Why can a session not collect readings right now? Nothing is added when it
// can.
void addReasonsThatBlockTheGps(const DeviceState& device, ReasonSet& reasons) noexcept {
  if (device.locationPermission == LocationPermission::NotDetermined) {
    reasons.add(ReasonCode::PermissionNotDetermined);
  } else if (device.locationPermission == LocationPermission::Denied) {
    reasons.add(ReasonCode::PermissionDenied);
  } else if (device.locationPermission == LocationPermission::Restricted) {
    reasons.add(ReasonCode::PermissionRestricted);
  } else if (!device.locationIsTurnedOn) {
    reasons.add(ReasonCode::LocationServicesDisabled);
  } else if (device.locationProvider == LocationProviderState::PlayServicesMissing) {
    reasons.add(ReasonCode::PlayServicesUnavailable);
  } else if (device.locationProvider == LocationProviderState::Unavailable) {
    reasons.add(ReasonCode::NativeProviderUnavailable);
  } else if (!device.appIsInForeground) {
    reasons.add(ReasonCode::ForegroundRequired);
  }
}

}  // namespace

// =============================================================================
// FUNCTIONS OTHER FILES CAN CALL
//
// They are announced in Engine.hpp.
// =============================================================================

// --- Sessions ----------------------------------------------------------------

SessionOpened Engine::openSession(const SessionSettings& settings, std::int64_t nowMs) noexcept {
  SessionOpened opened;

  const std::size_t position = findFreeSessionSlot();
  const bool thereIsNoRoom = (position == MAX_OPEN_SESSIONS);
  if (thereIsNoRoom) {
    opened.reasons.add(ReasonCode::Busy);
    return opened;  // sessionId stays 0: the session was not opened
  }

  Session& session = sessions_[position];
  session = Session{};
  session.isOpen = true;
  session.id = nextSessionId_;
  nextSessionId_ += 1;

  if (settings.keepCollectingReadings) {
    addReasonsThatBlockTheGps(settings.device, opened.reasons);
    const bool gpsCanBeUsed = opened.reasons.empty();

    if (gpsCanBeUsed) {
      session.collecting = CollectingState::Active;
      session.timeBetweenReadingsMs =
          settings.timeBetweenReadingsMs > 0 ? settings.timeBetweenReadingsMs : 0;
      const std::int64_t longestTimeMs =
          settings.longestCollectingTimeMs > 0 ? settings.longestCollectingTimeMs : 0;
      session.collectingEndsAtMs = nowMs + longestTimeMs;
    } else {
      // The session is open, but it does not collect. Nothing is asked from
      // the user here: asking for permission is the app's decision.
      session.collecting = CollectingState::Blocked;
    }
  }

  opened.sessionId = session.id;
  opened.collecting = session.collecting;
  return opened;
}

bool Engine::clearSessionHistory(std::uint32_t sessionId) noexcept {
  const std::size_t position = findSession(sessionId);
  const bool sessionExists = (position != MAX_OPEN_SESSIONS);
  if (sessionExists) {
    sessions_[position].history.clear();
    sessions_[position].hasKeptAReading = false;
  }
  return sessionExists;
}

void Engine::closeSession(std::uint32_t sessionId, std::int64_t nowMs) noexcept {
  const std::size_t position = findSession(sessionId);
  const bool sessionExists = (position != MAX_OPEN_SESSIONS);
  if (!sessionExists) {
    return;  // closing twice does nothing
  }

  // Stop the evaluations that belong to this session.
  const PhoneActivityHistory activityHistory = phoneActivityUntil(nowMs);
  for (RunningEvaluation& evaluation : evaluations_) {
    const bool belongsToThisSession =
        evaluation.isRunning && evaluation.hasSession && evaluation.sessionPosition == position;
    if (belongsToThisSession) {
      evaluation.request.stop(WhyTheEvaluationStopped::SessionWasClosed, historyOf(evaluation),
                              activityHistory, nowMs);
    }
  }
  collectFinishedEvaluations();

  // Forget its readings and free the slot. `clear()` wipes the coordinates.
  sessions_[position].history.clear();
  sessions_[position] = Session{};
}

// --- Evaluations -------------------------------------------------------------

EvaluationStarted Engine::startEvaluation(std::uint32_t sessionId,
                                          const EvaluationSettings& settings,
                                          std::int64_t nowMs) noexcept {
  EvaluationStarted started;

  // The JavaScript side already checks the address and the radius. This is a
  // second check, so that a broken value can never reach the calculations.
  const bool requestMakesSense = isValidCoordinates(settings.asked.destination) &&
                                 isValidRadiusMeters(settings.asked.radiusMeters);
  if (!requestMakesSense) {
    started.result = EvaluationStarted::Result::InvalidRequest;
    return started;
  }

  // Every evaluation produces exactly one report, sooner or later. A new one
  // is only accepted when there is room for its report AND for the reports
  // of all the evaluations that are still running. Normally the list is
  // empty, because the platform takes the reports after every call.
  const std::size_t reportsThatWillNeedRoom =
      howManyReportsWaiting_ + howManyEvaluationsAreRunning() + 1;
  const bool thereWouldBeNoRoom = (reportsThatWillNeedRoom > MAX_REPORTS_WAITING);
  if (thereWouldBeNoRoom) {
    started.result = EvaluationStarted::Result::TooManyReportsWaiting;
    return started;
  }

  started.result = EvaluationStarted::Result::Accepted;
  started.requestId = nextRequestId_;
  nextRequestId_ += 1;

  forgetOldThings(nowMs);

  // Which session does it belong to?
  const bool wantsASession = (sessionId != 0);
  const std::size_t sessionPosition = findSession(sessionId);
  const bool sessionExists = (sessionPosition != MAX_OPEN_SESSIONS);

  if (wantsASession && !sessionExists) {
    // The app may have closed the session a moment ago. That is not a
    // programming mistake: answer with a report that says so.
    putReportWithNoEvaluationInTheWaitingList(started.requestId, sessionId, settings,
                                              ReasonCode::UnknownSession);
    return started;
  }

  const std::size_t slot = findFreeEvaluationSlot();
  const bool thereIsNoFreeSlot = (slot == MAX_RUNNING_EVALUATIONS);
  const bool sessionHasTooMany =
      wantsASession &&
      countEvaluationsOfSession(sessionPosition) >= MAX_RUNNING_EVALUATIONS_PER_SESSION;

  if (thereIsNoFreeSlot || sessionHasTooMany) {
    putReportWithNoEvaluationInTheWaitingList(started.requestId, sessionId, settings,
                                              ReasonCode::Busy);
    return started;
  }

  RunningEvaluation& evaluation = evaluations_[slot];
  evaluation = RunningEvaluation{};
  evaluation.isRunning = true;
  evaluation.hasSession = wantsASession;
  evaluation.sessionPosition = wantsASession ? sessionPosition : 0;

  evaluation.request.start(started.requestId, sessionId, settings, historyOf(evaluation),
                           phoneActivityUntil(nowMs), nowMs);

  // It may have finished already (no permission, location turned off...).
  collectFinishedEvaluations();
  return started;
}

void Engine::cancelEvaluation(std::uint32_t requestId, std::int64_t nowMs) noexcept {
  const PhoneActivityHistory activityHistory = phoneActivityUntil(nowMs);

  for (RunningEvaluation& evaluation : evaluations_) {
    const bool isTheOne = evaluation.isRunning && evaluation.request.requestId() == requestId;
    if (isTheOne) {
      evaluation.request.stop(WhyTheEvaluationStopped::CancelledByTheApp, historyOf(evaluation),
                              activityHistory, nowMs);
    }
  }

  collectFinishedEvaluations();
}

// --- Things that happen ------------------------------------------------------

void Engine::whenReadingsArrive(const UncheckedReading* readings,
                                std::size_t howMany,
                                std::int64_t nowMs) noexcept {
  forgetOldThings(nowMs);

  const PhoneActivityHistory activityHistory = phoneActivityUntil(nowMs);

  // Which sessions take readings right now?
  //   - a session with an evaluation running takes every reading;
  //   - a session that is only collecting takes one batch every
  //     `timeBetweenReadingsMs`, so that 10 slots cover more than 10 seconds.
  std::array<bool, MAX_OPEN_SESSIONS> sessionTakesReadings{};
  for (std::size_t position = 0; position < MAX_OPEN_SESSIONS; ++position) {
    const Session& session = sessions_[position];
    if (!session.isOpen) {
      continue;
    }

    const bool hasAnEvaluationRunning = (countEvaluationsOfSession(position) > 0);
    const bool isCollecting = (session.collecting == CollectingState::Active);
    const bool enoughTimeSinceTheLastOne =
        !session.hasKeptAReading ||
        (nowMs - session.lastReadingKeptAtMs >= session.timeBetweenReadingsMs);

    sessionTakesReadings[position] =
        hasAnEvaluationRunning || (isCollecting && enoughTimeSinceTheLastOne);
  }

  // Each place that stores readings keeps its own notes about this batch.
  std::array<NotesAboutNewReadings, MAX_OPEN_SESSIONS> notesOfEachSession{};
  std::array<NotesAboutNewReadings, MAX_RUNNING_EVALUATIONS> notesOfEachLoneEvaluation{};

  for (std::size_t index = 0; index < howMany; ++index) {
    // Every reading passes the entrance check once.
    const ReadingCheck check = checkReading(readings[index], nowMs);

    for (std::size_t position = 0; position < MAX_OPEN_SESSIONS; ++position) {
      if (sessionTakesReadings[position]) {
        storeOneReading(sessions_[position].history, check, activityHistory, nowMs,
                        notesOfEachSession[position]);
      }
    }

    for (std::size_t slot = 0; slot < MAX_RUNNING_EVALUATIONS; ++slot) {
      RunningEvaluation& evaluation = evaluations_[slot];
      const bool worksAlone = evaluation.isRunning && !evaluation.hasSession;
      if (worksAlone) {
        storeOneReading(evaluation.ownHistory, check, activityHistory, nowMs,
                        notesOfEachLoneEvaluation[slot]);
      }
    }
  }

  // Remember when each collecting session last kept something.
  for (std::size_t position = 0; position < MAX_OPEN_SESSIONS; ++position) {
    if (notesOfEachSession[position].howManyWereKept > 0) {
      sessions_[position].hasKeptAReading = true;
      sessions_[position].lastReadingKeptAtMs = nowMs;
    }
  }

  // Tell every running evaluation, with the notes of the place it reads from.
  for (std::size_t slot = 0; slot < MAX_RUNNING_EVALUATIONS; ++slot) {
    RunningEvaluation& evaluation = evaluations_[slot];
    if (!evaluation.isRunning) {
      continue;
    }

    const NotesAboutNewReadings& notes = evaluation.hasSession
                                             ? notesOfEachSession[evaluation.sessionPosition]
                                             : notesOfEachLoneEvaluation[slot];

    evaluation.request.whenReadingsArrive(historyOf(evaluation), activityHistory, nowMs, notes);
  }

  collectFinishedEvaluations();
}

void Engine::whenPhoneActivityChanges(PhoneActivity activity,
                                      PhoneActivityConfidence confidence,
                                      std::int64_t sinceMs,
                                      std::int64_t nowMs) noexcept {
  // "Since" cannot be in the future: nobody knows what the phone will do.
  if (sinceMs > nowMs) {
    sinceMs = nowMs;
  }

  // The activity that was going on ended when this one began.
  closeTheActivityThatIsGoingOn(sinceMs);

  activityGoingOn_.startedAtMs = sinceMs;
  activityGoingOn_.endedAtMs = sinceMs;  // counted until "now" when it is read
  activityGoingOn_.activity = activity;
  activityGoingOn_.confidence = confidence;
  anActivityIsGoingOn_ = true;
}

void Engine::whenPhoneActivityUpdatesStop(std::int64_t nowMs) noexcept {
  closeTheActivityThatIsGoingOn(nowMs);
}

void Engine::whenTimePasses(std::int64_t nowMs) noexcept {
  forgetOldThings(nowMs);

  const PhoneActivityHistory activityHistory = phoneActivityUntil(nowMs);

  for (RunningEvaluation& evaluation : evaluations_) {
    if (evaluation.isRunning) {
      evaluation.request.whenTimePasses(historyOf(evaluation), activityHistory, nowMs);
    }
  }

  // A session never collects for longer than the time it was given.
  for (Session& session : sessions_) {
    const bool collectingTimeIsOver = session.isOpen &&
                                      session.collecting == CollectingState::Active &&
                                      nowMs >= session.collectingEndsAtMs;
    if (collectingTimeIsOver) {
      session.collecting = CollectingState::Stopped;
      session.collectingTimeIsOver = true;
    }
  }

  collectFinishedEvaluations();
}

void Engine::whenAppGoesToBackground(std::int64_t nowMs) noexcept {
  // Nothing keeps running while the app is not visible, and nothing starts
  // again by itself when the app comes back.
  stopEveryEvaluation(WhyTheEvaluationStopped::AppWentToBackground, nowMs);
  stopCollectingInEverySession();

  // The phone activity is no longer watched: what happens from now on is
  // unknown.
  closeTheActivityThatIsGoingOn(nowMs);
}

void Engine::whenLocationPermissionIsLost(std::int64_t nowMs) noexcept {
  stopEveryEvaluation(WhyTheEvaluationStopped::LocationPermissionWasLost, nowMs);
  stopCollectingInEverySession();

  // The user took the permission away: forget every location we have.
  for (Session& session : sessions_) {
    if (session.isOpen) {
      session.history.clear();
      session.hasKeptAReading = false;
    }
  }
}

void Engine::whenLocationProviderFails(std::int64_t nowMs) noexcept {
  stopEveryEvaluation(WhyTheEvaluationStopped::LocationProviderFailed, nowMs);
  stopCollectingInEverySession();
}

// --- What the platform asks after every call ---------------------------------

PlatformWork Engine::whatThePlatformMustDoNow() const noexcept {
  PlatformWork work;

  bool someEvaluationWantsTheGps = false;
  bool someSessionIsCollecting = false;
  std::int64_t shortestTimeBetweenReadingsMs = 0;

  // Evaluations: do they want the GPS, the phone activity, a wake up?
  for (const RunningEvaluation& evaluation : evaluations_) {
    if (!evaluation.isRunning) {
      continue;
    }

    if (evaluation.request.wantsLocationUpdates()) {
      someEvaluationWantsTheGps = true;
    }
    if (evaluation.request.wantsPhoneActivityUpdates()) {
      work.phoneActivityUpdatesAreWanted = true;
    }

    const std::int64_t wakeUpAtMs = evaluation.request.nextWakeUpAtMs();
    if (!work.wakeUpIsWanted || wakeUpAtMs < work.wakeUpAtMs) {
      work.wakeUpIsWanted = true;
      work.wakeUpAtMs = wakeUpAtMs;
    }
  }

  // Sessions that are collecting: they want the GPS, and a wake up when
  // their time is over.
  for (const Session& session : sessions_) {
    const bool isCollecting = session.isOpen && session.collecting == CollectingState::Active;
    if (!isCollecting) {
      continue;
    }

    if (!someSessionIsCollecting ||
        session.timeBetweenReadingsMs < shortestTimeBetweenReadingsMs) {
      shortestTimeBetweenReadingsMs = session.timeBetweenReadingsMs;
    }
    someSessionIsCollecting = true;

    if (!work.wakeUpIsWanted || session.collectingEndsAtMs < work.wakeUpAtMs) {
      work.wakeUpIsWanted = true;
      work.wakeUpAtMs = session.collectingEndsAtMs;
    }
  }

  work.locationUpdatesAreWanted = someEvaluationWantsTheGps || someSessionIsCollecting;

  if (someEvaluationWantsTheGps) {
    // An evaluation is waiting for an answer: ask for readings quickly.
    work.timeBetweenLocationUpdatesMs = TIME_BETWEEN_READINGS_DURING_EVALUATION_MS;
  } else if (someSessionIsCollecting) {
    work.timeBetweenLocationUpdatesMs = shortestTimeBetweenReadingsMs;
  }

  return work;
}

bool Engine::takeFinishedReport(Assessment& report) noexcept {
  const bool nothingIsWaiting = (howManyReportsWaiting_ == 0);
  if (nothingIsWaiting) {
    return false;
  }

  // Hand over the oldest one and move the others one place forward.
  report = reportsWaiting_[0];
  for (std::size_t position = 0; position + 1 < howManyReportsWaiting_; ++position) {
    reportsWaiting_[position] = reportsWaiting_[position + 1];
  }
  howManyReportsWaiting_ -= 1;

  // Wipe the place that is no longer used: a report holds a position.
  reportsWaiting_[howManyReportsWaiting_] = Assessment{};
  return true;
}

// --- For tests and diagnostics -----------------------------------------------

std::size_t Engine::howManySessionsAreOpen() const noexcept {
  std::size_t howMany = 0;
  for (const Session& session : sessions_) {
    if (session.isOpen) {
      howMany += 1;
    }
  }
  return howMany;
}

std::size_t Engine::howManyEvaluationsAreRunning() const noexcept {
  std::size_t howMany = 0;
  for (const RunningEvaluation& evaluation : evaluations_) {
    if (evaluation.isRunning) {
      howMany += 1;
    }
  }
  return howMany;
}

std::size_t Engine::howManyReadingsSessionRemembers(std::uint32_t sessionId) const noexcept {
  const std::size_t position = findSession(sessionId);
  const bool sessionExists = (position != MAX_OPEN_SESSIONS);
  return sessionExists ? sessions_[position].history.size() : 0;
}

CollectingState Engine::collectingStateOfSession(std::uint32_t sessionId) const noexcept {
  const std::size_t position = findSession(sessionId);
  const bool sessionExists = (position != MAX_OPEN_SESSIONS);
  return sessionExists ? sessions_[position].collecting : CollectingState::NotRequested;
}

// --- Private pieces ----------------------------------------------------------

std::size_t Engine::findSession(std::uint32_t sessionId) const noexcept {
  std::size_t found = MAX_OPEN_SESSIONS;  // this value means "not found"

  const bool isARealNumber = (sessionId != 0);
  if (isARealNumber) {
    for (std::size_t position = 0; position < MAX_OPEN_SESSIONS; ++position) {
      if (sessions_[position].isOpen && sessions_[position].id == sessionId) {
        found = position;
      }
    }
  }
  return found;
}

std::size_t Engine::findFreeSessionSlot() const noexcept {
  std::size_t found = MAX_OPEN_SESSIONS;  // this value means "not found"

  for (std::size_t position = 0; position < MAX_OPEN_SESSIONS; ++position) {
    const bool isFree = !sessions_[position].isOpen;
    const bool nothingFoundYet = (found == MAX_OPEN_SESSIONS);
    if (isFree && nothingFoundYet) {
      found = position;
    }
  }
  return found;
}

std::size_t Engine::findFreeEvaluationSlot() const noexcept {
  std::size_t found = MAX_RUNNING_EVALUATIONS;  // this value means "not found"

  for (std::size_t slot = 0; slot < MAX_RUNNING_EVALUATIONS; ++slot) {
    const bool isFree = !evaluations_[slot].isRunning;
    const bool nothingFoundYet = (found == MAX_RUNNING_EVALUATIONS);
    if (isFree && nothingFoundYet) {
      found = slot;
    }
  }
  return found;
}

std::size_t Engine::countEvaluationsOfSession(std::size_t sessionPosition) const noexcept {
  std::size_t howMany = 0;
  for (const RunningEvaluation& evaluation : evaluations_) {
    if (evaluation.isRunning && evaluation.hasSession &&
        evaluation.sessionPosition == sessionPosition) {
      howMany += 1;
    }
  }
  return howMany;
}

const LocationHistory& Engine::historyOf(const RunningEvaluation& evaluation) const noexcept {
  return evaluation.hasSession ? sessions_[evaluation.sessionPosition].history
                               : evaluation.ownHistory;
}

PhoneActivityHistory Engine::phoneActivityUntil(std::int64_t nowMs) const noexcept {
  // Start with a copy of the reports that already ended...
  PhoneActivityHistory everything = activityReportsThatEnded_;

  // ...and add the activity that is still going on, counted until now.
  if (anActivityIsGoingOn_) {
    PhoneActivityReport stillGoingOn = activityGoingOn_;
    stillGoingOn.endedAtMs = nowMs;
    everything.storeAndForgetTheOldestIfFull(stillGoingOn);
  }

  return everything;
}

void Engine::closeTheActivityThatIsGoingOn(std::int64_t atMs) noexcept {
  if (!anActivityIsGoingOn_) {
    return;
  }

  activityGoingOn_.endedAtMs = atMs;

  const bool lastedSomeTime = (activityGoingOn_.endedAtMs > activityGoingOn_.startedAtMs);
  if (lastedSomeTime) {
    activityReportsThatEnded_.storeAndForgetTheOldestIfFull(activityGoingOn_);
  }

  activityGoingOn_ = PhoneActivityReport{};
  anActivityIsGoingOn_ = false;
}

void Engine::forgetOldThings(std::int64_t nowMs) noexcept {
  for (Session& session : sessions_) {
    if (session.isOpen) {
      session.history.forgetOldReadings(nowMs);
    }
  }
  for (RunningEvaluation& evaluation : evaluations_) {
    if (evaluation.isRunning && !evaluation.hasSession) {
      evaluation.ownHistory.forgetOldReadings(nowMs);
    }
  }

  // No reading is kept for more than 2 minutes, so an activity report that
  // ended before that is of no use.
  activityReportsThatEnded_.forgetReportsThatEndedBefore(nowMs - MAX_AGE_IN_HISTORY_MS);
}

void Engine::storeOneReading(LocationHistory& history,
                             const ReadingCheck& check,
                             const PhoneActivityHistory& activityHistory,
                             std::int64_t nowMs,
                             NotesAboutNewReadings& notes) noexcept {
  notes.howManyArrived += 1;

  if (!check.accepted) {
    notes.reasons.add(check.rejectionReason);  // thrown away at the entrance
    return;
  }
  notes.reasons.merge(check.warnings);

  // When the history is full, a reading has to leave. The best reading we
  // have must not be the one: otherwise ten poor readings in a row would
  // push out the only good one.
  std::optional<std::uint64_t> arrivalNumberThatMustStay;
  const bool historyIsFull = (history.size() == LocationHistory::capacity());
  if (historyIsFull) {
    const ChosenReading best =
        chooseBestReading(history, activityHistory, nowMs, RulesForChoosingReading{});
    if (best.found) {
      arrivalNumberThatMustStay = best.best.reading.arrivalNumber;
    }
  }

  const LocationHistory::StoreResult result =
      history.store(check.reading, nowMs, arrivalNumberThatMustStay);

  if (result == LocationHistory::StoreResult::Stored) {
    notes.howManyWereKept += 1;

  } else if (result == LocationHistory::StoreResult::StoredOutOfOrder) {
    notes.howManyWereKept += 1;
    notes.reasons.add(ReasonCode::SamplesOutOfOrder);

  } else if (result == LocationHistory::StoreResult::IgnoredDuplicate) {
    notes.reasons.add(ReasonCode::SamplesDuplicate);

  } else {
    // IgnoredTooOld or IgnoredOlderThanAllStored
    notes.reasons.add(ReasonCode::LocationStale);
  }
}

void Engine::stopEveryEvaluation(WhyTheEvaluationStopped why, std::int64_t nowMs) noexcept {
  const PhoneActivityHistory activityHistory = phoneActivityUntil(nowMs);

  for (RunningEvaluation& evaluation : evaluations_) {
    if (evaluation.isRunning) {
      evaluation.request.stop(why, historyOf(evaluation), activityHistory, nowMs);
    }
  }

  collectFinishedEvaluations();
}

void Engine::stopCollectingInEverySession() noexcept {
  for (Session& session : sessions_) {
    const bool isCollecting = session.isOpen && session.collecting == CollectingState::Active;
    if (isCollecting) {
      session.collecting = CollectingState::Stopped;
    }
  }
}

void Engine::putReportInTheWaitingList(const Assessment& report) noexcept {
  // startEvaluation() only accepts an evaluation when there is room for its
  // report, and each evaluation produces exactly one. So there is always
  // room here; the check only protects the memory if that ever breaks.
  const bool thereIsRoom = (howManyReportsWaiting_ < MAX_REPORTS_WAITING);
  if (thereIsRoom) {
    reportsWaiting_[howManyReportsWaiting_] = report;
    if (readingsAreSimulated_) {
      reportsWaiting_[howManyReportsWaiting_].reasons.add(ReasonCode::SimulatedLocationSource);
    }
    howManyReportsWaiting_ += 1;
  }
}

void Engine::putReportWithNoEvaluationInTheWaitingList(std::uint32_t requestId,
                                                       std::uint32_t sessionId,
                                                       const EvaluationSettings& settings,
                                                       ReasonCode why) noexcept {
  // Unavailable, no position, score 0, and the reason why nothing started.
  Assessment report;
  report.requestId = requestId;
  report.sessionId = sessionId;
  report.radiusMeters = settings.asked.radiusMeters;
  report.permissionPrecision = settings.device.permissionPrecision;
  report.score = calculateQualityScore(ScoreInput{});
  report.reasons.add(why);

  report.steps[0] = EvaluationStep::Started;
  report.steps[1] = EvaluationStep::Finished;
  report.howManySteps = 2;

  putReportInTheWaitingList(report);
}

void Engine::collectFinishedEvaluations() noexcept {
  for (RunningEvaluation& evaluation : evaluations_) {
    const bool justFinished = evaluation.isRunning && evaluation.request.isFinished();
    if (!justFinished) {
      continue;
    }

    Assessment report = evaluation.request.report();

    // When the session stopped collecting because its time was over, the
    // reports of that session say so.
    const bool sessionCollectingTimeIsOver =
        evaluation.hasSession && sessions_[evaluation.sessionPosition].collectingTimeIsOver;
    if (sessionCollectingTimeIsOver) {
      report.reasons.add(ReasonCode::MonitoringDurationReached);
    }

    putReportInTheWaitingList(report);

    // Free the slot. Assigning an empty value wipes the readings it held.
    evaluation.ownHistory.clear();
    evaluation = RunningEvaluation{};
  }
}

}  // namespace lce
