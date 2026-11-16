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
 */

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
  /** One or two sentences that tell the viewer what is going on. */
  caption: string;
}>;

export type Scenario = Readonly<{
  id: string;
  title: string;
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
  title: 'Arriving',
  radiusMeters: RADIUS_METERS,
  metersAcrossTheMap: 400,
  lengthSeconds: 23,
  momentAt(second) {
    const { at, speedMps } = courierOnTheWay(second);
    const metersFromTheAddress = Math.hypot(at.x, at.y);

    let caption: string;
    if (speedMps === 0) {
      caption =
        'Parked in front of the address. One reading after another tells ' +
        'the same story.';
    } else if (metersFromTheAddress > RADIUS_METERS + 15) {
      caption =
        'A courier rides to the address. The GPS delivers a reading every ' +
        'second, each with an error of about 8 m.';
    } else if (metersFromTheAddress > RADIUS_METERS - 15) {
      caption =
        'At the edge of the delivery radius. With 8 m of error the phone ' +
        'may be on either side of the line.';
    } else {
      caption = 'Inside the delivery radius and still moving.';
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
  title: 'GPS jump',
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
        caption:
          'The courier has not moved, but one reading lands 340 m away ' +
          '(the red dot). Nobody travels that far in one second.',
      };
    }

    let caption: string;
    if (second < SECOND_OF_THE_JUMP) {
      caption = 'The courier waits in front of the address.';
    } else if (second < SECOND_THE_READINGS_AGREE_AGAIN) {
      caption =
        'Good readings are back, but the odd one is still remembered and ' +
        'contradicts them. Trust stays lower for a few seconds.';
    } else {
      caption = 'The readings tell one story again.';
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
  title: 'Signal lost',
  radiusMeters: RADIUS_METERS,
  metersAcrossTheMap: 520,
  lengthSeconds: 22,
  momentAt(second) {
    if (second < SECOND_THE_GPS_GOES_SILENT) {
      return {
        courier: PARKING_SPOT,
        readings: [usualReading(PARKING_SPOT, second, 0)],
        caption: 'The courier waits in front of the address.',
      };
    }

    const silentFor = second - SECOND_THE_GPS_GOES_SILENT;
    return {
      courier: PARKING_SPOT,
      readings: [],
      caption:
        silentFor < 6
          ? 'The GPS goes silent, as in a garage or a lobby. Nothing tells ' +
            'whether the phone moved since the last reading.'
          : 'The longer the silence, the farther the phone could be from ' +
            'its last reading.',
    };
  },
};

// --- Signal lost, but the phone says it is standing still -------------------

const lostButStandingStill: Scenario = {
  id: 'lost-standing-still',
  title: 'Lost, standing still',
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
        caption:
          'The courier waits in front of the address. The phone reports ' +
          'that it is standing still (this report is simulated too).',
      };
    }

    return {
      courier: PARKING_SPOT,
      readings: [],
      caption:
        'The same silence, but the phone keeps reporting that it stands ' +
        'still. The last reading stays useful for much longer.',
    };
  },
};

// --- Fake GPS -----------------------------------------------------------------

/** A street corner 150 m from the address. */
const WHERE_THE_COURIER_REALLY_IS: Point = { x: -100, y: 110 };

const fakeGps: Scenario = {
  id: 'fake-gps',
  title: 'Fake GPS',
  radiusMeters: RADIUS_METERS,
  metersAcrossTheMap: 560,
  lengthSeconds: 14,
  momentAt(second) {
    const atTheDoor = usualReading({ x: 4, y: 3 }, second, 0);
    return {
      courier: WHERE_THE_COURIER_REALLY_IS,
      readings: [{ ...atTheDoor, flaggedAsMock: true, look: 'wrong' }],
      caption:
        second < 7
          ? 'The courier is 150 m away. A fake-GPS app reports readings ' +
            'at the door (the red dots), and the system marks them as mock.'
          : 'A reading that is not marked proves nothing either. This is ' +
            'evidence quality, never proof of presence.',
    };
  },
};

// --- Approximate location only -------------------------------------------------

const approximateOnly: Scenario = {
  id: 'approximate',
  title: 'Approximate only',
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
      caption:
        'The user allowed only the approximate location. The system blurs ' +
        'every reading on purpose: about 2 km of error.',
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
