// Study file: TypeScript mirror of ProximityRequest.hpp and ProximityRequest.cpp.
// Same names, same order. Not part of the library. If it ever disagrees with
// the C++, the C++ is right.
// Run: node --no-warnings --experimental-transform-types cpp/src/ProximityRequest.study.ts
// The examples at the end only run when this file is the one being run.

/*
THE BUSINESS RULE

  The app asks "is the courier near this address?". Answering takes time: the
  GPS may need a few seconds to give a good reading, or may give nothing.
  This file decides how long to wait and when to stop.

    FIRST ATTEMPT   wait for a good reading (normally up to 4.5 s)
                      a good reading arrives   -> finished ("fresh")
                      a weak reading arrives   -> go to EXTRA TIME right away
                      nothing arrives in time  -> a remembered reading is good
                                                  enough? finished : EXTRA TIME

    EXTRA TIME      keep waiting (normally up to 7 s more)
                      a good reading arrives   -> finished ("recovered")
                      the extra time ends      -> finished with what we remember

    At any moment   the total time is over (normally 12 s), the app cancels,
                    the app goes to the background...  -> finished

  A request ALWAYS finishes, and it finishes once. It never reads a clock
  and never touches the GPS: it is told what happened and what time it is.

THE ORDER OF THE CALLS

  start()
  ├─ the GPS cannot be used?   ->  finishWithWhatWeRemember()
  └─ otherwise the FIRST ATTEMPT begins

  whenReadingsArrive()
  ├─ the total time is over?   ->  whenTimePasses()
  ├─ buildReport()                                       (Report)
  ├─ the answer is "available"?          ->  finishWith(that report)
  └─ weak, and still the first attempt?  ->  beginExtraTime()

  whenTimePasses()
  ├─ the total time is over?   ->  finishWithWhatWeRemember()
  ├─ the first attempt is over?
  │    ├─ buildReport(): is a remembered reading good enough?
  │    ├─ yes, or there is no extra time  ->  finishWithWhatWeRemember()
  │    └─ no                              ->  beginExtraTime()
  └─ the extra time is over?   ->  finishWithWhatWeRemember()

  stop(why)  ->  finishWith(a report that says why)

  finishWith(report)       the ONLY place where a request ends

ONE REQUEST, STEP BY STEP

  second 0.0   start()                  FIRST ATTEMPT, wake up at 4.5
  second 1.0   whenReadingsArrive()     GPS error 500
                 buildReport()          -> Degraded (imprecise)
                 beginExtraTime()       EXTRA TIME, wake up at 8.0
  second 3.0   whenReadingsArrive()     GPS error 8
                 buildReport()          -> Available, Recovered
                 finishWith()

WHAT THE REST OF THE ENGINE DOES WITH THIS

  Engine.cpp keeps the requests that are running. Every time something
  happens it tells each of them, and then asks who is finished, whether the
  GPS is still wanted by someone, and when the next wake up must be.
*/

import {
  Report,
  ReportStatus,
  buildReport,
  calculateQualityScore,
  LocationPermission,
  PermissionPrecision,
  ReasonCode as ReportReason,
  ScoreInput,
  ScoreLimit,
  WhatTheAppAskedFor,
  WhatTheRequestKnows,
} from './Report.study.ts';
import {
  PhoneActivityConfidence,
  PhoneActivityHistory,
} from './PhoneActivityTimeline.study.ts';
import {
  readingNorth,
  type LocationReading,
} from './ReadingSelection.study.ts';
import { PhoneActivity } from './UncertaintyModel.study.ts';

// ProximityRequest.hpp

export enum LocationProviderState {
  Available,
  Unavailable,
  PlayServicesMissing,
}

export enum PhoneActivitySupport {
  Available,
  NotSupported,
  PermissionDenied,
  PermissionNotAsked,
}

export class DeviceState {
  locationPermission: LocationPermission = LocationPermission.NotDetermined;
  permissionPrecision: PermissionPrecision = PermissionPrecision.Unknown;
  locationIsTurnedOn: boolean = false;
  locationProvider: LocationProviderState = LocationProviderState.Unavailable;
  appIsInForeground: boolean = false;
  phoneActivity: PhoneActivitySupport = PhoneActivitySupport.NotSupported;
}

export class RequestSettings {
  asked: WhatTheAppAskedFor = new WhatTheAppAskedFor();
  totalTimeAllowedMs: number = 12000;
  firstAttemptTimeMs: number = 4500;
  extraTimeMs: number = 7000;
  device: DeviceState = new DeviceState();
}

export class NotesAboutNewReadings {
  howManyArrived: number = 0;
  howManyWereKept: number = 0;
  reasons: Set<ReasonCode> = new Set();
}

export enum WhyTheRequestStopped {
  CancelledByTheApp,
  AppWentToBackground,
  SessionWasClosed,
  LocationPermissionWasLost,
  LocationProviderFailed,
}

// In C++ the history is a class (LocationHistory). Here it is the list of
// readings, oldest first, plus the arrival number the next reading will get.
export type HistoryRightNow = Readonly<{
  readings: readonly LocationReading[];
  arrivalNumberOfNextReading: number;
}>;

export enum RequestStep {
  Started = 'started',
  FirstAttempt = 'first_attempt',
  ExtraTime = 'extra_time',
  AnswerFromHistory = 'answer_from_history',
  Finished = 'finished',
}

// The reasons this file adds, on top of the ones Report.study.ts knows.
// The values are the public texts.
export enum RequestReason {
  LocationServicesDisabled = 'location_services_disabled',
  MotionPermissionDenied = 'motion_permission_denied',
  MotionUnavailable = 'motion_unavailable',
  PlayServicesUnavailable = 'play_services_unavailable',
  AppBackgrounded = 'app_backgrounded',
  ForegroundRequired = 'foreground_required',
  InitialFixTimeout = 'initial_fix_timeout',
  RecoveryWindowTimeout = 'recovery_window_timeout',
  OverallDeadlineReached = 'overall_deadline_reached',
  NativeProviderUnavailable = 'native_provider_unavailable',
  LocationInvalidTimestamp = 'location_invalid_timestamp',
  LocationAgeUnreliable = 'location_age_unreliable',
  RequestCancelled = 'request_cancelled',
  SessionStopped = 'session_stopped',
}

export type ReasonCode = ReportReason | RequestReason | string;

// The final report of a request: the report of Report.study.ts plus
// the notes of the request.
export class FinishedReport {
  built: Report = new Report();
  requestId: number = 0;
  sessionId: number = 0;
  reasons: Set<ReasonCode> = new Set();
  steps: RequestStep[] = [];
  firstAttemptTimedOut: boolean = false;
  extraTimeWasUsed: boolean = false;
  fallbackCause: ReasonCode | undefined = undefined;
  readingsReceived: number = 0;
  readingsKept: number = 0;
  durationMs: number = 0;
  ageIsUnreliable: boolean = false;
}

enum Phase {
  FirstAttempt,
  ExtraTime,
  Finished,
}

// ProximityRequest.cpp

function notNegative(milliseconds: number): number {
  return milliseconds > 0 ? milliseconds : 0;
}

function findWhyTheAnswerIsNotGoodEnough(report: Report): ReasonCode {
  let why: ReasonCode = ReportReason.NoUsableFix;

  if (report.reasons.has(ReportReason.LocationLowAccuracy)) {
    why = ReportReason.LocationLowAccuracy;
  } else if (report.reasons.has(ReportReason.PossibleMockLocation)) {
    why = ReportReason.PossibleMockLocation;
  } else if (report.reasons.has(ReportReason.PrecisionReduced)) {
    why = ReportReason.PrecisionReduced;
  } else if (report.reasons.has(ReportReason.SuspectedGpsJump)) {
    why = ReportReason.SuspectedGpsJump;
  } else if (report.reasons.has(ReportReason.LocationStale)) {
    why = ReportReason.LocationStale;
  }

  return why;
}

function rememberedReadingIsGoodEnough(
  remembered: Report,
  asked: WhatTheAppAskedFor
): boolean {
  const isPreciseEnough =
    remembered.gpsErrorMeters <= asked.worstGpsErrorAcceptedMeters;
  const looksLikeAJump = remembered.score.wasLimitedBy(
    ScoreLimit.SuspectedJump
  );
  const blockedForBeingSimulated = remembered.score.wasLimitedBy(
    ScoreLimit.MockLocation
  );

  return (
    remembered.hasPosition &&
    remembered.phoneActivity.standingStillSinceTheReading &&
    isPreciseEnough &&
    !looksLikeAJump &&
    !blockedForBeingSimulated
  );
}

function reportWithNothing(settings: RequestSettings): Report {
  const report = new Report();
  report.radiusMeters = settings.asked.radiusMeters;
  report.permissionPrecision = settings.device.permissionPrecision;
  report.score = calculateQualityScore(new ScoreInput());
  return report;
}

export class ProximityRequest {
  private requestId_: number = 0;
  private sessionId_: number = 0;
  private settings_: RequestSettings = new RequestSettings();

  private phase_: Phase = Phase.Finished;

  private startedAtMs_: number = 0;
  private mustFinishAtMs_: number = 0;
  private currentPhaseEndsAtMs_: number = 0;

  private whatThisRequestKnows_: WhatTheRequestKnows =
    new WhatTheRequestKnows();

  private steps_: RequestStep[] = [];
  private firstAttemptTimedOut_: boolean = false;
  private extraTimeWasUsed_: boolean = false;
  private fallbackCause_: ReasonCode | undefined = undefined;
  private readingsReceived_: number = 0;
  private readingsKept_: number = 0;
  private reasons_: Set<ReasonCode> = new Set();

  private report_: FinishedReport = new FinishedReport();

  start(
    requestId: number,
    sessionId: number,
    settings: RequestSettings,
    history: HistoryRightNow,
    activityHistory: PhoneActivityHistory,
    nowMs: number
  ): void {
    // Forget everything from the request that used this object before.
    Object.assign(this, new ProximityRequest());

    this.requestId_ = requestId;
    this.sessionId_ = sessionId;
    this.settings_ = settings;
    this.settings_.totalTimeAllowedMs = notNegative(
      settings.totalTimeAllowedMs
    );
    this.settings_.firstAttemptTimeMs = notNegative(
      settings.firstAttemptTimeMs
    );
    this.settings_.extraTimeMs = notNegative(settings.extraTimeMs);

    this.startedAtMs_ = nowMs;
    this.mustFinishAtMs_ = nowMs + this.settings_.totalTimeAllowedMs;

    this.whatThisRequestKnows_.firstArrivalNumberOfThisRequest =
      history.arrivalNumberOfNextReading;
    this.whatThisRequestKnows_.locationPermission =
      settings.device.locationPermission;
    this.whatThisRequestKnows_.permissionPrecision =
      settings.device.permissionPrecision;

    this.rememberStep(RequestStep.Started);

    if (settings.asked.usePhoneActivity) {
      const support = settings.device.phoneActivity;
      if (support === PhoneActivitySupport.PermissionDenied) {
        this.reasons_.add(RequestReason.MotionPermissionDenied);
      } else if (support !== PhoneActivitySupport.Available) {
        this.reasons_.add(RequestReason.MotionUnavailable);
      }
    }

    // Can the GPS be used at all?
    const device = settings.device;
    const permissionIsGranted =
      device.locationPermission === LocationPermission.Granted;
    let gpsCanBeUsed = true;

    if (!permissionIsGranted) {
      gpsCanBeUsed = false;
    } else if (!device.locationIsTurnedOn) {
      gpsCanBeUsed = false;
      this.reasons_.add(RequestReason.LocationServicesDisabled);
    } else if (
      device.locationProvider === LocationProviderState.PlayServicesMissing
    ) {
      gpsCanBeUsed = false;
      this.reasons_.add(RequestReason.PlayServicesUnavailable);
    } else if (device.locationProvider === LocationProviderState.Unavailable) {
      gpsCanBeUsed = false;
      this.reasons_.add(RequestReason.NativeProviderUnavailable);
    } else if (!device.appIsInForeground) {
      gpsCanBeUsed = false;
      this.reasons_.add(RequestReason.ForegroundRequired);
    }

    if (!gpsCanBeUsed) {
      this.finishWithWhatWeRemember(history, activityHistory, nowMs);
      return;
    }

    // The first attempt begins.
    this.phase_ = Phase.FirstAttempt;
    this.rememberStep(RequestStep.FirstAttempt);
    this.currentPhaseEndsAtMs_ = nowMs + this.settings_.firstAttemptTimeMs;

    this.whenTimePasses(history, activityHistory, nowMs);
  }

  whenReadingsArrive(
    history: HistoryRightNow,
    activityHistory: PhoneActivityHistory,
    nowMs: number,
    notes: NotesAboutNewReadings
  ): void {
    if (this.isFinished()) {
      return;
    }

    this.readingsReceived_ += notes.howManyArrived;
    this.readingsKept_ += notes.howManyWereKept;
    for (const reason of notes.reasons) {
      this.reasons_.add(reason);
    }

    if (this.theTotalTimeIsOver(nowMs)) {
      this.whenTimePasses(history, activityHistory, nowMs);
      return;
    }

    const reportRightNow = buildReport(
      history.readings,
      activityHistory,
      nowMs,
      this.settings_.asked,
      this.whatThisRequestKnows_
    );

    const answerIsGoodEnough =
      reportRightNow.status === ReportStatus.Available;
    const stillInTheFirstAttempt = this.phase_ === Phase.FirstAttempt;
    const somethingArrived = notes.howManyArrived > 0;
    const thereIsExtraTime = this.settings_.extraTimeMs > 0;

    if (answerIsGoodEnough) {
      this.finishWith(reportRightNow, nowMs);
    } else if (stillInTheFirstAttempt && somethingArrived && thereIsExtraTime) {
      this.fallbackCause_ = findWhyTheAnswerIsNotGoodEnough(reportRightNow);
      this.beginExtraTime(history, nowMs);
    }
  }

  whenTimePasses(
    history: HistoryRightNow,
    activityHistory: PhoneActivityHistory,
    nowMs: number
  ): void {
    if (this.isFinished()) {
      return;
    }

    const currentPhaseIsOver = nowMs >= this.currentPhaseEndsAtMs_;
    const firstAttemptIsOver =
      this.phase_ === Phase.FirstAttempt && currentPhaseIsOver;
    const extraTimeIsOver =
      this.phase_ === Phase.ExtraTime && currentPhaseIsOver;

    if (this.theTotalTimeIsOver(nowMs)) {
      this.reasons_.add(RequestReason.OverallDeadlineReached);

      const noGoodReadingInTheFirstAttempt = this.phase_ === Phase.FirstAttempt;
      if (noGoodReadingInTheFirstAttempt) {
        this.firstAttemptTimedOut_ = true;
        this.reasons_.add(RequestReason.InitialFixTimeout);
        this.fallbackCause_ = RequestReason.InitialFixTimeout;
      }

      this.finishWithWhatWeRemember(history, activityHistory, nowMs);
    } else if (firstAttemptIsOver) {
      this.firstAttemptTimedOut_ = true;
      this.reasons_.add(RequestReason.InitialFixTimeout);
      this.fallbackCause_ = RequestReason.InitialFixTimeout;

      const remembered = buildReport(
        history.readings,
        activityHistory,
        nowMs,
        this.settings_.asked,
        this.whatThisRequestKnows_
      );

      const thereIsNoExtraTime = this.settings_.extraTimeMs === 0;

      if (
        rememberedReadingIsGoodEnough(remembered, this.settings_.asked) ||
        thereIsNoExtraTime
      ) {
        this.finishWithWhatWeRemember(history, activityHistory, nowMs);
      } else {
        this.beginExtraTime(history, nowMs);
      }
    } else if (extraTimeIsOver) {
      this.reasons_.add(RequestReason.RecoveryWindowTimeout);
      this.finishWithWhatWeRemember(history, activityHistory, nowMs);
    }
  }

  stop(
    why: WhyTheRequestStopped,
    history: HistoryRightNow,
    activityHistory: PhoneActivityHistory,
    nowMs: number
  ): void {
    if (this.isFinished()) {
      return;
    }

    if (why === WhyTheRequestStopped.LocationProviderFailed) {
      this.reasons_.add(RequestReason.NativeProviderUnavailable);
      this.finishWithWhatWeRemember(history, activityHistory, nowMs);
      return;
    }

    const report = reportWithNothing(this.settings_);

    if (why === WhyTheRequestStopped.CancelledByTheApp) {
      report.status = ReportStatus.Cancelled;
      this.reasons_.add(RequestReason.RequestCancelled);
    } else if (why === WhyTheRequestStopped.AppWentToBackground) {
      report.status = ReportStatus.Cancelled;
      this.reasons_.add(RequestReason.AppBackgrounded);
    } else if (why === WhyTheRequestStopped.SessionWasClosed) {
      report.status = ReportStatus.Cancelled;
      this.reasons_.add(RequestReason.SessionStopped);
    } else {
      report.status = ReportStatus.Unavailable;
      this.reasons_.add(ReportReason.PermissionDenied);
    }

    this.finishWith(report, nowMs);
  }

  isFinished(): boolean {
    return this.phase_ === Phase.Finished;
  }

  wantsLocationUpdates(): boolean {
    return !this.isFinished();
  }

  wantsPhoneActivityUpdates(): boolean {
    const phoneCanReportItsActivity =
      this.settings_.device.phoneActivity === PhoneActivitySupport.Available;

    return (
      !this.isFinished() &&
      this.settings_.asked.usePhoneActivity &&
      phoneCanReportItsActivity
    );
  }

  nextWakeUpAtMs(): number {
    let wakeUpAtMs = this.currentPhaseEndsAtMs_;
    if (this.mustFinishAtMs_ < wakeUpAtMs) {
      wakeUpAtMs = this.mustFinishAtMs_;
    }
    return wakeUpAtMs;
  }

  report(): FinishedReport {
    return this.report_;
  }

  private theTotalTimeIsOver(nowMs: number): boolean {
    return nowMs >= this.mustFinishAtMs_;
  }

  private rememberStep(step: RequestStep): void {
    this.steps_.push(step);
  }

  private beginExtraTime(history: HistoryRightNow, nowMs: number): void {
    this.phase_ = Phase.ExtraTime;
    this.extraTimeWasUsed_ = true;
    this.rememberStep(RequestStep.ExtraTime);

    this.whatThisRequestKnows_.firstArrivalNumberOfExtraTime =
      history.arrivalNumberOfNextReading;

    this.currentPhaseEndsAtMs_ = nowMs + this.settings_.extraTimeMs;
  }

  private finishWithWhatWeRemember(
    history: HistoryRightNow,
    activityHistory: PhoneActivityHistory,
    nowMs: number
  ): void {
    this.rememberStep(RequestStep.AnswerFromHistory);

    const report = buildReport(
      history.readings,
      activityHistory,
      nowMs,
      this.settings_.asked,
      this.whatThisRequestKnows_
    );

    this.finishWith(report, nowMs);
  }

  private finishWith(built: Report, nowMs: number): void {
    this.rememberStep(RequestStep.Finished);

    const report = new FinishedReport();
    report.built = built;
    report.requestId = this.requestId_;
    report.sessionId = this.sessionId_;
    report.steps = [...this.steps_];
    report.firstAttemptTimedOut = this.firstAttemptTimedOut_;
    report.extraTimeWasUsed = this.extraTimeWasUsed_;
    report.fallbackCause = this.fallbackCause_;
    report.readingsReceived = this.readingsReceived_;
    report.readingsKept = this.readingsKept_;
    report.durationMs = notNegative(nowMs - this.startedAtMs_);
    report.reasons = new Set([...built.reasons, ...this.reasons_]);

    const readingsHadBrokenTime =
      this.reasons_.has(RequestReason.LocationAgeUnreliable) ||
      this.reasons_.has(RequestReason.LocationInvalidTimestamp);
    report.ageIsUnreliable = !built.hasPosition && readingsHadBrokenTime;

    this.report_ = report;
    this.phase_ = Phase.Finished;
  }
}

// Examples. The expected values are the ones the C++ tests check.

export function phoneWhereEverythingWorks(): DeviceState {
  const device = new DeviceState();
  device.locationPermission = LocationPermission.Granted;
  device.permissionPrecision = PermissionPrecision.Precise;
  device.locationIsTurnedOn = true;
  device.locationProvider = LocationProviderState.Available;
  device.appIsInForeground = true;
  device.phoneActivity = PhoneActivitySupport.NotSupported;
  return device;
}

function usualSettings(): RequestSettings {
  const settings = new RequestSettings();
  settings.asked.usePhoneActivity = false;
  settings.device = phoneWhereEverythingWorks();
  return settings;
}

// One request together with the memory it reads from.
class PhoneWithOneRequest {
  readings: LocationReading[] = [];
  phoneActivity: PhoneActivityHistory = new PhoneActivityHistory();
  request: ProximityRequest = new ProximityRequest();

  history(): HistoryRightNow {
    return {
      readings: this.readings,
      arrivalNumberOfNextReading: this.readings.length + 1,
    };
  }

  start(seconds: number, settings: RequestSettings = usualSettings()): void {
    this.request.start(
      7,
      0,
      settings,
      this.history(),
      this.phoneActivity,
      seconds * 1000
    );
  }

  gpsDelivers(seconds: number, metersNorth: number, gpsError: number): void {
    this.readings.push(
      readingNorth(seconds, metersNorth, gpsError, this.readings.length + 1)
    );
    const notes = new NotesAboutNewReadings();
    notes.howManyArrived = 1;
    notes.howManyWereKept = 1;
    this.request.whenReadingsArrive(
      this.history(),
      this.phoneActivity,
      seconds * 1000,
      notes
    );
  }

  timePasses(seconds: number): void {
    this.request.whenTimePasses(
      this.history(),
      this.phoneActivity,
      seconds * 1000
    );
  }

  stop(seconds: number, why: WhyTheRequestStopped): void {
    this.request.stop(why, this.history(), this.phoneActivity, seconds * 1000);
  }
}

function show(
  text: string,
  phone: PhoneWithOneRequest,
  expected: Readonly<{
    status: ReportStatus;
    steps: string;
    durationMs: number;
  }>
): void {
  const report = phone.request.report();
  const steps = report.steps.join(', ');

  const upToDate =
    phone.request.isFinished() &&
    report.built.status === expected.status &&
    steps === expected.steps &&
    report.durationMs === expected.durationMs;
  if (!upToDate) {
    throw new Error(
      `${text}: out of date, the C++ tests expect another report`
    );
  }

  console.log(text);
  console.log(
    `  ${ReportStatus[report.built.status]} after ${report.durationMs} ms` +
      `   score ${report.built.score.qualityScore}`
  );
  console.log(`  steps: ${steps}`);
  console.log(`  reasons: ${[...report.reasons].join(', ')}`);
}

if (import.meta.main) {
  const quick = new PhoneWithOneRequest();
  quick.start(0);
  quick.gpsDelivers(1.2, 20, 8);
  show('a good reading arrives quickly', quick, {
    status: ReportStatus.Available,
    steps: 'started, first_attempt, finished',
    durationMs: 1200,
  });

  const weakThenGood = new PhoneWithOneRequest();
  weakThenGood.start(0);
  weakThenGood.gpsDelivers(1, 20, 500);
  weakThenGood.gpsDelivers(3, 20, 8);
  show('the first reading is weak, a good one comes later', weakThenGood, {
    status: ReportStatus.Available,
    steps: 'started, first_attempt, extra_time, finished',
    durationMs: 3000,
  });

  const nothing = new PhoneWithOneRequest();
  nothing.start(0);
  nothing.timePasses(4.5);
  nothing.timePasses(11.5);
  show('the GPS never answers and nothing is remembered', nothing, {
    status: ReportStatus.Unavailable,
    steps: 'started, first_attempt, extra_time, answer_from_history, finished',
    durationMs: 11500,
  });

  const remembered = new PhoneWithOneRequest();
  remembered.readings.push(readingNorth(0, 20, 8, 1));
  remembered.phoneActivity.store({
    startedAtMs: 0,
    endedAtMs: 30000,
    activity: PhoneActivity.StandingStill,
    confidence: PhoneActivityConfidence.High,
  });
  const withActivity = usualSettings();
  withActivity.asked.usePhoneActivity = true;
  withActivity.device.phoneActivity = PhoneActivitySupport.Available;
  remembered.start(10, withActivity);
  remembered.timePasses(14.5);
  show(
    'nothing new, but the phone stood still since a remembered reading',
    remembered,
    {
      status: ReportStatus.Degraded,
      steps: 'started, first_attempt, answer_from_history, finished',
      durationMs: 4500,
    }
  );

  const cancelled = new PhoneWithOneRequest();
  cancelled.start(0);
  cancelled.stop(2, WhyTheRequestStopped.CancelledByTheApp);
  cancelled.gpsDelivers(2.5, 20, 8);
  show('the app cancels, and a late reading changes nothing', cancelled, {
    status: ReportStatus.Cancelled,
    steps: 'started, first_attempt, finished',
    durationMs: 2000,
  });
}
