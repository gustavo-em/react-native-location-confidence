import { describe, expect, it } from '@jest/globals';
import { LocationConfidenceError, NativeModuleError } from '../errors';
import {
  ENGINE_VERSION_THIS_CODE_EXPECTS,
  reportCancelledBeforeItStarted,
  toProximityReport,
  toAuthorization,
  toCapabilities,
  toSession,
} from '../mapper';
import type { ProximityReport } from '../types';
import { cancelledNativeReport, goldenNativeReport } from './FakeNative';

/*
 * Checks made by the compiler, not by Jest: if one of them stops being
 * true, `tsc` fails.
 */
type MustBeTrue<T extends true> = T;

// SCR-005: the type itself says there is never a probability.
export type ProbabilityIsAlwaysNull = MustBeTrue<
  ProximityReport['probabilityWithinRadius'] extends null ? true : false
>;

// SCR-030: nothing in the report claims to prove who or where someone is.
export type NoFieldClaimsProof = MustBeTrue<
  Extract<
    keyof ProximityReport | keyof ProximityReport['diagnostics'],
    'isTrustedIdentity' | 'proofOfPresence' | 'approved' | 'isInside'
  > extends never
    ? true
    : false
>;

function codeThrownBy(action: () => unknown): string {
  try {
    action();
  } catch (error) {
    if (error instanceof LocationConfidenceError) {
      return error.code;
    }
    throw error;
  }
  throw new Error('nothing was thrown');
}

describe('the report of a request', () => {
  it('golden scenario: every field crosses unchanged', () => {
    const native = goldenNativeReport();

    expect(toProximityReport('lc-1', native)).toEqual({
      schemaVersion: 1,
      scoreModelVersion: 'heuristic-v1',
      requestId: 'lc-1',
      sessionId: null,
      status: 'available',
      source: 'fresh',
      measuredPosition: { latitude: native.latitude, longitude: 0 },
      distanceMeters: 20,
      radiusMeters: 100,
      horizontalAccuracyMeters: 8,
      effectiveUncertaintyMeters: 53,
      locationAgeMs: 1000,
      ageReliability: 'monotonic',
      permissionPrecision: 'precise',
      qualityScore: 75,
      probabilityWithinRadius: null,
      evidenceRelation: 'supports_inside',
      motion: {
        state: 'unknown',
        confidence: 'unknown',
        continuousStationaritySupported: false,
        coverageStartAgeMs: null,
        gapsDetected: false,
      },
      scoreBreakdown: {
        accuracyFactor: 0.92,
        freshnessFactor: 59 / 60,
        movementFactor: 0.55,
        consistencyFactor: 0.6,
        rawScore: 81,
        appliedCaps: ['single_fix'],
      },
      diagnostics: {
        reasonCodes: ['uncertainty_model_heuristic'],
        phaseTrace: ['started', 'first_attempt', 'finished'],
        initialFixTimedOut: false,
        recoveryAttempted: false,
        samplesReceived: 1,
        uniqueSamplesRetained: 1,
        samplesAnalyzed: 1,
        outliersFlagged: 0,
        durationMs: 2000,
        fallbackCause: null,
        simulatedLocationReported: false,
      },
    });
  });

  it('without a reading the position fields are null, never zero', () => {
    const report = toProximityReport('lc-2', cancelledNativeReport());

    expect(report.status).toBe('cancelled');
    expect(report.source).toBe('unavailable');
    expect(report.measuredPosition).toBeNull();
    expect(report.distanceMeters).toBeNull();
    expect(report.horizontalAccuracyMeters).toBeNull();
    expect(report.effectiveUncertaintyMeters).toBeNull();
    expect(report.locationAgeMs).toBeNull();
    expect(report.ageReliability).toBeNull();
    expect(report.scoreBreakdown).toBeNull();
    expect(report.diagnostics.simulatedLocationReported).toBeNull();
    expect(report.qualityScore).toBe(0);
    expect(report.radiusMeters).toBe(100);
  });

  it('a reading at the point (0, 0) is a position, not a missing one', () => {
    const native = { ...goldenNativeReport(), latitude: 0, longitude: 0 };
    expect(toProximityReport('lc-3', native).measuredPosition).toEqual({
      latitude: 0,
      longitude: 0,
    });
  });

  it('SCR-005 the probability is null whatever the answer is', () => {
    for (const native of [goldenNativeReport(), cancelledNativeReport()]) {
      expect(
        toProximityReport('lc-4', native).probabilityWithinRadius
      ).toBeNull();
    }
    expect(
      reportCancelledBeforeItStarted('lc-4', null, 100).probabilityWithinRadius
    ).toBeNull();
  });

  it('SCR-030 no field says the location is authentic', () => {
    const report = toProximityReport('lc-5', goldenNativeReport());
    const everyFieldName = JSON.stringify(report).toLowerCase();

    for (const forbidden of ['trusted', 'proof', 'authentic', 'approved']) {
      expect(everyFieldName).not.toContain(forbidden);
    }
    // "The system did not mark it" is reported as false, and that is all.
    expect(report.diagnostics.simulatedLocationReported).toBe(false);
  });

  it('turns every number of the engine into its public text', () => {
    const base = goldenNativeReport();
    const textsOf = <K extends keyof ProximityReport>(
      field:
        | 'status'
        | 'source'
        | 'evidenceRelation'
        | 'ageReliability'
        | 'permissionPrecision',
      howMany: number,
      publicField: K
    ) =>
      Array.from(
        { length: howMany },
        (_, number) =>
          toProximityReport('lc-6', { ...base, [field]: number })[publicField]
      );

    expect(textsOf('status', 4, 'status')).toEqual([
      'available',
      'degraded',
      'unavailable',
      'cancelled',
    ]);
    expect(textsOf('source', 4, 'source')).toEqual([
      'fresh',
      'recovered',
      'cached',
      'unavailable',
    ]);
    expect(textsOf('evidenceRelation', 3, 'evidenceRelation')).toEqual([
      'supports_inside',
      'supports_outside',
      'inconclusive',
    ]);
    expect(textsOf('ageReliability', 4, 'ageReliability')).toEqual([
      null,
      'monotonic',
      'estimated',
      'unreliable',
    ]);
    expect(textsOf('permissionPrecision', 3, 'permissionPrecision')).toEqual([
      'precise',
      'approximate',
      'unknown',
    ]);
  });

  it('turns the activity of the phone into its public texts', () => {
    const base = goldenNativeReport();
    const states = [0, 1, 2, 3, 4, 5].map(
      (motionState) =>
        toProximityReport('lc-7', { ...base, motionState }).motion.state
    );
    expect(states).toEqual([
      'stationary',
      'walking',
      'running',
      'cycling',
      'vehicle',
      'unknown',
    ]);

    const confidences = [0, 1, 2, 3].map(
      (motionConfidence) =>
        toProximityReport('lc-7', { ...base, motionConfidence }).motion
          .confidence
    );
    expect(confidences).toEqual(['high', 'medium', 'low', 'unknown']);

    const standingStill = toProximityReport('lc-7', {
      ...base,
      motionState: 0,
      motionConfidence: 0,
      continuousStationaritySupported: true,
      hasCoverageStart: true,
      coverageStartAgeMs: 42000,
      gapsDetected: true,
    }).motion;
    expect(standingStill).toEqual({
      state: 'stationary',
      confidence: 'high',
      continuousStationaritySupported: true,
      coverageStartAgeMs: 42000,
      gapsDetected: true,
    });
  });

  it('reports what the system said about a simulated location', () => {
    const base = goldenNativeReport();
    const answers = [0, 1, 2].map(
      (mockSignal) =>
        toProximityReport('lc-8', { ...base, mockSignal }).diagnostics
          .simulatedLocationReported
    );
    expect(answers).toEqual([null, false, true]);
  });

  it('keeps the session id and the cause of the extra time', () => {
    const report = toProximityReport('lc-9', {
      ...goldenNativeReport(),
      sessionId: '4',
      source: 1,
      initialFixTimedOut: true,
      recoveryAttempted: true,
      fallbackCause: 'initial_fix_timeout',
      outliersFlagged: 2,
    });
    expect(report.sessionId).toBe('4');
    expect(report.source).toBe('recovered');
    expect(report.diagnostics.initialFixTimedOut).toBe(true);
    expect(report.diagnostics.recoveryAttempted).toBe(true);
    expect(report.diagnostics.fallbackCause).toBe('initial_fix_timeout');
    expect(report.diagnostics.outliersFlagged).toBe(2);
  });

  it('copies the lists, so changing the report never changes the source', () => {
    const native = goldenNativeReport();
    const report = toProximityReport('lc-10', native);
    report.diagnostics.reasonCodes.push('changed_by_the_app');
    report.scoreBreakdown?.appliedCaps.push('changed_by_the_app');
    expect(native.reasonCodes).toEqual(['uncertainty_model_heuristic']);
    expect(native.appliedCaps).toEqual(['single_fix']);
  });

  it('refuses a number the engine does not have', () => {
    const base = goldenNativeReport();
    const broken = [
      { ...base, status: 4 },
      { ...base, status: -1 },
      { ...base, status: 1.5 },
      { ...base, source: 4 },
      { ...base, evidenceRelation: 3 },
      { ...base, ageReliability: 4 },
      { ...base, permissionPrecision: 3 },
      { ...base, motionState: 6 },
      { ...base, motionConfidence: 4 },
      { ...base, mockSignal: 3 },
    ];
    for (const native of broken) {
      expect(codeThrownBy(() => toProximityReport('lc-11', native))).toBe(
        'native_internal_error'
      );
    }
  });

  it('refuses a report made by another version of the engine', () => {
    expect(ENGINE_VERSION_THIS_CODE_EXPECTS).toBe(2);
    for (const transportVersion of [0, 1, 3]) {
      const native = { ...goldenNativeReport(), transportVersion };
      expect(codeThrownBy(() => toProximityReport('lc-12', native))).toBe(
        'native_version_mismatch'
      );
    }
  });
});

describe('the report of a request cancelled before it started', () => {
  it('has no position, a score of zero and says why', () => {
    expect(reportCancelledBeforeItStarted('lc-20', '2', 80)).toEqual({
      schemaVersion: 1,
      scoreModelVersion: 'heuristic-v1',
      requestId: 'lc-20',
      sessionId: '2',
      status: 'cancelled',
      source: 'unavailable',
      measuredPosition: null,
      distanceMeters: null,
      radiusMeters: 80,
      horizontalAccuracyMeters: null,
      effectiveUncertaintyMeters: null,
      locationAgeMs: null,
      ageReliability: null,
      permissionPrecision: 'unknown',
      qualityScore: 0,
      probabilityWithinRadius: null,
      evidenceRelation: 'inconclusive',
      motion: {
        state: 'unknown',
        confidence: 'unknown',
        continuousStationaritySupported: false,
        coverageStartAgeMs: null,
        gapsDetected: false,
      },
      scoreBreakdown: null,
      diagnostics: {
        reasonCodes: ['request_cancelled'],
        phaseTrace: ['started', 'finished'],
        initialFixTimedOut: false,
        recoveryAttempted: false,
        samplesReceived: 0,
        uniqueSamplesRetained: 0,
        samplesAnalyzed: 0,
        outliersFlagged: 0,
        durationMs: 0,
        fallbackCause: null,
        simulatedLocationReported: null,
      },
    });
  });
});

describe('permissions, capabilities and sessions', () => {
  it('says in one word whether the location is allowed', () => {
    expect(
      toAuthorization({
        location: 'granted',
        precision: 'approximate',
        motion: 'unsupported',
      })
    ).toEqual({
      location: 'granted',
      locationGranted: true,
      precision: 'approximate',
      motion: 'unsupported',
    });

    for (const location of ['not_determined', 'denied', 'restricted']) {
      const authorization = toAuthorization({
        location,
        precision: 'unknown',
        motion: 'not_determined',
      });
      expect(authorization.location).toBe(location);
      expect(authorization.locationGranted).toBe(false);
    }
  });

  it('refuses permission texts it does not know', () => {
    const good = {
      location: 'granted',
      precision: 'precise',
      motion: 'denied',
    };
    for (const broken of [
      { ...good, location: 'always' },
      { ...good, precision: 'exact' },
      { ...good, motion: 'maybe' },
      { ...good, location: 'unsupported' },
    ]) {
      expect(codeThrownBy(() => toAuthorization(broken))).toBe(
        'native_internal_error'
      );
    }
  });

  it('passes the capabilities on, without the version of the engine', () => {
    expect(
      toCapabilities({
        locationServicesEnabled: true,
        locationProviderAvailable: false,
        motionSupported: false,
        mockDetectionSupported: true,
        simulationEnabled: true,
        coreAbiVersion: 2,
        reasonCodes: ['play_services_unavailable'],
      })
    ).toEqual({
      locationServicesEnabled: true,
      locationProviderAvailable: false,
      motionSupported: false,
      mockDetectionSupported: true,
      simulationEnabled: true,
      reasonCodes: ['play_services_unavailable'],
    });
  });

  it('SES-003 a session that could not collect says so, and stays usable', () => {
    expect(
      toSession({
        id: '2',
        monitoring: 'blocked',
        reasonCodes: ['permission_denied'],
      })
    ).toEqual({
      id: '2',
      monitoring: 'blocked',
      reasonCodes: ['permission_denied'],
    });
    expect(
      toSession({ id: '3', monitoring: 'active', reasonCodes: [] }).monitoring
    ).toBe('active');
  });

  it('a session the engine refused is an error, not a session with no id', () => {
    const refused = {
      id: '',
      monitoring: 'not_requested',
      reasonCodes: ['busy'],
    };
    expect(codeThrownBy(() => toSession(refused))).toBe('busy');
    expect(() => toSession(refused)).toThrow(LocationConfidenceError);
    expect(() => toSession(refused)).not.toThrow(NativeModuleError);
  });

  it('refuses a monitoring text it does not know', () => {
    expect(
      codeThrownBy(() =>
        toSession({ id: '1', monitoring: 'stopped', reasonCodes: [] })
      )
    ).toBe('native_internal_error');
  });
});
