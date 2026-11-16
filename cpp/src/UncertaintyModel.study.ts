// Study file: TypeScript mirror of UncertaintyModel.hpp and UncertaintyModel.cpp.
// Same names, same order. Not part of the library. If it ever disagrees with
// the C++, the C++ is right.
// Run: node --no-warnings --experimental-transform-types cpp/src/UncertaintyModel.study.ts
// The examples at the end only run when this file is the one being run. Other
// study files import from this one, the way a C++ file includes a header.

/*
THE BUSINESS RULE

  A GPS reading says where the phone WAS, not where it IS. Before trusting an
  old reading we ask: "how far could the phone have moved since then?"

  There are three ways to answer. Each one we can calculate gives a distance,
  and the smallest distance wins. Knowing more never makes the answer worse.

THE ORDER OF THE CALLS

  calculateDistancePhoneCouldHaveMoved(reading, timeInEachActivity)
  │
  ├─ replaceInvalidSpeedWithDefault()        normally gives back 45
  │
  ├─ secondsSinceReading     = all the lines of the table / 1000
  ├─ longestDistancePossible = 45 x seconds
  │
  ├─ OPTION 3, always                                    we start with it
  │    └─ meters = longestDistancePossible
  │
  ├─ OPTION 2, if the GPS reported a speed and its error
  │    └─ calculateDistanceUsingSpeedReportedByGps()
  │         speedToBeSafe                 = gpsSpeed + 2 x gpsSpeedError
  │         distanceAtThatSpeed           = speedToBeSafe x seconds
  │         extraDistanceIfThePhoneSpedUp = 2.5 x seconds x seconds / 2
  │       meters = the last two added together
  │                (cut down to longestDistancePossible when it is more)
  │
  └─ OPTION 1, if we know some of the phone activity
       └─ calculateDistanceUsingPhoneActivity()
            for each activity: speedForPhoneActivity() x seconds spent in it
          if that is not longer than meters, meters = that   <- the smallest wins

  calculateHowFarPhoneCouldBeFromReading(reading, distanceMoved)   <- called next
  └─ GPS error + meters

ONE CALL, STEP BY STEP

  A reading with a GPS error of 10 meters. The GPS reported speed 0.3 with
  error 0.5. The phone says it was running during the 3 seconds since then.

    timeInEachActivity = wholeTimeInOneActivity(PhoneActivity.Running, 3000)

    calculateDistancePhoneCouldHaveMoved(almostStopped, timeInEachActivity)
      replaceInvalidSpeedWithDefault(45)            -> 45
      secondsSinceReading     = 3000 / 1000     =  3
      longestDistancePossible = 45 x 3          =  135
      OPTION 3                                     meters = 135
      OPTION 2
        calculateDistanceUsingSpeedReportedByGps(0.3, 0.5, 3)
          speedToBeSafe       = 0.3 + 2 x 0.5   =  1.3
          distanceAtThatSpeed = 1.3 x 3         =  3.9
          extraDistance       = 2.5 x 3 x 3 / 2 =  11.25
                                                -> 15.15
        is 15.15 more than 135?                    no, meters = 15.15
      OPTION 1
        calculateDistanceUsingPhoneActivity()
          running: 8 x 3                        =  24
        is 24 longer than 15.15?                   yes, meters stays 15.15
      returns { meters: 15.15, howItWasCalculated: FromSpeedReportedByGps }

    calculateHowFarPhoneCouldBeFromReading(almostStopped, distanceMoved)
      10 + 15.15                                -> 25.15

WHAT THE REST OF THE ENGINE DOES WITH THE ANSWER

  The phone could be up to 25.15 meters away from the reading. If the reading
  is 30 meters from the delivery address and the delivery radius is 100 meters:

      30 + 25.15 = 55.15   which is less than 100   ->  "inside"

  That last comparison is not in this file. It is classifyRelation() in
  ProximityRelation.cpp.

  The table timeInEachActivity is filled by PhoneActivityTimeline.cpp from what
  the phone reported. Today only the tests call these functions.
*/

export enum PhoneActivity {
  StandingStill,
  Walking,
  Running,
  Cycling,
  InVehicle,
  Unknown,
}

export const FASTEST_POSSIBLE_SPEED_METERS_PER_SECOND = 45.0;

export class TimeInEachPhoneActivity {
  millisecondsStandingStill: number = 0;
  millisecondsWalking: number = 0;
  millisecondsRunning: number = 0;
  millisecondsCycling: number = 0;
  millisecondsInVehicle: number = 0;

  millisecondsUnknown: number = 0;

  twoReportsDisagreed: boolean = false;

  add(phoneActivity: PhoneActivity, milliseconds: number): void {
    const timeIsNegative = milliseconds < 0;
    if (timeIsNegative) {
      return;
    }

    switch (phoneActivity) {
      case PhoneActivity.StandingStill:
        this.millisecondsStandingStill += milliseconds;
        break;
      case PhoneActivity.Walking:
        this.millisecondsWalking += milliseconds;
        break;
      case PhoneActivity.Running:
        this.millisecondsRunning += milliseconds;
        break;
      case PhoneActivity.Cycling:
        this.millisecondsCycling += milliseconds;
        break;
      case PhoneActivity.InVehicle:
        this.millisecondsInVehicle += milliseconds;
        break;
      case PhoneActivity.Unknown:
        this.millisecondsUnknown += milliseconds;
        break;
    }
  }

  totalMilliseconds(): number {
    return (
      this.millisecondsStandingStill +
      this.millisecondsWalking +
      this.millisecondsRunning +
      this.millisecondsCycling +
      this.millisecondsInVehicle +
      this.millisecondsUnknown
    );
  }

  weKnowNothing(): boolean {
    return this.millisecondsUnknown === this.totalMilliseconds();
  }

  standingStillTheWholeTime(): boolean {
    return (
      this.millisecondsStandingStill > 0 &&
      this.millisecondsStandingStill === this.totalMilliseconds()
    );
  }
}

export enum HowDistanceWasCalculated {
  FromPhoneActivity,
  FromSpeedReportedByGps,
  AssumingFastestPossibleSpeed,
}

export type LocationReading = Readonly<{
  horizontalAccuracyMeters: number;
  speedMetersPerSecond?: number;
  speedAccuracyMetersPerSecond?: number;
}>;

export class DistancePhoneCouldHaveMoved {
  meters: number = 0.0;

  howItWasCalculated: HowDistanceWasCalculated =
    HowDistanceWasCalculated.AssumingFastestPossibleSpeed;

  weOnlyAssumedTheWorst(): boolean {
    return (
      this.howItWasCalculated ===
      HowDistanceWasCalculated.AssumingFastestPossibleSpeed
    );
  }
}

const SPEED_STANDING_STILL = 0.5;
const SPEED_WALKING = 3.0;
const SPEED_RUNNING = 8.0;
const SPEED_CYCLING = 12.0;

export const HOW_MANY_TIMES_WE_ADD_THE_GPS_SPEED_ERROR = 2.0;
export const SPEED_THE_PHONE_CAN_GAIN_EACH_SECOND = 2.5;

function millisecondsToSeconds(milliseconds: number): number {
  return milliseconds / 1000.0;
}

function calculateDistanceUsingPhoneActivity(
  timeInEachActivity: TimeInEachPhoneActivity,
  fastestPossibleSpeed: number
): number {
  const metersStandingStill =
    speedForPhoneActivity(PhoneActivity.StandingStill, fastestPossibleSpeed) *
    millisecondsToSeconds(timeInEachActivity.millisecondsStandingStill);

  const metersWalking =
    speedForPhoneActivity(PhoneActivity.Walking, fastestPossibleSpeed) *
    millisecondsToSeconds(timeInEachActivity.millisecondsWalking);

  const metersRunning =
    speedForPhoneActivity(PhoneActivity.Running, fastestPossibleSpeed) *
    millisecondsToSeconds(timeInEachActivity.millisecondsRunning);

  const metersCycling =
    speedForPhoneActivity(PhoneActivity.Cycling, fastestPossibleSpeed) *
    millisecondsToSeconds(timeInEachActivity.millisecondsCycling);

  const metersInVehicle =
    speedForPhoneActivity(PhoneActivity.InVehicle, fastestPossibleSpeed) *
    millisecondsToSeconds(timeInEachActivity.millisecondsInVehicle);

  const metersUnknown =
    speedForPhoneActivity(PhoneActivity.Unknown, fastestPossibleSpeed) *
    millisecondsToSeconds(timeInEachActivity.millisecondsUnknown);

  return (
    metersStandingStill +
    metersWalking +
    metersRunning +
    metersCycling +
    metersInVehicle +
    metersUnknown
  );
}

function calculateDistanceUsingSpeedReportedByGps(
  gpsSpeed: number,
  gpsSpeedError: number,
  secondsSinceReading: number
): number {
  const speedToBeSafe =
    gpsSpeed + HOW_MANY_TIMES_WE_ADD_THE_GPS_SPEED_ERROR * gpsSpeedError;

  const distanceAtThatSpeed = speedToBeSafe * secondsSinceReading;

  const extraDistanceIfThePhoneSpedUp =
    (SPEED_THE_PHONE_CAN_GAIN_EACH_SECOND *
      secondsSinceReading *
      secondsSinceReading) /
    2.0;

  return distanceAtThatSpeed + extraDistanceIfThePhoneSpedUp;
}

export function replaceInvalidSpeedWithDefault(speed: number): number {
  const speedIsValid = Number.isFinite(speed) && speed > 0.0;
  if (speedIsValid) {
    return speed;
  }
  return FASTEST_POSSIBLE_SPEED_METERS_PER_SECOND;
}

export function wholeTimeInOneActivity(
  phoneActivity: PhoneActivity,
  milliseconds: number
): TimeInEachPhoneActivity {
  const timeInEachActivity = new TimeInEachPhoneActivity();
  timeInEachActivity.add(phoneActivity, milliseconds);
  return timeInEachActivity;
}

export function speedForPhoneActivity(
  phoneActivity: PhoneActivity,
  fastestPossibleSpeedMetersPerSecond: number
): number {
  const fastestPossibleSpeed = replaceInvalidSpeedWithDefault(
    fastestPossibleSpeedMetersPerSecond
  );

  let speed = fastestPossibleSpeed;

  switch (phoneActivity) {
    case PhoneActivity.StandingStill:
      speed = SPEED_STANDING_STILL;
      break;
    case PhoneActivity.Walking:
      speed = SPEED_WALKING;
      break;
    case PhoneActivity.Running:
      speed = SPEED_RUNNING;
      break;
    case PhoneActivity.Cycling:
      speed = SPEED_CYCLING;
      break;
    case PhoneActivity.InVehicle:
    case PhoneActivity.Unknown:
      break;
  }

  if (speed > fastestPossibleSpeed) {
    speed = fastestPossibleSpeed;
  }

  return speed;
}

export function calculateDistancePhoneCouldHaveMoved(
  reading: LocationReading,
  timeInEachActivity: TimeInEachPhoneActivity,
  fastestPossibleSpeedMetersPerSecond: number = FASTEST_POSSIBLE_SPEED_METERS_PER_SECOND
): DistancePhoneCouldHaveMoved {
  const fastestPossibleSpeed = replaceInvalidSpeedWithDefault(
    fastestPossibleSpeedMetersPerSecond
  );

  const secondsSinceReading = millisecondsToSeconds(
    timeInEachActivity.totalMilliseconds()
  );

  const longestDistancePossible = fastestPossibleSpeed * secondsSinceReading;

  const weKnowSomeOfThePhoneActivity = !timeInEachActivity.weKnowNothing();

  const gpsReportedSpeedAndItsError =
    reading.speedMetersPerSecond !== undefined &&
    reading.speedAccuracyMetersPerSecond !== undefined;

  const result = new DistancePhoneCouldHaveMoved();

  // Option 3
  result.meters = longestDistancePossible;
  result.howItWasCalculated =
    HowDistanceWasCalculated.AssumingFastestPossibleSpeed;

  // Option 2
  if (gpsReportedSpeedAndItsError) {
    const gpsSpeed = reading.speedMetersPerSecond;
    const gpsSpeedError = reading.speedAccuracyMetersPerSecond;

    let distanceUsingGpsSpeed = calculateDistanceUsingSpeedReportedByGps(
      gpsSpeed,
      gpsSpeedError,
      secondsSinceReading
    );

    if (distanceUsingGpsSpeed > longestDistancePossible) {
      distanceUsingGpsSpeed = longestDistancePossible;
    }

    result.meters = distanceUsingGpsSpeed;
    result.howItWasCalculated = HowDistanceWasCalculated.FromSpeedReportedByGps;
  }

  // Option 1
  if (weKnowSomeOfThePhoneActivity) {
    let distanceUsingPhoneActivity = calculateDistanceUsingPhoneActivity(
      timeInEachActivity,
      fastestPossibleSpeed
    );

    if (distanceUsingPhoneActivity > longestDistancePossible) {
      distanceUsingPhoneActivity = longestDistancePossible;
    }

    const optionOneIsNotLonger = distanceUsingPhoneActivity <= result.meters;
    if (optionOneIsNotLonger) {
      result.meters = distanceUsingPhoneActivity;
      result.howItWasCalculated = HowDistanceWasCalculated.FromPhoneActivity;
    }
  }

  return result;
}

export function calculateHowFarPhoneCouldBeFromReading(
  reading: LocationReading,
  distanceMoved: DistancePhoneCouldHaveMoved
): number {
  const gpsErrorMeters = reading.horizontalAccuracyMeters;
  return gpsErrorMeters + distanceMoved.meters;
}

// Examples. The expected values are the ones the C++ tests check.

function show(
  text: string,
  reading: LocationReading,
  timeInEachActivity: TimeInEachPhoneActivity,
  expectedMeters: number
): void {
  const distanceMoved = calculateDistancePhoneCouldHaveMoved(
    reading,
    timeInEachActivity
  );
  const howFarFromReading = calculateHowFarPhoneCouldBeFromReading(
    reading,
    distanceMoved
  );

  if (Math.abs(distanceMoved.meters - expectedMeters) > 1e-9) {
    throw new Error(`${text}: out of date, C++ expects ${expectedMeters} m`);
  }

  const meters = distanceMoved.meters.toFixed(2).padStart(7);
  const how = HowDistanceWasCalculated[distanceMoved.howItWasCalculated];
  console.log(
    `${text.padEnd(36)} moved ${meters} m  ${how.padEnd(28)}  from reading ${howFarFromReading} m`
  );
}

if (import.meta.main) {
  const noSpeed: LocationReading = { horizontalAccuracyMeters: 10.0 };

  const almostStopped: LocationReading = {
    horizontalAccuracyMeters: 10.0,
    speedMetersPerSecond: 0.3,
    speedAccuracyMetersPerSecond: 0.5,
  };

  const movingFast: LocationReading = {
    horizontalAccuracyMeters: 10.0,
    speedMetersPerSecond: 40.0,
    speedAccuracyMetersPerSecond: 5.0,
  };

  const { StandingStill, Walking, Running, InVehicle, Unknown } = PhoneActivity;

  const stillThenWalking = new TimeInEachPhoneActivity();
  stillThenWalking.add(StandingStill, 20000);
  stillThenWalking.add(Walking, 10000);

  const stillThenNothing = new TimeInEachPhoneActivity();
  stillThenNothing.add(StandingStill, 2000);
  stillThenNothing.add(Unknown, 8000);

  show('walking, 5 s', noSpeed, wholeTimeInOneActivity(Walking, 5000), 15);
  show(
    'in vehicle, 10 s',
    noSpeed,
    wholeTimeInOneActivity(InVehicle, 10000),
    450
  );
  show(
    'nothing known, 5 s',
    noSpeed,
    wholeTimeInOneActivity(Unknown, 5000),
    225
  );
  show(
    'nothing known, 3 s',
    noSpeed,
    wholeTimeInOneActivity(Unknown, 3000),
    135
  );
  show(
    'GPS almost stopped, 3 s',
    almostStopped,
    wholeTimeInOneActivity(Unknown, 3000),
    15.15
  );
  show(
    'GPS almost stopped, 10 s',
    almostStopped,
    wholeTimeInOneActivity(Unknown, 10000),
    138
  );
  show(
    'GPS says 40 m/s, 10 s',
    movingFast,
    wholeTimeInOneActivity(Unknown, 10000),
    450
  );
  show('20 s still, then 10 s walking', noSpeed, stillThenWalking, 40);
  show('2 s still, 8 s with no report', noSpeed, stillThenNothing, 361);
  show(
    'running 3 s, GPS almost stopped',
    almostStopped,
    wholeTimeInOneActivity(Running, 3000),
    15.15
  );
}
