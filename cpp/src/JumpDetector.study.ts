// Study file: TypeScript mirror of JumpDetector.hpp and JumpDetector.cpp.
// Same names, same order. Not part of the library. If it ever disagrees with
// the C++, the C++ is right.
// Run: node --no-warnings --experimental-transform-types cpp/src/JumpDetector.study.ts

/*
THE BUSINESS RULE

  Sometimes the GPS makes a mistake for one reading: on the map the phone seems
  to jump 450 meters away and come back two seconds later. Nobody moves like
  that. This file gives each reading a label: LooksFine, InConflict or
  ProbablyGpsJump. It never removes or changes a reading.

THE ORDER OF THE CALLS

  checkAllReadingsForGpsJumps(history)
  │
  └─ for each reading of the history ("this reading")
       │
       ├─ for each OTHER reading                                    STEP 1
       │    ├─ readingsContradictEachOther(thisReading, otherReading)
       │    │    ├─ replaceInvalidSpeedWithDefault()     normally gives back 45
       │    │    ├─ distanceBetweenMeters()              meters between the two
       │    │    └─ calculateLongestDistancePossibleBetweenReadings()
       │    │         distanceAtFastestSpeed = 45 x seconds between them
       │    │         gpsErrorOfBothReadings = one GPS error + the other
       │    │         answer = the two added together
       │    │       they contradict when: meters between them > answer
       │    │
       │    ├─ if they contradict:  readingsAgainstIt + 1
       │    └─ else:                readingsOnItsSide + 1
       │
       ├─ if nothing is against it                                  STEP 2
       │    └─ LooksFine
       ├─ else if more against it than on its side
       │    └─ ProbablyGpsJump
       └─ else
            └─ InConflict

ONE CALL, STEP BY STEP

  Three readings, each with a GPS error of 5 meters:
    A  second 0, at the delivery address
    B  second 2, 450 meters away
    C  second 4, back at the address

    checkAllReadingsForGpsJumps([A, B, C])

      this reading = A
        A and B   450 m apart, longest possible 45 x 2 + 5 + 5 = 100   contradict
        A and C     0 m apart, longest possible 45 x 4 + 5 + 5 = 190   fine
        on its side 2 (A and C), against it 1 (B)      -> InConflict

      this reading = B
        B and A   contradict
        B and C   contradict
        on its side 1 (only B), against it 2 (A and C) -> ProbablyGpsJump

      this reading = C
        C and A   fine
        C and B   contradict
        on its side 2 (C and A), against it 1 (B)      -> InConflict

      returns the 3 labels, howManyAreProbablyGpsJumps 1, howManyAreInConflict 2

WHAT THE REST OF THE ENGINE DOES WITH THE ANSWER

  LooksFine         nothing changes.
  InConflict        the reading can still be used, but with less trust: the
                    quality score gets a ceiling.
  ProbablyGpsJump   the reading is not trusted: the quality score gets a very
                    low ceiling and the engine answers "cannot tell" instead of
                    "inside" or "outside".

  Turning the labels into a score is the job of the candidate selector, which
  is not written yet. Today only the tests call these functions.

  In the C++ the readings arrive inside a LocationHistory. Here a plain array
  plays that part: position 0 is the oldest reading and no reading is repeated.
*/

export enum Provider {
  Fused,
  CoreLocation,
  SystemCache,
}

export type Coordinates = Readonly<{
  latitudeDegrees: number;
  longitudeDegrees: number;
}>;

export type LocationReading = Readonly<{
  position: Coordinates;
  horizontalAccuracyMeters: number;
  capturedAtMs: number;
  provider: Provider;
}>;

export const FASTEST_POSSIBLE_SPEED_METERS_PER_SECOND = 45.0;

export enum JumpCheckResult {
  LooksFine,
  InConflict,
  ProbablyGpsJump,
}

export class JumpCheckOfOneReading {
  readingsOnItsSide: number = 1;
  readingsAgainstIt: number = 0;
  result: JumpCheckResult = JumpCheckResult.LooksFine;
}

export class JumpCheckOfAllReadings {
  ofEachReading: JumpCheckOfOneReading[] = [];
  howManyReadings: number = 0;

  howManyAreInConflict: number = 0;
  howManyAreProbablyGpsJumps: number = 0;

  readingsFromDifferentProvidersContradict: boolean = false;

  someReadingsContradictEachOther(): boolean {
    return this.howManyAreInConflict + this.howManyAreProbablyGpsJumps > 0;
  }
}

function calculateLongestDistancePossibleBetweenReadings(
  oneReading: LocationReading,
  otherReading: LocationReading,
  fastestPossibleSpeed: number
): number {
  let millisecondsBetweenReadings =
    otherReading.capturedAtMs - oneReading.capturedAtMs;
  if (millisecondsBetweenReadings < 0.0) {
    millisecondsBetweenReadings = -millisecondsBetweenReadings;
  }
  const secondsBetweenReadings = millisecondsBetweenReadings / 1000.0;

  const distanceAtFastestSpeed = fastestPossibleSpeed * secondsBetweenReadings;

  const gpsErrorOfBothReadings =
    oneReading.horizontalAccuracyMeters + otherReading.horizontalAccuracyMeters;

  return distanceAtFastestSpeed + gpsErrorOfBothReadings;
}

export function readingsContradictEachOther(
  oneReading: LocationReading,
  otherReading: LocationReading,
  fastestPossibleSpeedMetersPerSecond: number = FASTEST_POSSIBLE_SPEED_METERS_PER_SECOND
): boolean {
  const fastestPossibleSpeed = replaceInvalidSpeedWithDefault(
    fastestPossibleSpeedMetersPerSecond
  );

  const metersBetweenReadings = distanceBetweenMeters(
    oneReading.position,
    otherReading.position
  );

  const longestDistancePossible =
    calculateLongestDistancePossibleBetweenReadings(
      oneReading,
      otherReading,
      fastestPossibleSpeed
    );

  return metersBetweenReadings > longestDistancePossible;
}

export function checkAllReadingsForGpsJumps(
  history: readonly LocationReading[],
  fastestPossibleSpeedMetersPerSecond: number = FASTEST_POSSIBLE_SPEED_METERS_PER_SECOND
): JumpCheckOfAllReadings {
  const all = new JumpCheckOfAllReadings();
  all.howManyReadings = history.length;

  for (const [thisPosition, thisReading] of history.entries()) {
    const check = new JumpCheckOfOneReading();

    // Step 1
    for (const [otherPosition, otherReading] of history.entries()) {
      const itIsTheSameReading = otherPosition === thisPosition;
      if (itIsTheSameReading) {
        continue;
      }

      const theyContradictEachOther = readingsContradictEachOther(
        thisReading,
        otherReading,
        fastestPossibleSpeedMetersPerSecond
      );

      if (theyContradictEachOther) {
        check.readingsAgainstIt += 1;

        const theyCameFromDifferentProviders =
          thisReading.provider !== otherReading.provider;
        if (theyCameFromDifferentProviders) {
          all.readingsFromDifferentProvidersContradict = true;
        }
      } else {
        check.readingsOnItsSide += 1;
      }
    }

    // Step 2
    const nothingIsAgainstIt = check.readingsAgainstIt === 0;
    const moreAgainstItThanOnItsSide =
      check.readingsAgainstIt > check.readingsOnItsSide;

    if (nothingIsAgainstIt) {
      check.result = JumpCheckResult.LooksFine;
    } else if (moreAgainstItThanOnItsSide) {
      check.result = JumpCheckResult.ProbablyGpsJump;
      all.howManyAreProbablyGpsJumps += 1;
    } else {
      check.result = JumpCheckResult.InConflict;
      all.howManyAreInConflict += 1;
    }

    all.ofEachReading.push(check);
  }

  return all;
}

// Borrowed from UncertaintyModel.cpp and GeoMath.cpp so this file runs alone.

function replaceInvalidSpeedWithDefault(speed: number): number {
  const speedIsValid = Number.isFinite(speed) && speed > 0.0;
  if (speedIsValid) {
    return speed;
  }
  return FASTEST_POSSIBLE_SPEED_METERS_PER_SECOND;
}

export const EARTH_RADIUS_METERS = 6371008.8;
const DEGREES_TO_RADIANS = Math.PI / 180.0;

export function distanceBetweenMeters(
  from: Coordinates,
  to: Coordinates
): number {
  const fromLatitude = from.latitudeDegrees * DEGREES_TO_RADIANS;
  const toLatitude = to.latitudeDegrees * DEGREES_TO_RADIANS;
  const latitudeDifference =
    (to.latitudeDegrees - from.latitudeDegrees) * DEGREES_TO_RADIANS;

  const longitudeDifferenceDegrees = shortWayAroundThePlanet(
    to.longitudeDegrees - from.longitudeDegrees
  );
  const longitudeDifference = longitudeDifferenceDegrees * DEGREES_TO_RADIANS;

  const sineOfHalfLatitudeDifference = Math.sin(latitudeDifference / 2.0);
  const sineOfHalfLongitudeDifference = Math.sin(longitudeDifference / 2.0);

  let haversine =
    sineOfHalfLatitudeDifference * sineOfHalfLatitudeDifference +
    Math.cos(fromLatitude) *
      Math.cos(toLatitude) *
      sineOfHalfLongitudeDifference *
      sineOfHalfLongitudeDifference;
  if (haversine < 0.0) {
    haversine = 0.0;
  }
  if (haversine > 1.0) {
    haversine = 1.0;
  }

  const angleBetweenPoints =
    2.0 * Math.atan2(Math.sqrt(haversine), Math.sqrt(1.0 - haversine));

  return EARTH_RADIUS_METERS * angleBetweenPoints;
}

function shortWayAroundThePlanet(degrees: number): number {
  return degrees - 360.0 * Math.round(degrees / 360.0);
}

// Examples. The expected labels are the ones the C++ tests check.

function readingNorth(
  seconds: number,
  metersNorth: number,
  gpsErrorMeters: number = 5.0
): LocationReading {
  const degreesPerMeter = 180.0 / (Math.PI * EARTH_RADIUS_METERS);
  return {
    position: {
      latitudeDegrees: metersNorth * degreesPerMeter,
      longitudeDegrees: 0.0,
    },
    horizontalAccuracyMeters: gpsErrorMeters,
    capturedAtMs: seconds * 1000,
    provider: Provider.Fused,
  };
}

function show(
  text: string,
  history: readonly LocationReading[],
  expectedLabels: readonly JumpCheckResult[]
): void {
  const all = checkAllReadingsForGpsJumps(history);

  console.log(text);
  for (const [position, check] of all.ofEachReading.entries()) {
    if (check.result !== expectedLabels[position]) {
      throw new Error(`${text}: out of date at reading ${position}`);
    }
    console.log(
      `  reading ${position}   on its side ${check.readingsOnItsSide}` +
        `   against it ${check.readingsAgainstIt}` +
        `   ${JumpCheckResult[check.result]}`
    );
  }
}

const { LooksFine, InConflict, ProbablyGpsJump } = JumpCheckResult;

show(
  'motorbike at 15 meters per second',
  [readingNorth(0, 0), readingNorth(1, 15), readingNorth(2, 30)],
  [LooksFine, LooksFine, LooksFine]
);
show(
  'the GPS jumps away and comes back',
  [readingNorth(0, 0), readingNorth(2, 450), readingNorth(4, 0)],
  [InConflict, ProbablyGpsJump, InConflict]
);
show(
  'only two readings, and they contradict',
  [readingNorth(0, 0), readingNorth(2, 450)],
  [InConflict, InConflict]
);
show(
  'the old reading is the wrong one',
  [
    readingNorth(0, 0),
    readingNorth(2, 450),
    readingNorth(3, 455),
    readingNorth(4, 460),
  ],
  [ProbablyGpsJump, InConflict, InConflict, InConflict]
);
show(
  'same jump, but GPS errors of 300 meters',
  [readingNorth(0, 0, 300), readingNorth(2, 450, 300), readingNorth(4, 0, 300)],
  [LooksFine, LooksFine, LooksFine]
);
show(
  'three readings all far from each other',
  [readingNorth(0, 0), readingNorth(1, 2000), readingNorth(2, 4000)],
  [ProbablyGpsJump, ProbablyGpsJump, ProbablyGpsJump]
);
