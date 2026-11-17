// Study file: TypeScript mirror of Report.hpp and Report.cpp.
// Same names, same order. Not part of the library. If it ever disagrees with
// the C++, the C++ is right.
// Run: node --no-warnings --experimental-transform-types cpp/src/Report.study.ts
// The examples at the end only run when this file is the one being run.

/*
THE BUSINESS RULE

  The app asks: "is the courier near this address?". The engine answers with
  a report (a report), never with a plain yes or no:

    - which GPS reading was used, how old it is, where it came from;
    - how far the phone could really be from that reading;
    - what the evidence says about the delivery radius: inside, outside, or
      we cannot tell;
    - a quality score from 0 to 100 (an index, never a probability);
    - the reasons, as short fixed labels.

  This file builds the report from what is in memory at one moment. It does
  not wait for the GPS.

THE ORDER OF THE CALLS

  buildReport(history, activityHistory, nowMs, asked, request)
  │
  ├─ no location permission?  ->  report "unavailable" and stop
  │
  ├─ chooseBestReading()                    STEP 1  (ReadingSelection)
  │    no reading can be used?  ->  report "unavailable" and stop
  │
  ├─ findWhereTheReadingCameFrom()          STEP 2  fresh, recovered or cached
  ├─ distanceBetweenMeters()                STEP 3
  │  classifyRelation()                             (ProximityRelation)
  ├─ calculateQualityScore()                STEP 4  (QualityScore)
  ├─ decideTheStatus()                      STEP 5  available or degraded
  ├─ describePhoneActivity()                STEP 6
  └─ writeDownTheReasons()

ONE CALL, STEP BY STEP

  The address is at the door, the delivery radius is 100 meters.
  One reading: obtained by this request, 2 seconds old, GPS error 10,
               30 meters from the address.
  The phone reported "standing still" during those 2 seconds.

    buildReport()
      chooseBestReading()              -> that reading
                                          could have moved 0.5 x 2 = 1 meter
                                          how far the phone could be = 11 meters
      findWhereTheReadingCameFrom()    -> Fresh
      classifyRelation(30, 11, 100)    -> SupportsInside  (at most 41 meters away)
      calculateQualityScore()          -> blend 89, limit "single_fix" 75  -> 75
      decideTheStatus()                -> Available

WHAT THE REST OF THE ENGINE DOES WITH THE ANSWER

  ProximityRequest.cpp calls buildReport() every time new readings
  arrive, to see whether the answer is already good enough to stop, and one
  last time when the time is over. The platform code turns the final report
  into the object the app receives in JavaScript.
*/

import {
  distanceBetweenMeters,
  JumpCheckResult,
} from './JumpDetector.study.ts';
import {
  findPhoneActivityAtMoment,
  findSinceWhenPhoneActivityIsKnown,
  PhoneActivityConfidence,
  PhoneActivityHistory,
} from './PhoneActivityTimeline.study.ts';
import {
  chooseBestReading,
  ChosenReading,
  MockSignal,
  readingNorth,
  RulesForChoosingReading,
  type LocationReading,
  type WhatWeKnowAboutReading,
} from './ReadingSelection.study.ts';
import {
  FASTEST_POSSIBLE_SPEED_METERS_PER_SECOND,
  PhoneActivity,
} from './UncertaintyModel.study.ts';
import type { Coordinates } from './JumpDetector.study.ts';

// Report.hpp

export enum ReportStatus {
  Available,
  Degraded,
  Unavailable,
  Cancelled,
}

export enum AgeReliability {
  NotApplicable,
  Monotonic,
  Estimated,
  Unreliable,
}

export enum LocationPermission {
  NotDetermined,
  Denied,
  Restricted,
  Granted,
}

export enum PermissionPrecision {
  Precise,
  Approximate,
  Unknown,
}

export class WhatTheAppAskedFor {
  destination: Coordinates = { latitudeDegrees: 0.0, longitudeDegrees: 0.0 };
  radiusMeters: number = 100.0;
  newestAgeThatCountsAsFreshMs: number = 10000;
  oldestAgeAllowedMs: number = 60000;
  worstGpsErrorAcceptedMeters: number = 50.0;
  fastestPossibleSpeedMetersPerSecond: number =
    FASTEST_POSSIBLE_SPEED_METERS_PER_SECOND;
  usePhoneActivity: boolean = true;
  allowAnswerFromHistory: boolean = true;
  mockPolicy: MockPolicy = MockPolicy.ZeroScore;
}

export class WhatTheRequestKnows {
  firstArrivalNumberOfThisRequest: number = 1;
  firstArrivalNumberOfExtraTime: number = 0;
  locationPermission: LocationPermission = LocationPermission.Granted;
  permissionPrecision: PermissionPrecision = PermissionPrecision.Precise;
}

export class PhoneActivitySummary {
  activityNow: PhoneActivity = PhoneActivity.Unknown;
  confidenceNow: PhoneActivityConfidence = PhoneActivityConfidence.Unknown;
  standingStillSinceTheReading: boolean = false;
  knownUntilNow: boolean = false;
  knownSinceAgeMs: number = 0;
  hasGaps: boolean = false;
}

export class Report {
  status: ReportStatus = ReportStatus.Unavailable;
  source: FixSource = FixSource.Unavailable;

  hasPosition: boolean = false;
  position: Coordinates = { latitudeDegrees: 0.0, longitudeDegrees: 0.0 };
  distanceToDestinationMeters: number = 0.0;
  gpsErrorMeters: number = 0.0;
  howFarPhoneCouldBeMeters: number = 0.0;
  readingAgeMs: number = 0;
  mockSignal: MockSignal = MockSignal.Unknown;

  radiusMeters: number = 0.0;
  ageReliability: AgeReliability = AgeReliability.NotApplicable;
  permissionPrecision: PermissionPrecision = PermissionPrecision.Unknown;

  relation: EvidenceRelation = EvidenceRelation.Inconclusive;
  score: ScoreResult = new ScoreResult();
  phoneActivity: PhoneActivitySummary = new PhoneActivitySummary();
  reasons: Set<ReasonCode> = new Set();

  readingsExamined: number = 0;
  gpsJumpsFound: number = 0;
}

// Report.cpp

function reasonForMissingPermission(
  permission: LocationPermission
): ReasonCode {
  let reason = ReasonCode.PermissionDenied;

  if (permission === LocationPermission.NotDetermined) {
    reason = ReasonCode.PermissionNotDetermined;
  } else if (permission === LocationPermission.Restricted) {
    reason = ReasonCode.PermissionRestricted;
  }

  return reason;
}

function findWhereTheReadingCameFrom(
  best: WhatWeKnowAboutReading,
  asked: WhatTheAppAskedFor,
  request: WhatTheRequestKnows
): FixSource {
  const arrivalNumber = best.reading.arrivalNumber;

  const thisRequestObtainedIt =
    arrivalNumber >= request.firstArrivalNumberOfThisRequest;
  const extraTimeWasUsed = request.firstArrivalNumberOfExtraTime !== 0;
  const arrivedDuringExtraTime =
    extraTimeWasUsed && arrivalNumber >= request.firstArrivalNumberOfExtraTime;
  const isNotOld = best.ageMs <= asked.newestAgeThatCountsAsFreshMs;

  let source = FixSource.Cached;

  if (thisRequestObtainedIt && isNotOld && arrivedDuringExtraTime) {
    source = FixSource.Recovered;
  } else if (thisRequestObtainedIt && isNotOld) {
    source = FixSource.Fresh;
  }

  return source;
}

function decideTheStatus(
  report: Report,
  asked: WhatTheAppAskedFor,
  isProbablyAGpsJump: boolean,
  blockedForBeingSimulated: boolean,
  locationIsOnlyApproximate: boolean
): ReportStatus {
  const wasObtainedNow =
    report.source === FixSource.Fresh ||
    report.source === FixSource.Recovered;
  const isPreciseEnough =
    report.gpsErrorMeters <= asked.worstGpsErrorAcceptedMeters;

  const everythingIsFine =
    wasObtainedNow &&
    isPreciseEnough &&
    !isProbablyAGpsJump &&
    !blockedForBeingSimulated &&
    !locationIsOnlyApproximate;

  return everythingIsFine
    ? ReportStatus.Available
    : ReportStatus.Degraded;
}

function describePhoneActivity(
  activityHistory: PhoneActivityHistory,
  best: WhatWeKnowAboutReading | undefined,
  nowMs: number,
  asked: WhatTheAppAskedFor
): PhoneActivitySummary {
  const summary = new PhoneActivitySummary();

  if (!asked.usePhoneActivity) {
    return summary;
  }

  const rightNow = findPhoneActivityAtMoment(activityHistory, nowMs);
  summary.activityNow = rightNow.activity;
  summary.confidenceNow = rightNow.confidence;

  const thereIsAReading = best !== undefined;
  if (thereIsAReading) {
    const table = best.timeInEachActivity;

    summary.standingStillSinceTheReading = table.standingStillTheWholeTime();

    const knownSinceMs = findSinceWhenPhoneActivityIsKnown(
      activityHistory,
      best.reading.capturedAtMs,
      nowMs
    );
    summary.knownUntilNow = knownSinceMs < nowMs;
    if (summary.knownUntilNow) {
      summary.knownSinceAgeMs = nowMs - knownSinceMs;
    }

    const partOfTheTimeIsUnknown = table.millisecondsUnknown > 0;
    const partOfTheTimeIsKnown = !table.weKnowNothing();
    summary.hasGaps = partOfTheTimeIsKnown && partOfTheTimeIsUnknown;
  }

  return summary;
}

function writeDownTheReasons(
  report: Report,
  chosen: ChosenReading,
  best: WhatWeKnowAboutReading,
  asked: WhatTheAppAskedFor,
  locationIsOnlyApproximate: boolean
): void {
  const reasons = report.reasons;

  reasons.add(ReasonCode.UncertaintyModelHeuristic);

  const isImprecise =
    report.gpsErrorMeters > asked.worstGpsErrorAcceptedMeters;
  if (isImprecise) {
    reasons.add(ReasonCode.LocationLowAccuracy);
  }

  const isOld = best.ageMs > asked.newestAgeThatCountsAsFreshMs;
  if (isOld) {
    reasons.add(ReasonCode.LocationStale);
  }

  if (chosen.someReadingsContradictEachOther) {
    reasons.add(ReasonCode.SuspectedGpsJump);
  }
  if (chosen.readingsFromDifferentProvidersContradict) {
    reasons.add(ReasonCode.ProviderConflict);
  }
  if (best.reading.mockSignal === MockSignal.FlaggedByOs) {
    reasons.add(ReasonCode.PossibleMockLocation);
  }
  if (locationIsOnlyApproximate) {
    reasons.add(ReasonCode.PrecisionReduced);
  }
  if (best.gpsSpeedDoesNotMatchTheMovement) {
    reasons.add(ReasonCode.LocationMetadataInconsistent);
  }
  if (report.phoneActivity.hasGaps) {
    reasons.add(ReasonCode.MotionInsufficientCoverage);
  }
  if (best.timeInEachActivity.twoReportsDisagreed) {
    reasons.add(ReasonCode.MotionConflictingEvidence);
  }

  const onlyTheHistoryAndItCannotTell =
    report.source === FixSource.Cached &&
    report.relation === EvidenceRelation.Inconclusive;
  if (onlyTheHistoryAndItCannotTell) {
    reasons.add(ReasonCode.HistoryOnlyInconclusive);
  }
}

export function buildReport(
  history: readonly LocationReading[],
  activityHistory: PhoneActivityHistory,
  nowMs: number,
  asked: WhatTheAppAskedFor,
  request: WhatTheRequestKnows
): Report {
  const report = new Report();
  report.radiusMeters = asked.radiusMeters;
  report.permissionPrecision = request.permissionPrecision;
  report.readingsExamined = history.length;
  report.score = calculateQualityScore(new ScoreInput());

  const permissionIsGranted =
    request.locationPermission === LocationPermission.Granted;
  if (!permissionIsGranted) {
    report.reasons.add(
      reasonForMissingPermission(request.locationPermission)
    );
    return report;
  }

  // Step 1
  const rules = new RulesForChoosingReading();
  rules.oldestAgeAllowedMs = asked.oldestAgeAllowedMs;
  rules.fastestPossibleSpeedMetersPerSecond =
    asked.fastestPossibleSpeedMetersPerSecond;
  rules.usePhoneActivity = asked.usePhoneActivity;
  if (!asked.allowAnswerFromHistory) {
    rules.firstArrivalNumberAllowed = request.firstArrivalNumberOfThisRequest;
  }

  const chosen = chooseBestReading(history, activityHistory, nowMs, rules);
  report.gpsJumpsFound = chosen.howManyAreProbablyGpsJumps;

  const best = chosen.best;
  if (best === undefined) {
    report.reasons.add(ReasonCode.NoUsableFix);
    if (history.length === 0) {
      report.reasons.add(ReasonCode.HistoryEmpty);
    }
    if (chosen.howManyWereTooOld > 0) {
      report.reasons.add(ReasonCode.LocationStale);
    }
    report.phoneActivity = describePhoneActivity(
      activityHistory,
      undefined,
      nowMs,
      asked
    );
    return report;
  }

  report.hasPosition = true;
  report.position = best.reading.position;
  report.gpsErrorMeters = best.reading.horizontalAccuracyMeters;
  report.howFarPhoneCouldBeMeters = best.howFarPhoneCouldBeMeters;
  report.readingAgeMs = best.ageMs;
  report.mockSignal = best.reading.mockSignal;
  report.ageReliability = best.reading.captureTimeWasEstimated
    ? AgeReliability.Estimated
    : AgeReliability.Monotonic;

  const isProbablyAGpsJump =
    best.jumpCheck.result === JumpCheckResult.ProbablyGpsJump;
  const someReadingContradictsIt = best.jumpCheck.readingsAgainstIt > 0;
  const blockedForBeingSimulated =
    best.reading.mockSignal === MockSignal.FlaggedByOs &&
    asked.mockPolicy === MockPolicy.ZeroScore;
  const locationIsOnlyApproximate =
    best.reading.onlyApproximatePermission ||
    request.permissionPrecision === PermissionPrecision.Approximate;

  // Step 2
  report.source = findWhereTheReadingCameFrom(best, asked, request);

  // Step 3
  report.distanceToDestinationMeters = distanceBetweenMeters(
    best.reading.position,
    asked.destination
  );

  const gates = new RelationGates();
  gates.hasCandidate = true;
  gates.ageReliable = true;
  gates.withinFallbackAge = true;
  gates.permissionAuthorized = true;
  gates.strongJumpSuspicion = isProbablyAGpsJump;
  gates.blockedByMockPolicy = blockedForBeingSimulated;

  report.relation = classifyRelation(
    report.distanceToDestinationMeters,
    report.howFarPhoneCouldBeMeters,
    asked.radiusMeters,
    gates
  );

  // Step 4
  const scoreInput = new ScoreInput();
  scoreInput.hasCandidate = true;
  scoreInput.ageReliable = true;
  scoreInput.horizontalAccuracyMeters = best.reading.horizontalAccuracyMeters;
  scoreInput.ageMs = best.ageMs;
  scoreInput.possibleMovementMeters = best.distanceMoved.meters;
  scoreInput.movementIsUnknown = best.distanceMoved.weOnlyAssumedTheWorst();
  scoreInput.consistency = consistencyFromAgreeingReadings(
    best.jumpCheck.readingsOnItsSide,
    someReadingContradictsIt
  );
  scoreInput.source = report.source;
  scoreInput.onlyApproximatePermission = locationIsOnlyApproximate;
  scoreInput.suspectedJump = isProbablyAGpsJump;
  scoreInput.mockSignal = best.reading.mockSignal;
  scoreInput.mockPolicy = asked.mockPolicy;

  report.score = calculateQualityScore(scoreInput);

  // Step 5
  report.status = decideTheStatus(
    report,
    asked,
    isProbablyAGpsJump,
    blockedForBeingSimulated,
    locationIsOnlyApproximate
  );

  // Step 6
  report.phoneActivity = describePhoneActivity(
    activityHistory,
    best,
    nowMs,
    asked
  );
  writeDownTheReasons(
    report,
    chosen,
    best,
    asked,
    locationIsOnlyApproximate
  );

  return report;
}

// Borrowed from Types.hpp, ReasonCodes.hpp, ProximityRelation.cpp and
// QualityScore.cpp. Those files have no study mirror of their own.

export enum MockPolicy {
  ZeroScore,
  FlagOnly,
}

export enum FixSource {
  Fresh,
  Recovered,
  Cached,
  Unavailable,
}

// Only the reasons this file writes down, with their public text.
export enum ReasonCode {
  PermissionNotDetermined = 'permission_not_determined',
  PermissionDenied = 'permission_denied',
  PermissionRestricted = 'permission_restricted',
  PrecisionReduced = 'precision_reduced',
  MotionInsufficientCoverage = 'motion_insufficient_coverage',
  LocationStale = 'location_stale',
  LocationLowAccuracy = 'location_low_accuracy',
  HistoryEmpty = 'history_empty',
  HistoryOnlyInconclusive = 'history_only_inconclusive',
  SuspectedGpsJump = 'suspected_gps_jump',
  PossibleMockLocation = 'possible_mock_location',
  NoUsableFix = 'no_usable_fix',
  LocationMetadataInconsistent = 'location_metadata_inconsistent',
  ProviderConflict = 'provider_conflict',
  MotionConflictingEvidence = 'motion_conflicting_evidence',
  UncertaintyModelHeuristic = 'uncertainty_model_heuristic',
}

export enum EvidenceRelation {
  SupportsInside,
  SupportsOutside,
  Inconclusive,
}

export class RelationGates {
  hasCandidate: boolean = false;
  ageReliable: boolean = false;
  withinFallbackAge: boolean = false;
  permissionAuthorized: boolean = false;
  strongJumpSuspicion: boolean = false;
  blockedByMockPolicy: boolean = false;
}

export function classifyRelation(
  distanceToDestinationMeters: number,
  uncertaintyMeters: number,
  radiusMeters: number,
  gates: RelationGates
): EvidenceRelation {
  const everyGateIsFine =
    gates.hasCandidate &&
    gates.ageReliable &&
    gates.withinFallbackAge &&
    gates.permissionAuthorized &&
    !gates.strongJumpSuspicion &&
    !gates.blockedByMockPolicy;

  const radiusIsValid =
    Number.isFinite(radiusMeters) &&
    radiusMeters > 0.0 &&
    radiusMeters <= 100000.0;

  const numbersAreUsable =
    Number.isFinite(distanceToDestinationMeters) &&
    distanceToDestinationMeters >= 0.0 &&
    Number.isFinite(uncertaintyMeters) &&
    uncertaintyMeters >= 0.0 &&
    radiusIsValid;

  if (!everyGateIsFine || !numbersAreUsable) {
    return EvidenceRelation.Inconclusive;
  }

  const farthestPossible = distanceToDestinationMeters + uncertaintyMeters;
  let closestPossible = distanceToDestinationMeters - uncertaintyMeters;
  if (closestPossible < 0.0) {
    closestPossible = 0.0;
  }

  if (farthestPossible <= radiusMeters) {
    return EvidenceRelation.SupportsInside;
  }
  if (closestPossible > radiusMeters) {
    return EvidenceRelation.SupportsOutside;
  }
  return EvidenceRelation.Inconclusive;
}

export enum Consistency {
  OneReadingOnly,
  TwoAgree,
  ThreeOrMoreAgree,
  ReadingsDisagree,
}

export function consistencyFromAgreeingReadings(
  differentReadingsThatAgree: number,
  readingsContradictEachOther: boolean
): Consistency {
  if (readingsContradictEachOther) return Consistency.ReadingsDisagree;
  if (differentReadingsThatAgree >= 3) return Consistency.ThreeOrMoreAgree;
  if (differentReadingsThatAgree === 2) return Consistency.TwoAgree;
  return Consistency.OneReadingOnly;
}

export enum ScoreLimit {
  Cached,
  ApproximatePermission,
  UnknownMovementNotFresh,
  UncertaintyAtLeast100,
  UncertaintyAtLeast300,
  SingleFix,
  ConflictingEvidence,
  SuspectedJump,
  MockLocation,
  AgeUnreliable,
  NoCandidate,
}

const HIGHEST_SCORE_ALLOWED_BY: Record<ScoreLimit, number> = {
  [ScoreLimit.Cached]: 60,
  [ScoreLimit.ApproximatePermission]: 30,
  [ScoreLimit.UnknownMovementNotFresh]: 40,
  [ScoreLimit.UncertaintyAtLeast100]: 35,
  [ScoreLimit.UncertaintyAtLeast300]: 15,
  [ScoreLimit.SingleFix]: 75,
  [ScoreLimit.ConflictingEvidence]: 75,
  [ScoreLimit.SuspectedJump]: 20,
  [ScoreLimit.MockLocation]: 0,
  [ScoreLimit.AgeUnreliable]: 0,
  [ScoreLimit.NoCandidate]: 0,
};

export class ScoreInput {
  hasCandidate: boolean = false;
  ageReliable: boolean = false;
  horizontalAccuracyMeters: number = 0.0;
  ageMs: number = 0.0;
  possibleMovementMeters: number = 0.0;
  movementIsUnknown: boolean = true;
  consistency: Consistency = Consistency.OneReadingOnly;
  source: FixSource = FixSource.Unavailable;
  onlyApproximatePermission: boolean = false;
  suspectedJump: boolean = false;
  mockSignal: MockSignal = MockSignal.Unknown;
  mockPolicy: MockPolicy = MockPolicy.ZeroScore;
}

export class ScoreResult {
  hasBreakdown: boolean = false;
  accuracyFactor: number = 0.0;
  freshnessFactor: number = 0.0;
  movementFactor: number = 0.0;
  consistencyFactor: number = 0.0;
  effectiveUncertaintyMeters: number = 0.0;
  rawScore: number = 0;
  qualityScore: number = 0;
  limitsApplied: Set<ScoreLimit> = new Set();

  wasLimitedBy(limit: ScoreLimit): boolean {
    return this.limitsApplied.has(limit);
  }
}

function keepBetweenZeroAndOne(value: number): number {
  if (value < 0.0) return 0.0;
  if (value > 1.0) return 1.0;
  return value;
}

function gradeForConsistency(consistency: Consistency): number {
  switch (consistency) {
    case Consistency.OneReadingOnly:
      return 0.6;
    case Consistency.TwoAgree:
      return 0.8;
    case Consistency.ThreeOrMoreAgree:
      return 1.0;
    case Consistency.ReadingsDisagree:
      return 0.3;
  }
}

function limitScore(result: ScoreResult, limit: ScoreLimit): void {
  result.limitsApplied.add(limit);

  const ceiling = HIGHEST_SCORE_ALLOWED_BY[limit];
  if (result.qualityScore > ceiling) {
    result.qualityScore = ceiling;
  }
}

function isUsableMeasurement(value: number): boolean {
  return Number.isFinite(value) && value >= 0.0;
}

export function roundHalfUp(value: number): number {
  return Math.floor(value + 0.5 + 1e-9);
}

export function calculateQualityScore(input: ScoreInput): ScoreResult {
  const result = new ScoreResult();

  if (!input.hasCandidate) {
    result.limitsApplied.add(ScoreLimit.NoCandidate);
    return result;
  }
  if (!input.ageReliable) {
    result.limitsApplied.add(ScoreLimit.AgeUnreliable);
    return result;
  }

  const numbersAreUsable =
    isUsableMeasurement(input.horizontalAccuracyMeters) &&
    isUsableMeasurement(input.ageMs) &&
    isUsableMeasurement(input.possibleMovementMeters);
  if (!numbersAreUsable) {
    result.limitsApplied.add(ScoreLimit.NoCandidate);
    return result;
  }

  result.hasBreakdown = true;

  // Step 1: the four grades and their blend.
  result.accuracyFactor = keepBetweenZeroAndOne(
    1.0 - input.horizontalAccuracyMeters / 100.0
  );
  result.freshnessFactor = keepBetweenZeroAndOne(1.0 - input.ageMs / 60000.0);
  result.movementFactor = keepBetweenZeroAndOne(
    1.0 - input.possibleMovementMeters / 100.0
  );
  result.consistencyFactor = gradeForConsistency(input.consistency);

  const blendedGrade =
    0.4 * result.accuracyFactor +
    0.25 * result.freshnessFactor +
    0.2 * result.movementFactor +
    0.15 * result.consistencyFactor;

  let scoreBeforeLimits = roundHalfUp(100.0 * blendedGrade);
  if (scoreBeforeLimits < 0) scoreBeforeLimits = 0;
  if (scoreBeforeLimits > 100) scoreBeforeLimits = 100;

  result.rawScore = scoreBeforeLimits;
  result.qualityScore = scoreBeforeLimits;
  result.effectiveUncertaintyMeters =
    input.horizontalAccuracyMeters + input.possibleMovementMeters;

  // Step 2: the limits.
  const readingCameFromHistory = input.source === FixSource.Cached;
  if (readingCameFromHistory) {
    limitScore(result, ScoreLimit.Cached);
    if (input.movementIsUnknown) {
      limitScore(result, ScoreLimit.UnknownMovementNotFresh);
    }
  }
  if (input.onlyApproximatePermission) {
    limitScore(result, ScoreLimit.ApproximatePermission);
  }
  if (result.effectiveUncertaintyMeters >= 100.0) {
    limitScore(result, ScoreLimit.UncertaintyAtLeast100);
  }
  if (result.effectiveUncertaintyMeters >= 300.0) {
    limitScore(result, ScoreLimit.UncertaintyAtLeast300);
  }
  if (input.consistency === Consistency.OneReadingOnly) {
    limitScore(result, ScoreLimit.SingleFix);
  }
  if (input.consistency === Consistency.ReadingsDisagree) {
    limitScore(result, ScoreLimit.ConflictingEvidence);
  }
  if (input.suspectedJump) {
    limitScore(result, ScoreLimit.SuspectedJump);
  }
  const flaggedAsSimulated = input.mockSignal === MockSignal.FlaggedByOs;
  const appWantsZeroForSimulated = input.mockPolicy === MockPolicy.ZeroScore;
  if (flaggedAsSimulated && appWantsZeroForSimulated) {
    limitScore(result, ScoreLimit.MockLocation);
  }

  return result;
}

// Examples. The expected values are the ones the C++ tests check.

function show(
  text: string,
  report: Report,
  expected: Readonly<{
    status: ReportStatus;
    source: FixSource;
    relation: EvidenceRelation;
    rawScore: number;
    qualityScore: number;
  }>
): void {
  const upToDate =
    report.status === expected.status &&
    report.source === expected.source &&
    report.relation === expected.relation &&
    report.score.rawScore === expected.rawScore &&
    report.score.qualityScore === expected.qualityScore;
  if (!upToDate) {
    throw new Error(
      `${text}: out of date, the C++ tests expect another report`
    );
  }

  const limits = [...report.score.limitsApplied]
    .map((limit) => ScoreLimit[limit])
    .join(', ');

  console.log(text);
  console.log(
    `  ${ReportStatus[report.status]}, ${FixSource[report.source]},` +
      ` ${EvidenceRelation[report.relation]}`
  );
  console.log(
    `  score ${report.score.qualityScore} (before limits ${report.score.rawScore})` +
      (limits === '' ? '' : `   limits: ${limits}`)
  );
  console.log(`  reasons: ${[...report.reasons].join(', ')}`);
}

function phoneReported(
  activity: PhoneActivity,
  fromSeconds: number,
  toSeconds: number
): PhoneActivityHistory {
  const history = new PhoneActivityHistory();
  history.store({
    startedAtMs: fromSeconds * 1000,
    endedAtMs: toSeconds * 1000,
    activity,
    confidence: PhoneActivityConfidence.High,
  });
  return history;
}

if (import.meta.main) {
  const asked = new WhatTheAppAskedFor();
  const obtainedEverything = new WhatTheRequestKnows();

  show(
    'one good reading obtained right now (fixture 12.2)',
    buildReport(
      [readingNorth(0, 30, 10, 1)],
      phoneReported(PhoneActivity.StandingStill, 0, 2),
      2000,
      asked,
      obtainedEverything
    ),
    {
      status: ReportStatus.Available,
      source: FixSource.Fresh,
      relation: EvidenceRelation.SupportsInside,
      rawScore: 89,
      qualityScore: 75,
    }
  );

  const obtainedNothing = new WhatTheRequestKnows();
  obtainedNothing.firstArrivalNumberOfThisRequest = 4;

  show(
    'an old reading in a vehicle, taken from the history (fixture 12.3)',
    buildReport(
      [
        readingNorth(0, 30, 10, 1),
        readingNorth(5, 30, 10, 2),
        readingNorth(10, 30, 10, 3),
      ],
      phoneReported(PhoneActivity.InVehicle, 0, 20),
      20000,
      asked,
      obtainedNothing
    ),
    {
      status: ReportStatus.Degraded,
      source: FixSource.Cached,
      relation: EvidenceRelation.Inconclusive,
      rawScore: 72,
      qualityScore: 15,
    }
  );

  show(
    'the only reading is 90 seconds old',
    buildReport(
      [readingNorth(0, 0, 5, 1)],
      new PhoneActivityHistory(),
      90000,
      asked,
      obtainedEverything
    ),
    {
      status: ReportStatus.Unavailable,
      source: FixSource.Unavailable,
      relation: EvidenceRelation.Inconclusive,
      rawScore: 0,
      qualityScore: 0,
    }
  );

  show(
    'a fresh reading 500 meters from the address',
    buildReport(
      [readingNorth(0, 500, 10, 1)],
      new PhoneActivityHistory(),
      1000,
      asked,
      obtainedEverything
    ),
    {
      status: ReportStatus.Available,
      source: FixSource.Fresh,
      relation: EvidenceRelation.SupportsOutside,
      rawScore: 81,
      qualityScore: 75,
    }
  );

  show(
    'the GPS jumps away and comes back (fixture 12.6)',
    buildReport(
      [
        readingNorth(0, 0, 5, 1),
        readingNorth(2, 500, 5, 2),
        readingNorth(4, 0, 5, 3),
      ],
      new PhoneActivityHistory(),
      4000,
      asked,
      obtainedEverything
    ),
    {
      status: ReportStatus.Available,
      source: FixSource.Fresh,
      relation: EvidenceRelation.SupportsInside,
      rawScore: 88,
      qualityScore: 75,
    }
  );
}
