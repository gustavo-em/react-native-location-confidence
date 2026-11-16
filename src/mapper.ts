/*
 * Turns what the native side sends into the public shapes of types.ts.
 *
 * The native side (Kotlin, Swift) sends flat objects, and the answers that
 * are "one choice from a list" travel as the engine's numbers. This file is
 * the ONLY place that knows which number means which text, so Android and
 * iOS can never disagree about it.
 *
 * No rule about locations lives here: nothing is computed, only renamed.
 */
import { LocationConfidenceError, NativeModuleError } from './errors';
import type {
  NativeAssessment,
  NativeAuthorization,
  NativeCapabilities,
  NativeSessionResult,
} from './NativeLocationConfidence';
import type {
  AccuracyAuthorization,
  AgeReliability,
  Assessment,
  AssessmentStatus,
  Authorization,
  Capabilities,
  EvidenceRelation,
  FixSource,
  MotionConfidence,
  MotionState,
  PermissionStatus,
  Session,
} from './types';

/**
 * The version of the engine this JavaScript was written for. It is
 * LCE_ABI_VERSION in cpp/include/location_confidence/lce.h. When the app
 * carries native code of another version, the numbers below could mean
 * something else, so the answer is refused instead of misread.
 */
export const ENGINE_VERSION_THIS_CODE_EXPECTS = 2;

/*
 * The engine names each choice with a number (the LCE_* lists of lce.h).
 * The position in each list IS the number: STATUS_TEXTS[1] is 'degraded'.
 */
const STATUS_TEXTS: readonly AssessmentStatus[] = [
  'available',
  'degraded',
  'unavailable',
  'cancelled',
];

const SOURCE_TEXTS: readonly FixSource[] = [
  'fresh',
  'recovered',
  'cached',
  'unavailable',
];

const RELATION_TEXTS: readonly EvidenceRelation[] = [
  'supports_inside',
  'supports_outside',
  'inconclusive',
];

// 0 means "no reading was used": there is no age to talk about.
const AGE_RELIABILITY_TEXTS: readonly (AgeReliability | null)[] = [
  null,
  'monotonic',
  'estimated',
  'unreliable',
];

const PRECISION_TEXTS: readonly AccuracyAuthorization[] = [
  'precise',
  'approximate',
  'unknown',
];

export const MOTION_STATE_TEXTS: readonly MotionState[] = [
  'stationary',
  'walking',
  'running',
  'cycling',
  'vehicle',
  'unknown',
];

export const MOTION_CONFIDENCE_TEXTS: readonly MotionConfidence[] = [
  'high',
  'medium',
  'low',
  'unknown',
];

// 0: the system said nothing. 1: it did not mark the reading. 2: it marked it.
const SIMULATED_LOCATION_ANSWERS: readonly (boolean | null)[] = [
  null,
  false,
  true,
];

const PERMISSION_TEXTS: readonly PermissionStatus[] = [
  'not_determined',
  'denied',
  'restricted',
  'granted',
];

const MONITORING_TEXTS: readonly Session['monitoring'][] = [
  'not_requested',
  'active',
  'blocked',
];

/** The item at position `number`, or an error that names the field. */
function chosenFrom<T>(
  list: readonly T[],
  number: number,
  fieldName: string
): T {
  const numberIsInTheList =
    Number.isInteger(number) && number >= 0 && number < list.length;
  if (!numberIsInTheList) {
    throw new NativeModuleError(
      'native_internal_error',
      `The native side sent an unknown ${fieldName}: ${number}`
    );
  }
  return list[number] as T;
}

/** `text` when it is one of the allowed texts, or an error that names the field. */
function oneOf<T extends string>(
  allowed: readonly T[],
  text: string,
  fieldName: string
): T {
  const found = allowed.find((candidate) => candidate === text);
  if (found === undefined) {
    throw new NativeModuleError(
      'native_internal_error',
      `The native side sent an unknown ${fieldName}: ${text}`
    );
  }
  return found;
}

/** The final report of one evaluation, in its public shape. */
export function toAssessment(
  requestId: string,
  native: NativeAssessment
): Assessment {
  const versionMatches =
    native.transportVersion === ENGINE_VERSION_THIS_CODE_EXPECTS;
  if (!versionMatches) {
    throw new NativeModuleError(
      'native_version_mismatch',
      `The native code is version ${native.transportVersion} and this ` +
        `JavaScript expects ${ENGINE_VERSION_THIS_CODE_EXPECTS}. ` +
        'Rebuild the app after updating react-native-location-confidence.'
    );
  }

  // Without a reading the numbers below are only zeros: they become null,
  // so that nobody mistakes "no position" for the point (0, 0).
  const hasPosition = native.hasPosition;

  return {
    schemaVersion: 1,
    scoreModelVersion: 'heuristic-v1',
    requestId,
    sessionId: native.sessionId,
    status: chosenFrom(STATUS_TEXTS, native.status, 'status'),
    source: chosenFrom(SOURCE_TEXTS, native.source, 'source'),

    measuredPosition: hasPosition
      ? { latitude: native.latitude, longitude: native.longitude }
      : null,
    distanceMeters: hasPosition ? native.distanceMeters : null,
    radiusMeters: native.radiusMeters,
    horizontalAccuracyMeters: hasPosition
      ? native.horizontalAccuracyMeters
      : null,
    effectiveUncertaintyMeters: hasPosition
      ? native.effectiveUncertaintyMeters
      : null,
    locationAgeMs: hasPosition ? native.locationAgeMs : null,
    ageReliability: chosenFrom(
      AGE_RELIABILITY_TEXTS,
      native.ageReliability,
      'ageReliability'
    ),
    permissionPrecision: chosenFrom(
      PRECISION_TEXTS,
      native.permissionPrecision,
      'permissionPrecision'
    ),

    qualityScore: native.qualityScore,
    probabilityWithinRadius: null,
    evidenceRelation: chosenFrom(
      RELATION_TEXTS,
      native.evidenceRelation,
      'evidenceRelation'
    ),

    motion: {
      state: chosenFrom(MOTION_STATE_TEXTS, native.motionState, 'motionState'),
      confidence: chosenFrom(
        MOTION_CONFIDENCE_TEXTS,
        native.motionConfidence,
        'motionConfidence'
      ),
      continuousStationaritySupported: native.continuousStationaritySupported,
      coverageStartAgeMs: native.hasCoverageStart
        ? native.coverageStartAgeMs
        : null,
      gapsDetected: native.gapsDetected,
    },

    scoreBreakdown: native.hasScoreBreakdown
      ? {
          accuracyFactor: native.accuracyFactor,
          freshnessFactor: native.freshnessFactor,
          movementFactor: native.movementFactor,
          consistencyFactor: native.consistencyFactor,
          rawScore: native.rawScore,
          appliedCaps: [...native.appliedCaps],
        }
      : null,

    diagnostics: {
      reasonCodes: [...native.reasonCodes],
      phaseTrace: [...native.phaseTrace],
      initialFixTimedOut: native.initialFixTimedOut,
      recoveryAttempted: native.recoveryAttempted,
      samplesReceived: native.samplesReceived,
      uniqueSamplesRetained: native.uniqueSamplesRetained,
      samplesAnalyzed: native.samplesAnalyzed,
      outliersFlagged: native.outliersFlagged,
      durationMs: native.durationMs,
      fallbackCause: native.fallbackCause,
      simulatedLocationReported: chosenFrom(
        SIMULATED_LOCATION_ANSWERS,
        native.mockSignal,
        'mockSignal'
      ),
    },
  };
}

/**
 * The report of an evaluation that was cancelled before it started: the
 * AbortSignal was already aborted. It is made here, in JavaScript, and the
 * native side is never called, so no sensor is turned on.
 */
export function assessmentCancelledBeforeItStarted(
  requestId: string,
  sessionId: string | null,
  radiusMeters: number
): Assessment {
  return {
    schemaVersion: 1,
    scoreModelVersion: 'heuristic-v1',
    requestId,
    sessionId,
    status: 'cancelled',
    source: 'unavailable',
    measuredPosition: null,
    distanceMeters: null,
    radiusMeters,
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
  };
}

export function toAuthorization(native: NativeAuthorization): Authorization {
  const location = oneOf(PERMISSION_TEXTS, native.location, 'location');
  return {
    location,
    locationGranted: location === 'granted',
    precision: oneOf(PRECISION_TEXTS, native.precision, 'precision'),
    motion: oneOf(
      [...PERMISSION_TEXTS, 'unsupported'] as const,
      native.motion,
      'motion'
    ),
  };
}

export function toCapabilities(native: NativeCapabilities): Capabilities {
  return {
    locationServicesEnabled: native.locationServicesEnabled,
    locationProviderAvailable: native.locationProviderAvailable,
    motionSupported: native.motionSupported,
    mockDetectionSupported: native.mockDetectionSupported,
    simulationEnabled: native.simulationEnabled,
    reasonCodes: [...native.reasonCodes],
  };
}

export function toSession(native: NativeSessionResult): Session {
  // An empty id means the engine refused: too many sessions are open at the
  // same time. Handing back a session that cannot be used would only move
  // the problem to the next call, so this one fails here, with the reason.
  const wasOpened = native.id !== '';
  if (!wasOpened) {
    throw new LocationConfidenceError(
      'busy',
      'The session could not be opened: too many sessions are open. ' +
        'Call stopSession() on the ones that are no longer needed.'
    );
  }

  return {
    id: native.id,
    monitoring: oneOf(MONITORING_TEXTS, native.monitoring, 'monitoring'),
    reasonCodes: [...native.reasonCodes],
  };
}
