import { beforeEach, describe, expect, it } from '@jest/globals';
import {
  InvalidArgumentError,
  LocationConfidenceError,
  NativeModuleError,
} from '../errors';
import { createLocationConfidence } from '../LocationConfidence';
import type { ProximityRequest, LocationConfidenceApi } from '../types';
import {
  FakeNative,
  cancelledNativeReport,
  goldenNativeReport,
  letPromisesSettle,
} from './FakeNative';

let native: FakeNative;
let library: LocationConfidenceApi;

beforeEach(() => {
  native = new FakeNative();
  library = createLocationConfidence(() => native);
});

function request(changes: Partial<ProximityRequest> = {}): ProximityRequest {
  return {
    destination: { latitude: 0, longitude: 0 },
    radiusMeters: 100,
    ...changes,
  };
}

/** The error a Promise is rejected with. */
async function errorOf(promise: Promise<unknown>): Promise<unknown> {
  try {
    await promise;
  } catch (error) {
    return error;
  }
  throw new Error('the Promise was not rejected');
}

/**
 * A stand-in for AbortSignal that lets a test count who is listening.
 * (A real AbortSignal does not say how many listeners it has.)
 */
class CountingSignal {
  aborted = false;
  listeners: Array<() => void> = [];

  addEventListener(_type: string, listener: () => void) {
    this.listeners.push(listener);
  }

  removeEventListener(_type: string, listener: () => void) {
    this.listeners = this.listeners.filter((kept) => kept !== listener);
  }

  abort() {
    this.aborted = true;
    for (const listener of [...this.listeners]) {
      listener();
    }
  }
}

describe('getProximityReport', () => {
  it('sends the checked request and returns the report in its public shape', async () => {
    const pending = library.getProximityReport(
      request({ sessionId: '1', options: { totalDeadlineMs: 8000 } })
    );
    await letPromisesSettle();

    const requestId = native.onlyOpenRequestId();
    const sent = native.openRequests.get(requestId)?.request;
    expect(sent?.sessionId).toBe('1');
    expect(sent?.totalDeadlineMs).toBe(8000);
    expect(sent?.initialFixBudgetMs).toBe(4500);

    native.finish(requestId, { ...goldenNativeReport(), sessionId: '1' });
    const report = await pending;

    expect(report.requestId).toBe(requestId);
    expect(report.sessionId).toBe('1');
    expect(report.status).toBe('available');
    expect(report.qualityScore).toBe(75);
    expect(report.evidenceRelation).toBe('supports_inside');
    expect(native.calls).toEqual(['getReport']);
  });

  it('gives each request a name of its own', async () => {
    const first = library.getProximityReport(request());
    const second = library.getProximityReport(request({ radiusMeters: 50 }));
    await letPromisesSettle();

    const ids = [...native.openRequests.keys()];
    expect(ids).toHaveLength(2);
    expect(ids[0]).not.toBe(ids[1]);

    // CON-012: each answer goes to the request that asked for it.
    const [firstId, secondId] = ids as [string, string];
    native.finish(secondId, { ...goldenNativeReport(), radiusMeters: 50 });
    native.finish(firstId, goldenNativeReport());
    expect((await first).radiusMeters).toBe(100);
    expect((await second).radiusMeters).toBe(50);
    expect((await first).requestId).toBe(firstId);
    expect((await second).requestId).toBe(secondId);
  });

  it('GEO-012..019 a broken request is refused before anything native', async () => {
    const brokenRequests = [
      request({ destination: { latitude: 90.00001, longitude: 0 } }),
      request({ destination: { latitude: 0, longitude: 180.00001 } }),
      request({ destination: { latitude: NaN, longitude: 0 } }),
      request({ radiusMeters: Infinity }),
      request({ radiusMeters: 0 }),
      request({ radiusMeters: -1 }),
      request({ radiusMeters: 100001 }),
      request({ options: { totalDeadlineMs: 500 } }),
      request({ signal: 'now' as never }),
    ];

    for (const broken of brokenRequests) {
      const error = await errorOf(library.getProximityReport(broken));
      expect(error).toBeInstanceOf(InvalidArgumentError);
      expect((error as InvalidArgumentError).code).toBe('invalid_argument');
    }
    expect(native.calls).toEqual([]);
  });

  it('CON-001 aborted before it started: cancelled, nothing native is called', async () => {
    const controller = new AbortController();
    controller.abort();

    const report = await library.getProximityReport(
      request({ sessionId: '5', signal: controller.signal })
    );

    expect(report.status).toBe('cancelled');
    expect(report.source).toBe('unavailable');
    expect(report.measuredPosition).toBeNull();
    expect(report.sessionId).toBe('5');
    expect(report.diagnostics.reasonCodes).toEqual(['request_cancelled']);
    expect(native.calls).toEqual([]);
  });

  it('CON-001 even an aborted request is checked first', async () => {
    const controller = new AbortController();
    controller.abort();

    const error = await errorOf(
      library.getProximityReport(
        request({ radiusMeters: -1, signal: controller.signal })
      )
    );
    expect(error).toBeInstanceOf(InvalidArgumentError);
    expect(native.calls).toEqual([]);
  });

  it('CON-002 aborted in the middle: the native side is told, one answer comes', async () => {
    const signal = new CountingSignal();
    let howManyAnswers = 0;
    const pending = library
      .getProximityReport(request({ signal: signal as unknown as AbortSignal }))
      .then((report) => {
        howManyAnswers += 1;
        return report;
      });
    await letPromisesSettle();
    const requestId = native.onlyOpenRequestId();
    expect(signal.listeners).toHaveLength(1);

    signal.abort();
    expect(native.cancelledRequestIds).toEqual([requestId]);

    // The native side answers the cancel with the final report.
    native.finish(requestId, cancelledNativeReport());
    const report = await pending;

    expect(report.status).toBe('cancelled');
    expect(report.requestId).toBe(requestId);
    expect(howManyAnswers).toBe(1);
    expect(signal.listeners).toHaveLength(0);
  });

  it('CON-005 aborted after the answer: nothing is cancelled or rewritten', async () => {
    const signal = new CountingSignal();
    const pending = library.getProximityReport(
      request({ signal: signal as unknown as AbortSignal })
    );
    await letPromisesSettle();
    native.finish(native.onlyOpenRequestId(), goldenNativeReport());
    const report = await pending;
    const before = JSON.stringify(report);

    signal.abort();
    await letPromisesSettle();

    expect(native.cancelledRequestIds).toEqual([]);
    expect(JSON.stringify(report)).toBe(before);
    expect(report.status).toBe('available');
    expect(signal.listeners).toHaveLength(0);
  });

  it('CON-006 aborting twice cancels once and breaks nothing', async () => {
    const controller = new AbortController();
    const pending = library.getProximityReport(
      request({ signal: controller.signal })
    );
    await letPromisesSettle();
    const requestId = native.onlyOpenRequestId();

    controller.abort();
    controller.abort();
    expect(native.cancelledRequestIds).toEqual([requestId]);

    native.finish(requestId, cancelledNativeReport());
    expect((await pending).status).toBe('cancelled');
  });

  it('CON-013 aborting one request leaves the other one alone', async () => {
    const controller = new AbortController();
    const cancelled = library.getProximityReport(
      request({ signal: controller.signal })
    );
    const kept = library.getProximityReport(request());
    await letPromisesSettle();
    const [cancelledId, keptId] = [...native.openRequests.keys()] as [
      string,
      string,
    ];

    controller.abort();
    expect(native.cancelledRequestIds).toEqual([cancelledId]);

    native.finish(cancelledId, cancelledNativeReport());
    native.finish(keptId, goldenNativeReport());
    expect((await cancelled).status).toBe('cancelled');
    expect((await kept).status).toBe('available');
  });

  it('stops listening to the signal when the native side fails', async () => {
    const signal = new CountingSignal();
    native.nextRejection = { code: 'native_internal_error', message: 'full' };

    const error = await errorOf(
      library.getProximityReport(
        request({ signal: signal as unknown as AbortSignal })
      )
    );

    expect(error).toBeInstanceOf(NativeModuleError);
    expect(signal.listeners).toHaveLength(0);
  });

  it('refuses a report from another version of the native code', async () => {
    const pending = library.getProximityReport(request());
    await letPromisesSettle();
    native.finish(native.onlyOpenRequestId(), {
      ...goldenNativeReport(),
      transportVersion: 1,
    });

    const error = await errorOf(pending);
    expect(error).toBeInstanceOf(NativeModuleError);
    expect((error as NativeModuleError).code).toBe('native_version_mismatch');
  });
});

describe('errors that come from the native side', () => {
  it('become errors of the library, with the same code', async () => {
    native.nextRejection = {
      code: 'platform_unsupported',
      message: 'no library for this processor',
    };
    const unsupported = await errorOf(library.startSession());
    expect(unsupported).toBeInstanceOf(NativeModuleError);
    expect((unsupported as NativeModuleError).code).toBe(
      'platform_unsupported'
    );
    expect((unsupported as Error).message).toBe(
      'no library for this processor'
    );

    native.nextRejection = { code: 'no_activity', message: 'no screen' };
    const noScreen = await errorOf(library.requestLocationPermission());
    expect((noScreen as NativeModuleError).code).toBe('no_activity');
  });

  it('a request the native side refuses is an InvalidArgumentError', async () => {
    native.nextRejection = { code: 'invalid_argument', message: 'bad radius' };
    const error = await errorOf(library.getProximityReport(request()));
    expect(error).toBeInstanceOf(InvalidArgumentError);
  });

  it('anything unexpected is a native_internal_error', async () => {
    native.nextRejection = { code: 'EUNSPECIFIED', message: 'boom' };
    const withOddCode = await errorOf(library.getCapabilities());
    expect((withOddCode as NativeModuleError).code).toBe(
      'native_internal_error'
    );

    native.nextRejection = { message: 'no code at all' };
    const withoutCode = await errorOf(library.getAuthorization());
    expect((withoutCode as NativeModuleError).code).toBe(
      'native_internal_error'
    );
  });

  it('a missing native module fails at the first use, with what to do', async () => {
    const missing = createLocationConfidence(() => {
      throw new NativeModuleError('native_module_missing', 'rebuild the app');
    });

    const calls = [
      missing.getCapabilities(),
      missing.getAuthorization(),
      missing.requestLocationPermission(),
      missing.requestMotionPermission(),
      missing.startSession(),
      missing.clearHistory('1'),
      missing.stopSession('1'),
      missing.getProximityReport(request()),
      missing.simulation.enable(),
      missing.simulation.disable(),
      missing.simulation.pushReadings([]),
      missing.simulation.setPhoneActivity('walking'),
    ];
    for (const call of calls) {
      const error = await errorOf(call);
      expect(error).toBeInstanceOf(NativeModuleError);
      expect((error as NativeModuleError).code).toBe('native_module_missing');
    }
  });
});

describe('capabilities and permissions', () => {
  it('reads the capabilities without asking for anything', async () => {
    const capabilities = await library.getCapabilities();
    expect(capabilities).toEqual({
      locationServicesEnabled: true,
      locationProviderAvailable: true,
      motionSupported: false,
      mockDetectionSupported: true,
      simulationEnabled: false,
      reasonCodes: ['motion_unavailable'],
    });
    expect(native.calls).toEqual(['getCapabilities']);
  });

  it('only the two request functions show a permission dialog', async () => {
    native.authorization = {
      location: 'denied',
      precision: 'unknown',
      motion: 'unsupported',
    };
    expect((await library.getAuthorization()).locationGranted).toBe(false);
    expect(native.calls).toEqual(['getAuthorization']);

    native.authorization = {
      location: 'granted',
      precision: 'precise',
      motion: 'unsupported',
    };
    expect((await library.requestLocationPermission()).locationGranted).toBe(
      true
    );
    expect((await library.requestMotionPermission()).motion).toBe(
      'unsupported'
    );
    expect(native.calls).toEqual([
      'getAuthorization',
      'requestLocationPermission',
      'requestMotionPermission',
    ]);
  });
});

describe('sessions', () => {
  it('SES-001 opens a session that does not collect by default', async () => {
    const session = await library.startSession();

    expect(session).toEqual({
      id: '1',
      monitoring: 'not_requested',
      reasonCodes: [],
    });
    expect(native.sessionOptionsReceived).toEqual([
      {
        keepCollectingReadings: false,
        timeBetweenReadingsMs: 5000,
        longestCollectingTimeMs: 900000,
      },
    ]);
  });

  it('SES-003 collecting without permission: blocked, with the reason', async () => {
    native.sessionResult = {
      id: '2',
      monitoring: 'blocked',
      reasonCodes: ['permission_not_determined'],
    };

    const session = await library.startSession({
      collectionMode: 'foreground_monitoring',
    });

    expect(session.monitoring).toBe('blocked');
    expect(session.reasonCodes).toEqual(['permission_not_determined']);
    // It never asks for the permission by itself.
    expect(native.calls).toEqual(['startSession']);
  });

  it('SES-013 broken options are refused before anything native', async () => {
    const zeroInterval = await errorOf(
      library.startSession({ monitoringIntervalMs: 0 })
    );
    const negativeDuration = await errorOf(
      library.startSession({ maxMonitoringDurationMs: -1 })
    );

    expect(zeroInterval).toBeInstanceOf(InvalidArgumentError);
    expect(negativeDuration).toBeInstanceOf(InvalidArgumentError);
    expect(native.calls).toEqual([]);
  });

  it('too many sessions open is an error that says what to do', async () => {
    native.sessionResult = {
      id: '',
      monitoring: 'not_requested',
      reasonCodes: ['busy'],
    };
    const error = await errorOf(library.startSession());
    expect(error).toBeInstanceOf(LocationConfidenceError);
    expect((error as LocationConfidenceError).code).toBe('busy');
  });

  it('CON-008 stops and clears by id, as many times as the app wants', async () => {
    await library.clearHistory('3');
    await library.stopSession('3');
    await library.stopSession('3');

    expect(native.sessionIdsCleared).toEqual(['3']);
    expect(native.sessionIdsStopped).toEqual(['3', '3']);
  });

  it('refuses a session id that is not a text', async () => {
    expect(await errorOf(library.stopSession(''))).toBeInstanceOf(
      InvalidArgumentError
    );
    expect(await errorOf(library.clearHistory(3 as never))).toBeInstanceOf(
      InvalidArgumentError
    );
    expect(native.calls).toEqual([]);
  });
});

describe('simulation', () => {
  it('turns the simulated source on and off', async () => {
    await library.simulation.enable();
    await library.simulation.disable();
    expect(native.simulationSwitches).toEqual([true, false]);
  });

  it('hands the made-up readings over, with the defaults filled in', async () => {
    await library.simulation.pushReadings([
      { latitude: 1, longitude: 2, accuracyMeters: 8 },
      { latitude: 1, longitude: 2, accuracyMeters: 8, flaggedAsMock: true },
    ]);

    expect(native.readingsReceived).toEqual([
      [
        {
          latitude: 1,
          longitude: 2,
          accuracyMeters: 8,
          ageMs: 0,
          speedMps: null,
          speedAccuracyMps: null,
          flaggedAsMock: false,
          approximate: false,
        },
        {
          latitude: 1,
          longitude: 2,
          accuracyMeters: 8,
          ageMs: 0,
          speedMps: null,
          speedAccuracyMps: null,
          flaggedAsMock: true,
          approximate: false,
        },
      ],
    ]);
  });

  it('refuses a made-up reading that makes no sense', async () => {
    const error = await errorOf(
      library.simulation.pushReadings([
        { latitude: 200, longitude: 0, accuracyMeters: 8 },
      ])
    );
    expect(error).toBeInstanceOf(InvalidArgumentError);
    expect(native.calls).toEqual([]);
  });

  it("sends the phone's activity as the engine's numbers", async () => {
    await library.simulation.setPhoneActivity('stationary');
    await library.simulation.setPhoneActivity('walking', 'medium');
    await library.simulation.setPhoneActivity('running', 'low');
    await library.simulation.setPhoneActivity('cycling');
    await library.simulation.setPhoneActivity('vehicle', 'high');
    await library.simulation.setPhoneActivity(null);

    expect(native.phoneActivitiesReceived).toEqual([
      [0, 0],
      [1, 1],
      [2, 2],
      [3, 0],
      [4, 0],
      [-1, 0],
    ]);
  });

  it('refuses an activity or a confidence that cannot be pretended', async () => {
    const calls = [
      library.simulation.setPhoneActivity('unknown' as never),
      library.simulation.setPhoneActivity('flying' as never),
      library.simulation.setPhoneActivity('walking', 'unknown' as never),
      library.simulation.setPhoneActivity('walking', 'sure' as never),
    ];
    for (const call of calls) {
      expect(await errorOf(call)).toBeInstanceOf(InvalidArgumentError);
    }
    expect(native.calls).toEqual([]);
  });
});
