import type { Point } from './world';

/*
 * The situations the simulator can play.
 *
 * A scenario is a script: for each second it says where the courier really
 * is, what the pretend GPS reports (it may report nothing, or something
 * wrong), and what the phone says about its own movement.
 *
 * The scripts know nothing about scores. What the screen shows about the
 * evidence always comes from the library.
 *
 * They know nothing about languages either: a moment only names the
 * sentence to show (a CaptionId). The words are in i18n/texts.ts.
 */

export type ScenarioId =
  | 'arriving'
  | 'gps-jump'
  | 'signal-lost'
  | 'lost-standing-still'
  | 'fake-gps'
  | 'approximate';

/** The sentences a scenario can show under the map, by name. */
export type CaptionId =
  | 'ridingToTheAddress'
  | 'atTheEdgeOfTheRadius'
  | 'insideAndStillMoving'
  | 'parkedAtTheAddress'
  | 'waitingAtTheAddress'
  | 'oneReadingFarAway'
  | 'oddReadingStillRemembered'
  | 'readingsAgreeAgain'
  | 'gpsWentSilent'
  | 'silenceGetsLonger'
  | 'waitingAndStandingStill'
  | 'silentButStandingStill'
  | 'fakeGpsAtTheDoor'
  | 'unmarkedProvesNothing'
  | 'blurredOnPurpose';

export type PhoneActivityToPretend =
  'stationary' | 'walking' | 'running' | 'cycling' | 'vehicle' | null;

export type ReadingOnTheMap = Readonly<{
  at: Point;
  accuracyMeters: number;
  speedMps?: number;
  speedAccuracyMps?: number;
  flaggedAsMock?: boolean;
  approximate?: boolean;
  /** How the map paints it: a wrong or fake reading is painted in red. */
  look: 'usual' | 'wrong';
}>;

export type Moment = Readonly<{
  /** Where the courier really is. Only the simulator knows this. */
  courier: Point;
  /** What the GPS reports during this second. Empty: the GPS is silent. */
  readings: readonly ReadingOnTheMap[];
  /** What the phone starts reporting about its movement. Left out: no change. */
  phoneActivity?: PhoneActivityToPretend;
  /** Which sentence tells the viewer what is going on. */
  caption: CaptionId;
}>;

export type Scenario = Readonly<{
  /** Its title on the screen is in i18n/texts.ts, under this id. */
  id: ScenarioId;
  radiusMeters: number;
  /** How many meters the map shows from its left edge to its right edge. */
  metersAcrossTheMap: number;
  lengthSeconds: number;
  momentAt: (second: number) => Moment;
}>;

const RADIUS_METERS = 80;

/** On the street in front of the building, 30 m from the address. */
const PARKING_SPOT: Point = { x: 22, y: -20 };

/** A number between -1 and 1 that is always the same for the same inputs. */
function wobble(second: number, which: number): number {
  const mixed = Math.sin(second * 12.9898 + which * 78.233) * 43758.5453;
  return (mixed - Math.floor(mixed)) * 2 - 1;
}

/**
 * What a healthy GPS reports for a phone that is at `truth`: a point a few
 * meters off (never exactly right), an error of 8 m, and the speed.
 */
function usualReading(
  truth: Point,
  second: number,
  speedMps: number
): ReadingOnTheMap {
  return {
    at: {
      x: truth.x + 4 * wobble(second, 1),
      y: truth.y + 4 * wobble(second, 2),
    },
    accuracyMeters: 8,
    speedMps,
    speedAccuracyMps: 0.3,
    look: 'usual',
  };
}

// --- Arriving -------------------------------------------------------------

// Up one street, then right into the street of the address.
const WAY_TO_THE_ADDRESS = [
  { from: { x: -100, y: -95 }, to: { x: -100, y: -20 }, speedMps: 13 },
  { from: { x: -100, y: -20 }, to: PARKING_SPOT, speedMps: 9 },
];

/** Where the courier is, and how fast, `second` seconds after leaving. */
function courierOnTheWay(second: number): { at: Point; speedMps: number } {
  let secondsLeft = second;
  for (const stretch of WAY_TO_THE_ADDRESS) {
    const lengthMeters = Math.hypot(
      stretch.to.x - stretch.from.x,
      stretch.to.y - stretch.from.y
    );
    const secondsOnThisStretch = lengthMeters / stretch.speedMps;
    if (secondsLeft < secondsOnThisStretch) {
      const partDone = secondsLeft / secondsOnThisStretch;
      return {
        at: {
          x: stretch.from.x + (stretch.to.x - stretch.from.x) * partDone,
          y: stretch.from.y + (stretch.to.y - stretch.from.y) * partDone,
        },
        speedMps: stretch.speedMps,
      };
    }
    secondsLeft -= secondsOnThisStretch;
  }
  return { at: PARKING_SPOT, speedMps: 0 };
}

const arriving: Scenario = {
  id: 'arriving',
  radiusMeters: RADIUS_METERS,
  metersAcrossTheMap: 400,
  lengthSeconds: 23,
  momentAt(second) {
    const { at, speedMps } = courierOnTheWay(second);
    const metersFromTheAddress = Math.hypot(at.x, at.y);

    let caption: CaptionId;
    if (speedMps === 0) {
      caption = 'parkedAtTheAddress';
    } else if (metersFromTheAddress > RADIUS_METERS + 15) {
      caption = 'ridingToTheAddress';
    } else if (metersFromTheAddress > RADIUS_METERS - 15) {
      caption = 'atTheEdgeOfTheRadius';
    } else {
      caption = 'insideAndStillMoving';
    }

    return {
      courier: at,
      readings: [usualReading(at, second, speedMps)],
      caption,
    };
  },
};

// --- GPS jump -------------------------------------------------------------

const SECOND_OF_THE_JUMP = 7;

// 8 seconds later the newest readings stop contradicting the far one: at
// the fastest speed the engine admits (45 m/s), 340 m in 8 s is possible.
const SECOND_THE_READINGS_AGREE_AGAIN = 15;

const gpsJump: Scenario = {
  id: 'gps-jump',
  radiusMeters: RADIUS_METERS,
  metersAcrossTheMap: 700,
  lengthSeconds: 19,
  momentAt(second) {
    if (second === SECOND_OF_THE_JUMP) {
      return {
        courier: PARKING_SPOT,
        readings: [
          {
            at: { x: -250, y: 190 },
            accuracyMeters: 15,
            look: 'wrong',
          },
        ],
        caption: 'oneReadingFarAway',
      };
    }

    // A glitch is often followed by a moment with no reading at all.
    if (second === SECOND_OF_THE_JUMP + 1) {
      return {
        courier: PARKING_SPOT,
        readings: [],
        caption: 'oneReadingFarAway',
      };
    }

    let caption: CaptionId;
    if (second < SECOND_OF_THE_JUMP) {
      caption = 'waitingAtTheAddress';
    } else if (second < SECOND_THE_READINGS_AGREE_AGAIN) {
      caption = 'oddReadingStillRemembered';
    } else {
      caption = 'readingsAgreeAgain';
    }

    return {
      courier: PARKING_SPOT,
      readings: [usualReading(PARKING_SPOT, second, 0)],
      caption,
    };
  },
};

// --- Signal lost ------------------------------------------------------------

const SECOND_THE_GPS_GOES_SILENT = 5;

const signalLost: Scenario = {
  id: 'signal-lost',
  radiusMeters: RADIUS_METERS,
  metersAcrossTheMap: 520,
  lengthSeconds: 22,
  momentAt(second) {
    if (second < SECOND_THE_GPS_GOES_SILENT) {
      return {
        courier: PARKING_SPOT,
        readings: [usualReading(PARKING_SPOT, second, 0)],
        caption: 'waitingAtTheAddress',
      };
    }

    const silentFor = second - SECOND_THE_GPS_GOES_SILENT;
    return {
      courier: PARKING_SPOT,
      readings: [],
      caption: silentFor < 6 ? 'gpsWentSilent' : 'silenceGetsLonger',
    };
  },
};

// --- Signal lost, but the phone says it is standing still -------------------

const lostButStandingStill: Scenario = {
  id: 'lost-standing-still',
  radiusMeters: RADIUS_METERS,
  metersAcrossTheMap: 520,
  lengthSeconds: 22,
  momentAt(second) {
    if (second < SECOND_THE_GPS_GOES_SILENT) {
      return {
        courier: PARKING_SPOT,
        readings: [usualReading(PARKING_SPOT, second, 0)],
        // Reported once, at the start; it stays true until something else
        // is reported.
        phoneActivity: second === 0 ? 'stationary' : undefined,
        caption: 'waitingAndStandingStill',
      };
    }

    return {
      courier: PARKING_SPOT,
      readings: [],
      caption: 'silentButStandingStill',
    };
  },
};

// --- Fake GPS -----------------------------------------------------------------

/** A street corner 150 m from the address. */
const WHERE_THE_COURIER_REALLY_IS: Point = { x: -100, y: 110 };

const fakeGps: Scenario = {
  id: 'fake-gps',
  radiusMeters: RADIUS_METERS,
  metersAcrossTheMap: 560,
  lengthSeconds: 14,
  momentAt(second) {
    const atTheDoor = usualReading({ x: 4, y: 3 }, second, 0);
    return {
      courier: WHERE_THE_COURIER_REALLY_IS,
      readings: [{ ...atTheDoor, flaggedAsMock: true, look: 'wrong' }],
      caption: second < 7 ? 'fakeGpsAtTheDoor' : 'unmarkedProvesNothing',
    };
  },
};

// --- Approximate location only -------------------------------------------------

const approximateOnly: Scenario = {
  id: 'approximate',
  radiusMeters: RADIUS_METERS,
  metersAcrossTheMap: 560,
  lengthSeconds: 10,
  momentAt() {
    return {
      courier: PARKING_SPOT,
      readings: [
        {
          at: { x: 420, y: -380 },
          accuracyMeters: 2000,
          approximate: true,
          look: 'usual',
        },
      ],
      caption: 'blurredOnPurpose',
    };
  },
};

export const SCENARIOS: readonly Scenario[] = [
  arriving,
  gpsJump,
  signalLost,
  lostButStandingStill,
  fakeGps,
  approximateOnly,
];
