import { TurboModuleRegistry, type TurboModule } from 'react-native';

/*
 * The contract between JavaScript and the native code (Kotlin on Android,
 * Swift on iOS).
 *
 * React Native's Codegen reads this file when the app is built and generates
 * the glue on both platforms. Only simple shapes cross here: numbers,
 * booleans, strings, arrays and flat objects. The rich, public types of the
 * library are in types.ts, and mapper.ts turns one into the other.
 *
 * Nobody outside this package should call this module directly: use the
 * `LocationConfidence` object exported by index.ts.
 */

export type NativeCapabilities = {
  /** The "Location" switch of the phone. */
  locationServicesEnabled: boolean;
  /** Android: Google Play services are present. iOS: always true. */
  locationProviderAvailable: boolean;
  /** The phone can report its activity (walking, standing still...). */
  motionSupported: boolean;
  /** The system can mark a reading as simulated. */
  mockDetectionSupported: boolean;
  /** The app replaced the GPS with readings of its own. */
  simulationEnabled: boolean;
  /** Version of the engine that was compiled into the app. */
  coreAbiVersion: number;
  reasonCodes: string[];
};

export type NativeAuthorization = {
  /** 'not_determined' | 'denied' | 'restricted' | 'granted' */
  location: string;
  /** 'precise' | 'approximate' | 'unknown' */
  precision: string;
  /** 'not_determined' | 'denied' | 'restricted' | 'granted' | 'unsupported' */
  motion: string;
};

export type NativeSessionOptions = {
  keepCollectingReadings: boolean;
  timeBetweenReadingsMs: number;
  longestCollectingTimeMs: number;
};

export type NativeSessionResult = {
  /** '' when the session could not be opened. */
  id: string;
  /** 'not_requested' | 'active' | 'blocked' */
  monitoring: string;
  reasonCodes: string[];
};

/** Every field is filled in by the JavaScript side: no optional values. */
export type NativeEvaluateRequest = {
  sessionId: string | null;
  latitude: number;
  longitude: number;
  radiusMeters: number;
  totalDeadlineMs: number;
  initialFixBudgetMs: number;
  recoveryWindowMs: number;
  maxFreshAgeMs: number;
  maxFallbackAgeMs: number;
  maxHorizontalAccuracyMeters: number;
  enableMotionAssistance: boolean;
  allowHistoricalFallback: boolean;
  maxPlausibleSpeedMps: number;
  /** true: a reading marked as simulated scores 0. false: it is only reported. */
  zeroScoreForMockLocation: boolean;
};

/**
 * The final report, flat. The fields that are a choice from a list (status,
 * source...) travel as the engine's numbers; mapper.ts turns them into the
 * public texts, in one place for Android and iOS.
 */
export type NativeAssessment = {
  transportVersion: number;
  sessionId: string | null;
  status: number;
  source: number;
  hasPosition: boolean;
  latitude: number;
  longitude: number;
  distanceMeters: number;
  horizontalAccuracyMeters: number;
  effectiveUncertaintyMeters: number;
  locationAgeMs: number;
  mockSignal: number;
  radiusMeters: number;
  ageReliability: number;
  permissionPrecision: number;
  evidenceRelation: number;
  qualityScore: number;
  hasScoreBreakdown: boolean;
  accuracyFactor: number;
  freshnessFactor: number;
  movementFactor: number;
  consistencyFactor: number;
  rawScore: number;
  appliedCaps: string[];
  motionState: number;
  motionConfidence: number;
  continuousStationaritySupported: boolean;
  hasCoverageStart: boolean;
  coverageStartAgeMs: number;
  gapsDetected: boolean;
  reasonCodes: string[];
  phaseTrace: string[];
  initialFixTimedOut: boolean;
  recoveryAttempted: boolean;
  fallbackCause: string | null;
  samplesReceived: number;
  uniqueSamplesRetained: number;
  samplesAnalyzed: number;
  outliersFlagged: number;
  durationMs: number;
};

/** A reading the app makes up, for a demo or a test. */
export type NativeSimulatedReading = {
  latitude: number;
  longitude: number;
  accuracyMeters: number;
  /** How old the reading already is when it is handed over. */
  ageMs: number;
  speedMps: number | null;
  speedAccuracyMps: number | null;
  /** As if the system had marked it as simulated. */
  flaggedAsMock: boolean;
  /** As if it had been taken with an approximate permission. */
  approximate: boolean;
};

export interface Spec extends TurboModule {
  getCapabilities(): Promise<NativeCapabilities>;
  getAuthorization(): Promise<NativeAuthorization>;
  requestLocationPermission(): Promise<NativeAuthorization>;
  requestMotionPermission(): Promise<NativeAuthorization>;

  startSession(options: NativeSessionOptions): Promise<NativeSessionResult>;
  clearHistory(sessionId: string): Promise<void>;
  stopSession(sessionId: string): Promise<void>;

  /**
   * `requestId` is chosen by JavaScript before the call, so that
   * cancelRequest() can name an evaluation whose Promise has not come back.
   */
  evaluate(
    requestId: string,
    request: NativeEvaluateRequest
  ): Promise<NativeAssessment>;
  cancelRequest(requestId: string): void;

  // Simulation: the app feeds readings of its own instead of the GPS.
  setSimulationEnabled(enabled: boolean): Promise<void>;
  simulateReadings(readings: NativeSimulatedReading[]): Promise<void>;
  /**
   * activity: 0 standing still, 1 walking, 2 running, 3 cycling, 4 in vehicle,
   * -1 "the phone stopped reporting". confidence: 0 high, 1 medium, 2 low.
   */
  simulatePhoneActivity(activity: number, confidence: number): Promise<void>;
}

/*
 * `get` and not `getEnforcing`: when the native part is missing (the app was
 * not rebuilt, or it runs in Expo Go) this is null, and index.tsx turns that
 * into an error that says what to do, at the first call. Importing the
 * library never throws.
 */
export default TurboModuleRegistry.get<Spec>('LocationConfidence');
