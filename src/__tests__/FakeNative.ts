/*
 * A native module made of plain JavaScript, for the tests.
 *
 * It writes down every call it receives, so a test can say "nothing native
 * was called". A request stays open until the test finishes it, the
 * same way a real one stays open while the GPS is working.
 */
import type { NativePort } from '../LocationConfidence';
import type {
  NativeReport,
  NativeAuthorization,
  NativeCapabilities,
  NativeRequest,
  NativeSessionOptions,
  NativeSessionResult,
  NativeSimulatedReading,
} from '../NativeLocationConfidence';

type OpenRequest = {
  request: NativeRequest;
  resolve: (answer: NativeReport) => void;
  reject: (error: unknown) => void;
};

/** 20 m north of the point (0, 0), in degrees of latitude. */
const TWENTY_METERS_NORTH_IN_DEGREES = (20 * 180) / (Math.PI * 6371008.8);

/**
 * What the native side sends for the "golden scenario": the destination is
 * the point (0, 0) with a radius of 100 m, and one reading arrives 20 m to
 * the north, with a GPS error of 8 m, taken 1 s before.
 *
 * The values are the ones the engine really answers for that scenario.
 * They are checked in C++ (cpp/tests/TestFacade.cpp) and in Kotlin
 * (CoreEngineTest.kt); here they only have to cross the mapper unchanged.
 */
export function goldenNativeReport(): NativeReport {
  return {
    transportVersion: 2,
    sessionId: null,
    status: 0,
    source: 0,
    hasPosition: true,
    latitude: TWENTY_METERS_NORTH_IN_DEGREES,
    longitude: 0,
    distanceMeters: 20,
    horizontalAccuracyMeters: 8,
    effectiveUncertaintyMeters: 53,
    locationAgeMs: 1000,
    mockSignal: 1,
    radiusMeters: 100,
    ageReliability: 1,
    permissionPrecision: 0,
    evidenceRelation: 0,
    qualityScore: 75,
    hasScoreBreakdown: true,
    accuracyFactor: 0.92,
    freshnessFactor: 59 / 60,
    movementFactor: 0.55,
    consistencyFactor: 0.6,
    rawScore: 81,
    appliedCaps: ['single_fix'],
    motionState: 5,
    motionConfidence: 3,
    continuousStationaritySupported: false,
    hasCoverageStart: false,
    coverageStartAgeMs: 0,
    gapsDetected: false,
    reasonCodes: ['uncertainty_model_heuristic'],
    phaseTrace: ['started', 'first_attempt', 'finished'],
    initialFixTimedOut: false,
    recoveryAttempted: false,
    fallbackCause: null,
    samplesReceived: 1,
    uniqueSamplesRetained: 1,
    samplesAnalyzed: 1,
    outliersFlagged: 0,
    durationMs: 2000,
  };
}

/** What the native side sends when a request is cancelled. */
export function cancelledNativeReport(): NativeReport {
  return {
    ...goldenNativeReport(),
    status: 3,
    source: 3,
    hasPosition: false,
    latitude: 0,
    longitude: 0,
    distanceMeters: 0,
    horizontalAccuracyMeters: 0,
    effectiveUncertaintyMeters: 0,
    locationAgeMs: 0,
    mockSignal: 0,
    ageReliability: 0,
    evidenceRelation: 2,
    qualityScore: 0,
    hasScoreBreakdown: false,
    accuracyFactor: 0,
    freshnessFactor: 0,
    movementFactor: 0,
    consistencyFactor: 0,
    rawScore: 0,
    appliedCaps: [],
    reasonCodes: ['request_cancelled'],
    samplesReceived: 0,
    uniqueSamplesRetained: 0,
    samplesAnalyzed: 0,
  };
}

export class FakeNative implements NativePort {
  /** The name of every native function that was called, in order. */
  calls: string[] = [];

  openRequests = new Map<string, OpenRequest>();
  cancelledRequestIds: string[] = [];
  sessionOptionsReceived: NativeSessionOptions[] = [];
  readingsReceived: NativeSimulatedReading[][] = [];
  phoneActivitiesReceived: Array<[number, number]> = [];
  simulationSwitches: boolean[] = [];
  sessionIdsStopped: string[] = [];
  sessionIdsCleared: string[] = [];

  capabilities: NativeCapabilities = {
    locationServicesEnabled: true,
    locationProviderAvailable: true,
    motionSupported: false,
    mockDetectionSupported: true,
    simulationEnabled: false,
    coreAbiVersion: 2,
    reasonCodes: ['motion_unavailable'],
  };

  authorization: NativeAuthorization = {
    location: 'granted',
    precision: 'precise',
    motion: 'unsupported',
  };

  sessionResult: NativeSessionResult = {
    id: '1',
    monitoring: 'not_requested',
    reasonCodes: [],
  };

  /** When set, the next call that returns a Promise is rejected with it. */
  nextRejection: { code?: string; message: string } | null = null;

  private answer<T>(name: string, value: T): Promise<T> {
    this.calls.push(name);
    if (this.nextRejection !== null) {
      const rejection = Object.assign(
        new Error(this.nextRejection.message),
        this.nextRejection
      );
      this.nextRejection = null;
      return Promise.reject(rejection);
    }
    return Promise.resolve(value);
  }

  getCapabilities() {
    return this.answer('getCapabilities', this.capabilities);
  }

  getAuthorization() {
    return this.answer('getAuthorization', this.authorization);
  }

  requestLocationPermission() {
    return this.answer('requestLocationPermission', this.authorization);
  }

  requestMotionPermission() {
    return this.answer('requestMotionPermission', this.authorization);
  }

  startSession(options: NativeSessionOptions) {
    this.sessionOptionsReceived.push(options);
    return this.answer('startSession', this.sessionResult);
  }

  clearHistory(sessionId: string) {
    this.sessionIdsCleared.push(sessionId);
    return this.answer('clearHistory', undefined);
  }

  stopSession(sessionId: string) {
    this.sessionIdsStopped.push(sessionId);
    return this.answer('stopSession', undefined);
  }

  getReport(requestId: string, request: NativeRequest) {
    this.calls.push('getReport');
    if (this.nextRejection !== null) {
      const rejection = Object.assign(
        new Error(this.nextRejection.message),
        this.nextRejection
      );
      this.nextRejection = null;
      return Promise.reject(rejection);
    }
    return new Promise<NativeReport>((resolve, reject) => {
      this.openRequests.set(requestId, { request, resolve, reject });
    });
  }

  cancelRequest(requestId: string) {
    this.calls.push('cancelRequest');
    this.cancelledRequestIds.push(requestId);
  }

  setSimulationEnabled(enabled: boolean) {
    this.simulationSwitches.push(enabled);
    return this.answer('setSimulationEnabled', undefined);
  }

  simulateReadings(readings: NativeSimulatedReading[]) {
    this.readingsReceived.push(readings);
    return this.answer('simulateReadings', undefined);
  }

  simulatePhoneActivity(activity: number, confidence: number) {
    this.phoneActivitiesReceived.push([activity, confidence]);
    return this.answer('simulatePhoneActivity', undefined);
  }

  /** The id of the only request that is open right now. */
  onlyOpenRequestId(): string {
    const ids = [...this.openRequests.keys()];
    if (ids.length !== 1) {
      throw new Error(`expected 1 open request, there are ${ids.length}`);
    }
    return ids[0] as string;
  }

  /** The native side finishes a request with this report. */
  finish(requestId: string, answer: NativeReport): void {
    const open = this.openRequests.get(requestId);
    if (open === undefined) {
      throw new Error(`no open request called ${requestId}`);
    }
    this.openRequests.delete(requestId);
    open.resolve(answer);
  }
}

/** Lets the Promises that are already resolved run their `then`. */
export async function letPromisesSettle(): Promise<void> {
  for (let turn = 0; turn < 5; turn += 1) {
    await Promise.resolve();
  }
}
