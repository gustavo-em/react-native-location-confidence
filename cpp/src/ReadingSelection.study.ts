// Study file: TypeScript mirror of ReadingSelection.hpp and ReadingSelection.cpp.
// Same names, same order. Not part of the library. If it ever disagrees with
// the C++, the C++ is right.
// Run: node --no-warnings --experimental-transform-types cpp/src/ReadingSelection.study.ts
// The examples at the end only run when this file is the one being run.

/*
THE BUSINESS RULE

  The history can hold up to 10 GPS readings. This file picks the one that
  tells us the most about where the phone is NOW. Not always the newest, not
  always the most precise: the readings are compared by

      how far the phone could be = GPS error
                                   + distance the phone could have moved since

  Rules, in order:
    1st  a reading that is probably a GPS jump loses to one that is not
    2nd  the smaller "how far the phone could be" wins
    3rd  if they are equal, the newer reading wins
    4th  if still equal, the smaller GPS error wins
    5th  if still equal, the one that arrived first wins

  The reading that is chosen is always one the phone really measured.

THE ORDER OF THE CALLS

  chooseBestReading(history, activityHistory, nowMs, rules)
  │
  ├─ checkAllReadingsForGpsJumps()                    STEP 1  (JumpDetector)
  │
  └─ for each reading of the history
       ├─ too old, or not allowed?  ->  skip it
       │
       ├─ examineReading()                            STEP 2
       │    ├─ calculateTimeInEachPhoneActivity()       (PhoneActivityTimeline)
       │    ├─ gpsSpeedMatchesTheMovement()             compares with the reading before it
       │    ├─ calculateDistancePhoneCouldHaveMoved()   (UncertaintyModel)
       │    └─ calculateHowFarPhoneCouldBeFromReading()
       │
       └─ firstIsBetterThanSecond()                   STEP 3

ONE CALL, STEP BY STEP

  now is second 50; nothing is known about the phone activity
  reading A: taken at second 0,  GPS error 5 meters
  reading B: taken at second 49, GPS error 200 meters

    chooseBestReading()
      checkAllReadingsForGpsJumps()        -> both look fine
      examineReading(A)                    -> 5 + 45 x 50 = 2255 meters
      examineReading(B)                    -> 200 + 45 x 1 = 245 meters
      firstIsBetterThanSecond(B, A)        -> true   (245 is less than 2255)
      returns B

  If the phone had reported "standing still" from second 0 to second 50:
      examineReading(A)                    -> 5 + 0.5 x 50 = 30 meters
      examineReading(B)                    -> 200 + 0.5 x 1 = 200.5 meters
      returns A

WHAT THE REST OF THE ENGINE DOES WITH THE ANSWER

  buildAssessment() in Assessment.cpp takes the chosen reading and writes the
  final report: inside the delivery radius or not, the quality score, the
  reasons. The engine also asks for the best reading before it stores a new
  one in a full history, to know which reading must not lose its place.
*/

import {
  checkAllReadingsForGpsJumps,
  distanceBetweenMeters,
  JumpCheckOfOneReading,
  JumpCheckResult,
  Provider,
  type Coordinates,
  type JumpCheckOfAllReadings,
} from './JumpDetector.study.ts';
import {
  calculateTimeInEachPhoneActivity,
  PhoneActivityConfidence,
  PhoneActivityHistory,
} from './PhoneActivityTimeline.study.ts';
import {
  calculateDistancePhoneCouldHaveMoved,
  calculateHowFarPhoneCouldBeFromReading,
  DistancePhoneCouldHaveMoved,
  FASTEST_POSSIBLE_SPEED_METERS_PER_SECOND,
  HOW_MANY_TIMES_WE_ADD_THE_GPS_SPEED_ERROR,
  PhoneActivity,
  SPEED_THE_PHONE_CAN_GAIN_EACH_SECOND,
  TimeInEachPhoneActivity,
  wholeTimeInOneActivity,
} from './UncertaintyModel.study.ts';

// From Types.hpp. In C++ the history is a class (LocationHistory); here it is
// an array of readings, oldest first.

export enum MockSignal {
  Unknown,
  NotFlaggedByOs,
  FlaggedByOs,
}

export type LocationReading = Readonly<{
  position: Coordinates;
  horizontalAccuracyMeters: number;
  capturedAtMs: number;
  captureTimeWasEstimated: boolean;
  onlyApproximatePermission: boolean;
  speedMetersPerSecond?: number;
  speedAccuracyMetersPerSecond?: number;
  provider: Provider;
  mockSignal: MockSignal;
  arrivalNumber: number;
}>;

export function ageInMs(reading: LocationReading, nowMs: number): number {
  const age = nowMs - reading.capturedAtMs;
  return age > 0 ? age : 0;
}

// ReadingSelection.hpp

export class RulesForChoosingReading {
  oldestAgeAllowedMs: number = 60000;
  fastestPossibleSpeedMetersPerSecond: number =
    FASTEST_POSSIBLE_SPEED_METERS_PER_SECOND;
  usePhoneActivity: boolean = true;
  firstArrivalNumberAllowed: number = 0;
}

export class WhatWeKnowAboutReading {
  reading: LocationReading;
  ageMs: number = 0;
  timeInEachActivity: TimeInEachPhoneActivity = new TimeInEachPhoneActivity();
  distanceMoved: DistancePhoneCouldHaveMoved =
    new DistancePhoneCouldHaveMoved();
  howFarPhoneCouldBeMeters: number = 0.0;
  jumpCheck: JumpCheckOfOneReading = new JumpCheckOfOneReading();
  gpsSpeedDoesNotMatchTheMovement: boolean = false;

  constructor(reading: LocationReading) {
    this.reading = reading;
  }
}

export class ChosenReading {
  found: boolean = false;
  best: WhatWeKnowAboutReading | undefined = undefined;

  howManyReadingsWereExamined: number = 0;
  howManyWereTooOld: number = 0;
  howManyAreProbablyGpsJumps: number = 0;
  someReadingsContradictEachOther: boolean = false;
  readingsFromDifferentProvidersContradict: boolean = false;
}

// ReadingSelection.cpp

function gpsSpeedMatchesTheMovement(
  previousReading: LocationReading,
  reading: LocationReading
): boolean {
  const gpsReportedASpeed = reading.speedMetersPerSecond !== undefined;

  const secondsBetween =
    (reading.capturedAtMs - previousReading.capturedAtMs) / 1000.0;
  const readingsHaveTimeBetweenThem = secondsBetween > 0.0;

  let speedMatches = true;

  if (gpsReportedASpeed && readingsHaveTimeBetweenThem) {
    const gpsSpeed = reading.speedMetersPerSecond ?? 0.0;
    const gpsSpeedError = reading.speedAccuracyMetersPerSecond ?? 0.0;
    const errorOnEachSide =
      HOW_MANY_TIMES_WE_ADD_THE_GPS_SPEED_ERROR * gpsSpeedError;
    const fastestReportedSpeed = gpsSpeed + errorOnEachSide;
    let slowestReportedSpeed = gpsSpeed - errorOnEachSide;
    if (slowestReportedSpeed < 0.0) {
      slowestReportedSpeed = 0.0;
    }

    const metersBetween = distanceBetweenMeters(
      previousReading.position,
      reading.position
    );
    const gpsErrorOfBothReadings =
      previousReading.horizontalAccuracyMeters +
      reading.horizontalAccuracyMeters;
    const longestDistance = metersBetween + gpsErrorOfBothReadings;
    let shortestDistance = metersBetween - gpsErrorOfBothReadings;
    if (shortestDistance < 0.0) {
      shortestDistance = 0.0;
    }
    const fastestSpeedThePositionsShow = longestDistance / secondsBetween;
    const slowestSpeedThePositionsShow = shortestDistance / secondsBetween;

    const howMuchTheSpeedCanChange =
      (SPEED_THE_PHONE_CAN_GAIN_EACH_SECOND * secondsBetween) / 2.0;

    const reportedSpeedIsTooFast =
      slowestReportedSpeed - howMuchTheSpeedCanChange >
      fastestSpeedThePositionsShow;
    const reportedSpeedIsTooSlow =
      fastestReportedSpeed + howMuchTheSpeedCanChange <
      slowestSpeedThePositionsShow;

    speedMatches = !reportedSpeedIsTooFast && !reportedSpeedIsTooSlow;
  }

  return speedMatches;
}

function examineReading(
  history: readonly LocationReading[],
  position: number,
  jumpCheckOfAllReadings: JumpCheckOfAllReadings,
  activityHistory: PhoneActivityHistory,
  nowMs: number,
  rules: RulesForChoosingReading
): WhatWeKnowAboutReading {
  const reading = history[position];
  const jumpCheck = jumpCheckOfAllReadings.ofEachReading[position];
  if (reading === undefined || jumpCheck === undefined) {
    throw new Error(`there is no reading at position ${position}`);
  }

  const result = new WhatWeKnowAboutReading(reading);
  result.ageMs = ageInMs(reading, nowMs);
  result.jumpCheck = jumpCheck;

  if (rules.usePhoneActivity) {
    result.timeInEachActivity = calculateTimeInEachPhoneActivity(
      activityHistory,
      reading.capturedAtMs,
      nowMs
    );
  } else {
    result.timeInEachActivity = wholeTimeInOneActivity(
      PhoneActivity.Unknown,
      result.ageMs
    );
  }

  const previousReading = history[position - 1];
  const thereIsAReadingBeforeThisOne = previousReading !== undefined;
  if (thereIsAReadingBeforeThisOne) {
    result.gpsSpeedDoesNotMatchTheMovement = !gpsSpeedMatchesTheMovement(
      previousReading,
      reading
    );
  }

  let readingUsedForTheDistance: LocationReading = reading;
  if (result.gpsSpeedDoesNotMatchTheMovement) {
    readingUsedForTheDistance = {
      ...reading,
      speedMetersPerSecond: undefined,
      speedAccuracyMetersPerSecond: undefined,
    };
  }

  result.distanceMoved = calculateDistancePhoneCouldHaveMoved(
    readingUsedForTheDistance,
    result.timeInEachActivity,
    rules.fastestPossibleSpeedMetersPerSecond
  );

  result.howFarPhoneCouldBeMeters = calculateHowFarPhoneCouldBeFromReading(
    reading,
    result.distanceMoved
  );

  return result;
}

function firstIsBetterThanSecond(
  first: WhatWeKnowAboutReading,
  second: WhatWeKnowAboutReading
): boolean {
  const firstIsProbablyAJump =
    first.jumpCheck.result === JumpCheckResult.ProbablyGpsJump;
  const secondIsProbablyAJump =
    second.jumpCheck.result === JumpCheckResult.ProbablyGpsJump;

  let firstIsBetter = false;

  if (firstIsProbablyAJump !== secondIsProbablyAJump) {
    // 1st rule
    firstIsBetter = secondIsProbablyAJump;
  } else if (
    first.howFarPhoneCouldBeMeters !== second.howFarPhoneCouldBeMeters
  ) {
    // 2nd rule
    firstIsBetter =
      first.howFarPhoneCouldBeMeters < second.howFarPhoneCouldBeMeters;
  } else if (first.ageMs !== second.ageMs) {
    // 3rd rule
    firstIsBetter = first.ageMs < second.ageMs;
  } else if (
    first.reading.horizontalAccuracyMeters !==
    second.reading.horizontalAccuracyMeters
  ) {
    // 4th rule
    firstIsBetter =
      first.reading.horizontalAccuracyMeters <
      second.reading.horizontalAccuracyMeters;
  } else {
    // 5th rule
    firstIsBetter = first.reading.arrivalNumber < second.reading.arrivalNumber;
  }

  return firstIsBetter;
}

export function chooseBestReading(
  history: readonly LocationReading[],
  activityHistory: PhoneActivityHistory,
  nowMs: number,
  rules: RulesForChoosingReading
): ChosenReading {
  const chosen = new ChosenReading();

  // Step 1
  const jumpCheckOfAllReadings = checkAllReadingsForGpsJumps(
    history,
    rules.fastestPossibleSpeedMetersPerSecond
  );

  chosen.howManyAreProbablyGpsJumps =
    jumpCheckOfAllReadings.howManyAreProbablyGpsJumps;
  chosen.someReadingsContradictEachOther =
    jumpCheckOfAllReadings.someReadingsContradictEachOther();
  chosen.readingsFromDifferentProvidersContradict =
    jumpCheckOfAllReadings.readingsFromDifferentProvidersContradict;

  for (const [position, reading] of history.entries()) {
    chosen.howManyReadingsWereExamined += 1;

    const isTooOld = ageInMs(reading, nowMs) > rules.oldestAgeAllowedMs;
    if (isTooOld) {
      chosen.howManyWereTooOld += 1;
      continue;
    }

    const isNotAllowed =
      reading.arrivalNumber < rules.firstArrivalNumberAllowed;
    if (isNotAllowed) {
      continue;
    }

    // Step 2
    const candidate = examineReading(
      history,
      position,
      jumpCheckOfAllReadings,
      activityHistory,
      nowMs,
      rules
    );

    // Step 3
    const bestSoFar = chosen.best;
    const isTheFirstOne = bestSoFar === undefined;
    if (isTheFirstOne || firstIsBetterThanSecond(candidate, bestSoFar)) {
      chosen.best = candidate;
      chosen.found = true;
    }
  }

  return chosen;
}

// Examples. The expected values are the ones the C++ tests check.

const METERS_PER_DEGREE_NORTH = (Math.PI * 6371008.8) / 180.0;

export function readingNorth(
  seconds: number,
  metersNorth: number,
  gpsErrorMeters: number,
  arrivalNumber: number
): LocationReading {
  return {
    position: {
      latitudeDegrees: metersNorth / METERS_PER_DEGREE_NORTH,
      longitudeDegrees: 0.0,
    },
    horizontalAccuracyMeters: gpsErrorMeters,
    capturedAtMs: seconds * 1000,
    captureTimeWasEstimated: false,
    onlyApproximatePermission: false,
    provider: Provider.Fused,
    mockSignal: MockSignal.Unknown,
    arrivalNumber,
  };
}

function show(
  text: string,
  history: readonly LocationReading[],
  activityHistory: PhoneActivityHistory,
  secondsNow: number,
  expectedCapturedAtSeconds: number | undefined,
  expectedHowFarMeters: number
): void {
  const chosen = chooseBestReading(
    history,
    activityHistory,
    secondsNow * 1000,
    new RulesForChoosingReading()
  );

  const upToDate =
    chosen.best === undefined
      ? expectedCapturedAtSeconds === undefined
      : expectedCapturedAtSeconds !== undefined &&
        chosen.best.reading.capturedAtMs === expectedCapturedAtSeconds * 1000 &&
        Math.abs(chosen.best.howFarPhoneCouldBeMeters - expectedHowFarMeters) <
          1e-6;
  if (!upToDate) {
    throw new Error(
      `${text}: out of date, the C++ tests expect another answer`
    );
  }

  console.log(text);
  if (chosen.best === undefined) {
    console.log('  no reading can be used');
  } else {
    console.log(
      `  the reading of second ${chosen.best.reading.capturedAtMs / 1000}` +
        `   the phone could be ${chosen.best.howFarPhoneCouldBeMeters.toFixed(2)} m from it` +
        `   ${JumpCheckResult[chosen.best.jumpCheck.result]}`
    );
  }
}

if (import.meta.main) {
  const nothingReported = new PhoneActivityHistory();

  const standingStill = new PhoneActivityHistory();
  standingStill.store({
    startedAtMs: 0,
    endedAtMs: 50000,
    activity: PhoneActivity.StandingStill,
    confidence: PhoneActivityConfidence.High,
  });

  const oldPreciseAndNewImprecise = [
    readingNorth(0, 0, 5, 1),
    readingNorth(49, 0, 200, 2),
  ];

  show(
    'old precise against new imprecise, nothing known',
    oldPreciseAndNewImprecise,
    nothingReported,
    50,
    49,
    245
  );
  show(
    'the same readings, the phone stood still',
    oldPreciseAndNewImprecise,
    standingStill,
    50,
    0,
    30
  );
  show(
    'a GPS jump in the middle is never the best reading',
    [
      readingNorth(0, 0, 5, 1),
      readingNorth(2, 450, 5, 2),
      readingNorth(3, 0, 60, 3),
    ],
    nothingReported,
    5,
    3,
    150
  );
  show(
    'one bad reading at the end does not replace a good series',
    [
      readingNorth(0, 0, 5, 1),
      readingNorth(1, 0, 5, 2),
      readingNorth(2, 0, 5, 3),
      readingNorth(3, 1000, 5, 4),
    ],
    nothingReported,
    3,
    2,
    50
  );
  show(
    'every reading is too old',
    [readingNorth(10, 0, 5, 1), readingNorth(30, 0, 5, 2)],
    nothingReported,
    100,
    undefined,
    0
  );

  const almostStoppedButMovedFar: LocationReading = {
    ...readingNorth(2, 100, 5, 2),
    speedMetersPerSecond: 0.3,
    speedAccuracyMetersPerSecond: 0.5,
  };
  show(
    'a reported speed that does not fit the movement is ignored',
    [readingNorth(0, 0, 5, 1), almostStoppedButMovedFar],
    nothingReported,
    5,
    2,
    140
  );
}
