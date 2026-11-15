// Study file: TypeScript mirror of UncertaintyModel.hpp and UncertaintyModel.cpp.
// Same names, same order. Not part of the library. If it ever disagrees with
// the C++, the C++ is right.
// Run: node --no-warnings --experimental-transform-types cpp/src/UncertaintyModel.study.ts

/*
THE BUSINESS RULE

  A GPS reading says where the phone WAS, not where it IS. Before trusting an
  old reading we ask: "how far could the phone have moved since then?"

THE ORDER OF THE CALLS

  calculateDistancePhoneCouldHaveMoved(reading, millisecondsSinceReading, phoneActivity)
  │
  ├─ replaceInvalidSpeedWithDefault()        normally gives back 45
  │
  ├─ secondsSinceReading     = milliseconds / 1000
  ├─ longestDistancePossible = 45 x seconds          <- limit for any answer
  │
  ├─ if we know the phone activity                              OPTION 1
  │    └─ speedForPhoneActivity()          3 for walking, 45 in a vehicle...
  │       meters = that speed x seconds
  │
  ├─ else if the GPS reported a speed and its error             OPTION 2
  │    └─ calculateDistanceUsingSpeedReportedByGps()
  │         speedToBeSafe                 = gpsSpeed + 2 x gpsSpeedError
  │         distanceAtThatSpeed           = speedToBeSafe x seconds
  │         extraDistanceIfThePhoneSpedUp = 2.5 x seconds x seconds / 2
  │         meters = the last two added together
  │
  ├─ else                                                       OPTION 3
  │    └─ meters = longestDistancePossible
  │
  └─ if meters > longestDistancePossible, meters = longestDistancePossible

  calculateHowFarPhoneCouldBeFromReading(reading, distanceMoved)   <- called next
  └─ GPS error + meters

ONE CALL, STEP BY STEP

  A reading taken 3 seconds ago with a GPS error of 10 meters. The GPS reported
  speed 0.3 with error 0.5. Nobody told us the phone activity.

    calculateDistancePhoneCouldHaveMoved(almostStopped, 3000, PhoneActivity.Unknown)
      replaceInvalidSpeedWithDefault(45)            -> 45
      secondsSinceReading                       =  3
      longestDistancePossible = 45 x 3          =  135
      do we know the phone activity?               no
      did the GPS report speed and its error?      yes  -> OPTION 2
        calculateDistanceUsingSpeedReportedByGps(0.3, 0.5, 3)
          speedToBeSafe       = 0.3 + 2 x 0.5   =  1.3
          distanceAtThatSpeed = 1.3 x 3         =  3.9
          extraDistance       = 2.5 x 3 x 3 / 2 =  11.25
                                                -> 15.15
      is 15.15 bigger than 135?                    no, keep 15.15
      returns { meters: 15.15, howItWasCalculated: FromSpeedReportedByGps }

    calculateHowFarPhoneCouldBeFromReading(almostStopped, distanceMoved)
      10 + 15.15                                -> 25.15

WHAT THE REST OF THE ENGINE DOES WITH THE ANSWER

  The phone could be up to 25.15 meters away from the reading. If the reading
  is 30 meters from the delivery address and the delivery radius is 100 meters:

      30 + 25.15 = 55.15   which is less than 100   ->  "inside"

  That last comparison is not in this file. It is classifyRelation() in
  ProximityRelation.cpp. Without the GPS speed, option 3 would give 135:
  30 + 10 + 135 = 175, more than 100, and the answer would be "cannot tell".

  Today only the tests call these functions.
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

const HOW_MANY_TIMES_WE_ADD_THE_GPS_SPEED_ERROR = 2.0;
const SPEED_THE_PHONE_CAN_GAIN_EACH_SECOND = 2.5;

function replaceInvalidSpeedWithDefault(speed: number): number {
  const speedIsValid = Number.isFinite(speed) && speed > 0.0;
  if (speedIsValid) {
    return speed;
  }
  return FASTEST_POSSIBLE_SPEED_METERS_PER_SECOND;
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

export function speedForPhoneActivity(
  phoneActivity: PhoneActivity,
  fastestPossibleSpeedMetersPerSecond: number
): number {
  const fastestPossibleSpeed = replaceInvalidSpeedWithDefault(
    fastestPossibleSpeedMetersPerSecond
  );

  switch (phoneActivity) {
    case PhoneActivity.StandingStill:
      return SPEED_STANDING_STILL;
    case PhoneActivity.Walking:
      return SPEED_WALKING;
    case PhoneActivity.Running:
      return SPEED_RUNNING;
    case PhoneActivity.Cycling:
      return SPEED_CYCLING;
    case PhoneActivity.InVehicle:
    case PhoneActivity.Unknown:
      return fastestPossibleSpeed;
  }
}

export function calculateDistancePhoneCouldHaveMoved(
  reading: LocationReading,
  millisecondsSinceReading: number,
  phoneActivity: PhoneActivity,
  fastestPossibleSpeedMetersPerSecond: number = FASTEST_POSSIBLE_SPEED_METERS_PER_SECOND
): DistancePhoneCouldHaveMoved {
  const fastestPossibleSpeed = replaceInvalidSpeedWithDefault(
    fastestPossibleSpeedMetersPerSecond
  );

  let secondsSinceReading = millisecondsSinceReading / 1000.0;
  if (secondsSinceReading < 0.0) {
    secondsSinceReading = 0.0;
  }

  const longestDistancePossible = fastestPossibleSpeed * secondsSinceReading;

  const weKnowThePhoneActivity = phoneActivity !== PhoneActivity.Unknown;

  const gpsReportedSpeedAndItsError =
    reading.speedMetersPerSecond !== undefined &&
    reading.speedAccuracyMetersPerSecond !== undefined;

  const result = new DistancePhoneCouldHaveMoved();

  if (weKnowThePhoneActivity) {
    // Option 1
    const speedOfTheActivity = speedForPhoneActivity(
      phoneActivity,
      fastestPossibleSpeed
    );

    result.meters = speedOfTheActivity * secondsSinceReading;
    result.howItWasCalculated = HowDistanceWasCalculated.FromPhoneActivity;
  } else if (gpsReportedSpeedAndItsError) {
    // Option 2
    const gpsSpeed = reading.speedMetersPerSecond;
    const gpsSpeedError = reading.speedAccuracyMetersPerSecond;

    result.meters = calculateDistanceUsingSpeedReportedByGps(
      gpsSpeed,
      gpsSpeedError,
      secondsSinceReading
    );
    result.howItWasCalculated = HowDistanceWasCalculated.FromSpeedReportedByGps;
  } else {
    // Option 3
    result.meters = longestDistancePossible;
    result.howItWasCalculated =
      HowDistanceWasCalculated.AssumingFastestPossibleSpeed;
  }

  if (result.meters > longestDistancePossible) {
    result.meters = longestDistancePossible;
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
  millisecondsSinceReading: number,
  phoneActivity: PhoneActivity,
  expectedMeters: number
): void {
  const distanceMoved = calculateDistancePhoneCouldHaveMoved(
    reading,
    millisecondsSinceReading,
    phoneActivity
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
    `${text.padEnd(25)} moved ${meters} m  ${how.padEnd(28)}  from reading ${howFarFromReading} m`
  );
}

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

show('walking, 5 s', noSpeed, 5000, PhoneActivity.Walking, 15);
show('in vehicle, 10 s', noSpeed, 10000, PhoneActivity.InVehicle, 450);
show('nothing known, 5 s', noSpeed, 5000, PhoneActivity.Unknown, 225);
show('nothing known, 3 s', noSpeed, 3000, PhoneActivity.Unknown, 135);
show(
  'GPS almost stopped, 3 s',
  almostStopped,
  3000,
  PhoneActivity.Unknown,
  15.15
);
show(
  'GPS almost stopped, 10 s',
  almostStopped,
  10000,
  PhoneActivity.Unknown,
  138
);
show('GPS says 40 m/s, 10 s', movingFast, 10000, PhoneActivity.Unknown, 450);
