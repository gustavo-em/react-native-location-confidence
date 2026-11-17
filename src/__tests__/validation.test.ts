import { describe, expect, it } from '@jest/globals';
import { InvalidArgumentError } from '../errors';
import type { ProximityRequest, SessionOptions } from '../types';
import {
  LIMITS,
  checkProximityRequest,
  checkSessionOptions,
  checkSimulatedReadings,
} from '../validation';

const destination = { latitude: -20.8117, longitude: -49.3768 };

function request(changes: Partial<ProximityRequest> = {}): ProximityRequest {
  return { destination, radiusMeters: 100, ...changes };
}

/** The name of the argument that an InvalidArgumentError complains about. */
function argumentRefusedBy(action: () => unknown): string {
  try {
    action();
  } catch (error) {
    if (error instanceof InvalidArgumentError) {
      return error.argument;
    }
    throw error;
  }
  throw new Error('nothing was refused');
}

describe('the destination', () => {
  it('GEO-012 refuses a latitude just above 90', () => {
    const refused = argumentRefusedBy(() =>
      checkProximityRequest(
        request({ destination: { latitude: 90.00001, longitude: 0 } })
      )
    );
    expect(refused).toBe('destination.latitude');
  });

  it('GEO-013 refuses a longitude just above 180', () => {
    const refused = argumentRefusedBy(() =>
      checkProximityRequest(
        request({ destination: { latitude: 0, longitude: 180.00001 } })
      )
    );
    expect(refused).toBe('destination.longitude');
  });

  it('GEO-014 refuses a latitude that is not a number', () => {
    const refused = argumentRefusedBy(() =>
      checkProximityRequest(
        request({ destination: { latitude: NaN, longitude: 0 } })
      )
    );
    expect(refused).toBe('destination.latitude');
  });

  it('accepts the poles and the date line themselves', () => {
    const north = checkProximityRequest(
      request({ destination: { latitude: 90, longitude: 180 } })
    );
    const south = checkProximityRequest(
      request({ destination: { latitude: -90, longitude: -180 } })
    );
    expect([north.latitude, north.longitude]).toEqual([90, 180]);
    expect([south.latitude, south.longitude]).toEqual([-90, -180]);
  });

  it('GEO-026 takes negative zero as plain zero', () => {
    const checked = checkProximityRequest(
      request({ destination: { latitude: -0, longitude: -0 } })
    );
    expect(Object.is(checked.latitude, 0)).toBe(true);
    expect(Object.is(checked.longitude, 0)).toBe(true);
  });

  it('accepts the point (0, 0): it is a real place, not a missing value', () => {
    const checked = checkProximityRequest(
      request({ destination: { latitude: 0, longitude: 0 } })
    );
    expect([checked.latitude, checked.longitude]).toEqual([0, 0]);
  });

  it('refuses a destination that is missing or has texts for numbers', () => {
    const missing = { radiusMeters: 100 } as unknown as ProximityRequest;
    expect(argumentRefusedBy(() => checkProximityRequest(missing))).toBe(
      'request.destination'
    );

    const withTexts = request({
      destination: { latitude: '10', longitude: 0 } as never,
    });
    expect(argumentRefusedBy(() => checkProximityRequest(withTexts))).toBe(
      'destination.latitude'
    );
  });
});

describe('the radius', () => {
  it('GEO-015 refuses an infinite radius', () => {
    expect(
      argumentRefusedBy(() =>
        checkProximityRequest(request({ radiusMeters: Infinity }))
      )
    ).toBe('radiusMeters');
  });

  it('GEO-016 refuses a radius of zero', () => {
    expect(
      argumentRefusedBy(() =>
        checkProximityRequest(request({ radiusMeters: 0 }))
      )
    ).toBe('radiusMeters');
  });

  it('GEO-017 refuses a negative radius', () => {
    expect(
      argumentRefusedBy(() =>
        checkProximityRequest(request({ radiusMeters: -1 }))
      )
    ).toBe('radiusMeters');
  });

  it('GEO-018 accepts a radius of exactly 100000 m', () => {
    expect(
      checkProximityRequest(request({ radiusMeters: 100000 })).radiusMeters
    ).toBe(100000);
  });

  it('GEO-019 refuses a radius of 100001 m', () => {
    expect(
      argumentRefusedBy(() =>
        checkProximityRequest(request({ radiusMeters: 100001 }))
      )
    ).toBe('radiusMeters');
  });

  it('accepts a radius smaller than one meter', () => {
    expect(
      checkProximityRequest(request({ radiusMeters: 0.5 })).radiusMeters
    ).toBe(0.5);
  });
});

describe('the options of a request', () => {
  it('fills in every default', () => {
    expect(checkProximityRequest(request())).toEqual({
      sessionId: null,
      latitude: -20.8117,
      longitude: -49.3768,
      radiusMeters: 100,
      totalDeadlineMs: 12000,
      initialFixBudgetMs: 4500,
      recoveryWindowMs: 7000,
      maxFreshAgeMs: 10000,
      maxFallbackAgeMs: 60000,
      maxHorizontalAccuracyMeters: 50,
      enableMotionAssistance: true,
      allowHistoricalFallback: true,
      maxPlausibleSpeedMps: 45,
      zeroScoreForMockLocation: true,
    });
  });

  it('keeps what the app chose', () => {
    const checked = checkProximityRequest(
      request({
        sessionId: '3',
        options: {
          totalDeadlineMs: 20000,
          initialFixBudgetMs: 3000,
          recoveryWindowMs: 0,
          maxFreshAgeMs: 5000,
          maxFallbackAgeMs: 30000,
          maxHorizontalAccuracyMeters: 25,
          enableMotionAssistance: false,
          allowHistoricalFallback: false,
          maxPlausibleSpeedMps: 30,
          mockLocationPolicy: 'flag_only',
        },
      })
    );
    expect(checked).toMatchObject({
      sessionId: '3',
      totalDeadlineMs: 20000,
      initialFixBudgetMs: 3000,
      recoveryWindowMs: 0,
      maxFreshAgeMs: 5000,
      maxFallbackAgeMs: 30000,
      maxHorizontalAccuracyMeters: 25,
      enableMotionAssistance: false,
      allowHistoricalFallback: false,
      maxPlausibleSpeedMps: 30,
      zeroScoreForMockLocation: false,
    });
  });

  it('never lets the first attempt be longer than the whole request', () => {
    const checked = checkProximityRequest(
      request({ options: { totalDeadlineMs: 2000, initialFixBudgetMs: 4500 } })
    );
    expect(checked.initialFixBudgetMs).toBe(2000);

    const withDefaults = checkProximityRequest(
      request({ options: { totalDeadlineMs: 1000 } })
    );
    expect(withDefaults.initialFixBudgetMs).toBe(1000);
  });

  it('accepts the limits of the total time and refuses beyond them', () => {
    const shortest = LIMITS.totalDeadlineMs.min;
    const longest = LIMITS.totalDeadlineMs.max;
    expect(
      checkProximityRequest(request({ options: { totalDeadlineMs: shortest } }))
        .totalDeadlineMs
    ).toBe(1000);
    expect(
      checkProximityRequest(request({ options: { totalDeadlineMs: longest } }))
        .totalDeadlineMs
    ).toBe(60000);

    for (const totalDeadlineMs of [999, 60001, 1500.5, NaN, Infinity]) {
      expect(
        argumentRefusedBy(() =>
          checkProximityRequest(request({ options: { totalDeadlineMs } }))
        )
      ).toBe('options.totalDeadlineMs');
    }
  });

  it('refuses an extra time outside 0..20000', () => {
    for (const recoveryWindowMs of [-1, 20001, 0.5]) {
      expect(
        argumentRefusedBy(() =>
          checkProximityRequest(request({ options: { recoveryWindowMs } }))
        )
      ).toBe('options.recoveryWindowMs');
    }
    expect(
      checkProximityRequest(request({ options: { recoveryWindowMs: 20000 } }))
        .recoveryWindowMs
    ).toBe(20000);
  });

  it('refuses a negative first attempt', () => {
    expect(
      argumentRefusedBy(() =>
        checkProximityRequest(request({ options: { initialFixBudgetMs: -1 } }))
      )
    ).toBe('options.initialFixBudgetMs');
  });

  it('refuses an oldest age that is smaller than the fresh age', () => {
    expect(
      argumentRefusedBy(() =>
        checkProximityRequest(
          request({ options: { maxFreshAgeMs: 10000, maxFallbackAgeMs: 9999 } })
        )
      )
    ).toBe('options.maxFallbackAgeMs');

    const equal = checkProximityRequest(
      request({ options: { maxFreshAgeMs: 10000, maxFallbackAgeMs: 10000 } })
    );
    expect(equal.maxFallbackAgeMs).toBe(10000);
  });

  it('refuses ages that are negative or beyond what the engine remembers', () => {
    expect(
      argumentRefusedBy(() =>
        checkProximityRequest(request({ options: { maxFreshAgeMs: -1 } }))
      )
    ).toBe('options.maxFreshAgeMs');
    expect(
      argumentRefusedBy(() =>
        checkProximityRequest(
          request({ options: { maxFallbackAgeMs: 120001 } })
        )
      )
    ).toBe('options.maxFallbackAgeMs');
  });

  it('refuses a GPS error limit or a speed limit that is not above zero', () => {
    for (const maxHorizontalAccuracyMeters of [0, -5, NaN]) {
      expect(
        argumentRefusedBy(() =>
          checkProximityRequest(
            request({ options: { maxHorizontalAccuracyMeters } })
          )
        )
      ).toBe('options.maxHorizontalAccuracyMeters');
    }
    for (const maxPlausibleSpeedMps of [0, -1, Infinity]) {
      expect(
        argumentRefusedBy(() =>
          checkProximityRequest(request({ options: { maxPlausibleSpeedMps } }))
        )
      ).toBe('options.maxPlausibleSpeedMps');
    }
  });

  it('refuses values of the wrong type', () => {
    expect(
      argumentRefusedBy(() =>
        checkProximityRequest(
          request({ options: { totalDeadlineMs: '12000' as never } })
        )
      )
    ).toBe('options.totalDeadlineMs');
    expect(
      argumentRefusedBy(() =>
        checkProximityRequest(
          request({ options: { enableMotionAssistance: 'yes' as never } })
        )
      )
    ).toBe('options.enableMotionAssistance');
    expect(
      argumentRefusedBy(() =>
        checkProximityRequest(
          request({ options: { mockLocationPolicy: 'ignore' as never } })
        )
      )
    ).toBe('options.mockLocationPolicy');
    expect(
      argumentRefusedBy(() =>
        checkProximityRequest(request({ options: 'fast' as never }))
      )
    ).toBe('options');
  });

  it('refuses a session id that is not a non-empty text', () => {
    expect(
      argumentRefusedBy(() => checkProximityRequest(request({ sessionId: '' })))
    ).toBe('sessionId');
    expect(
      argumentRefusedBy(() =>
        checkProximityRequest(request({ sessionId: 7 as never }))
      )
    ).toBe('sessionId');
  });

  it('refuses a request that is not an object', () => {
    expect(
      argumentRefusedBy(() => checkProximityRequest(undefined as never))
    ).toBe('request');
    expect(argumentRefusedBy(() => checkProximityRequest(null as never))).toBe(
      'request'
    );
  });
});

describe('the options of a session', () => {
  it('SES-001 by default the session does not collect', () => {
    expect(checkSessionOptions(undefined)).toEqual({
      keepCollectingReadings: false,
      timeBetweenReadingsMs: 5000,
      longestCollectingTimeMs: 900000,
    });
    expect(checkSessionOptions({}).keepCollectingReadings).toBe(false);
  });

  it('SES-002 collecting has to be asked for', () => {
    const checked = checkSessionOptions({
      collectionMode: 'foreground_monitoring',
      monitoringIntervalMs: 2000,
      maxMonitoringDurationMs: 60000,
    });
    expect(checked).toEqual({
      keepCollectingReadings: true,
      timeBetweenReadingsMs: 2000,
      longestCollectingTimeMs: 60000,
    });
  });

  it('SES-013 refuses an interval of zero', () => {
    expect(
      argumentRefusedBy(() => checkSessionOptions({ monitoringIntervalMs: 0 }))
    ).toBe('options.monitoringIntervalMs');
  });

  it('SES-013 refuses a negative longest duration', () => {
    expect(
      argumentRefusedBy(() =>
        checkSessionOptions({ maxMonitoringDurationMs: -1 })
      )
    ).toBe('options.maxMonitoringDurationMs');
  });

  it('refuses intervals outside 1 s .. 60 s', () => {
    for (const monitoringIntervalMs of [999, 60001, 1500.5, NaN]) {
      expect(
        argumentRefusedBy(() => checkSessionOptions({ monitoringIntervalMs }))
      ).toBe('options.monitoringIntervalMs');
    }
  });

  it('shortens a longest duration above one hour to one hour', () => {
    expect(
      checkSessionOptions({ maxMonitoringDurationMs: 3 * 3600000 })
        .longestCollectingTimeMs
    ).toBe(3600000);
    expect(
      checkSessionOptions({ maxMonitoringDurationMs: 3600000 })
        .longestCollectingTimeMs
    ).toBe(3600000);
  });

  it('refuses an unknown way of collecting', () => {
    const options = { collectionMode: 'background' } as unknown;
    expect(
      argumentRefusedBy(() => checkSessionOptions(options as SessionOptions))
    ).toBe('options.collectionMode');
    expect(argumentRefusedBy(() => checkSessionOptions('fast' as never))).toBe(
      'options'
    );
  });
});

describe('the readings an app makes up', () => {
  it('fills in the defaults', () => {
    expect(
      checkSimulatedReadings([
        { latitude: -20.8, longitude: -49.3, accuracyMeters: 8 },
      ])
    ).toEqual([
      {
        latitude: -20.8,
        longitude: -49.3,
        accuracyMeters: 8,
        ageMs: 0,
        speedMps: null,
        speedAccuracyMps: null,
        flaggedAsMock: false,
        approximate: false,
      },
    ]);
  });

  it('keeps what the app chose', () => {
    const [checked] = checkSimulatedReadings([
      {
        latitude: 1,
        longitude: 2,
        accuracyMeters: 30,
        ageMs: 4000,
        speedMps: 3,
        speedAccuracyMps: 0.5,
        flaggedAsMock: true,
        approximate: true,
      },
    ]);
    expect(checked).toEqual({
      latitude: 1,
      longitude: 2,
      accuracyMeters: 30,
      ageMs: 4000,
      speedMps: 3,
      speedAccuracyMps: 0.5,
      flaggedAsMock: true,
      approximate: true,
    });
  });

  it('names the reading that is wrong', () => {
    const good = { latitude: 0, longitude: 0, accuracyMeters: 5 };
    expect(
      argumentRefusedBy(() =>
        checkSimulatedReadings([good, { ...good, latitude: 91 }])
      )
    ).toBe('readings[1].latitude');
    expect(
      argumentRefusedBy(() =>
        checkSimulatedReadings([{ ...good, accuracyMeters: -1 }])
      )
    ).toBe('readings[0].accuracyMeters');
    expect(
      argumentRefusedBy(() => checkSimulatedReadings([{ ...good, ageMs: -1 }]))
    ).toBe('readings[0].ageMs');
    expect(
      argumentRefusedBy(() => checkSimulatedReadings('none' as never))
    ).toBe('readings');
  });
});
