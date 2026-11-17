/*
 * The public types of the library.
 *
 * Every measure carries its unit in the name: Meters, Ms (milliseconds),
 * Mps (meters per second).
 */

/** A point on the map, in WGS-84 degrees (the usual GPS coordinates). */
export type Coordinates = Readonly<{
  /** -90 (south pole) to +90 (north pole). */
  latitude: number;
  /** -180 to +180. */
  longitude: number;
}>;

export type MotionState =
  'stationary' | 'walking' | 'running' | 'cycling' | 'vehicle' | 'unknown';

export type MotionConfidence = 'high' | 'medium' | 'low' | 'unknown';

/** Whether the user allowed the precise location or only an approximate one. */
export type AccuracyAuthorization = 'precise' | 'approximate' | 'unknown';

/**
 * Where the reading used in the answer came from.
 *
 * - `fresh`: obtained by this request, in its first attempt.
 * - `recovered`: obtained by this request, during the extra time.
 * - `cached`: it was already remembered, or the system handed over an old one.
 * - `unavailable`: there was no usable reading.
 */
export type FixSource = 'fresh' | 'recovered' | 'cached' | 'unavailable';

/**
 * How good the answer is as a whole.
 *
 * - `available`: a reading obtained just now, precise enough, nothing suspicious.
 * - `degraded`: there is a reading, but something about it is weak.
 * - `unavailable`: there is no reading that can be used.
 * - `cancelled`: the request was stopped before it finished.
 */
export type ReportStatus =
  'available' | 'degraded' | 'unavailable' | 'cancelled';

/**
 * What the evidence says about the delivery radius.
 *
 * `supports_inside` means the evidence is compatible with being there. It is
 * NOT proof that a person was at the address and it is NOT an approval of
 * the delivery. That decision belongs to your app.
 */
export type EvidenceRelation =
  'supports_inside' | 'supports_outside' | 'inconclusive';

/** How much the age of the reading can be trusted. */
export type AgeReliability = 'monotonic' | 'estimated' | 'unreliable';

/** What to do with a reading the system marked as simulated. */
export type MockLocationPolicy = 'zero_score' | 'flag_only';

/** The final report of one request. */
export interface ProximityReport {
  schemaVersion: 1;
  scoreModelVersion: 'heuristic-v1';
  requestId: string;
  sessionId: string | null;
  status: ReportStatus;
  source: FixSource;

  /**
   * The reading that was used, exactly as the phone measured it. The library
   * never predicts or invents a position. `null` when there is none.
   */
  measuredPosition: Coordinates | null;
  /** From the reading to the destination, in a straight line. */
  distanceMeters: number | null;
  radiusMeters: number;
  /** The phone's own estimate of the error of the reading. */
  horizontalAccuracyMeters: number | null;
  /**
   * How far the phone could really be from the reading right now: its error
   * plus the distance it could have moved since. A cautious bound made of
   * design guesses, not a statistical interval.
   */
  effectiveUncertaintyMeters: number | null;
  locationAgeMs: number | null;
  ageReliability: AgeReliability | null;
  permissionPrecision: AccuracyAuthorization;

  /**
   * An INDEX from 0 to 100 that says how good the evidence is. It is NOT a
   * percentage and NOT a probability: 78 does not mean "78% sure". Never
   * show it with a % sign.
   */
  qualityScore: number;
  /** Always null in this version: the library does not estimate probabilities. */
  probabilityWithinRadius: null;
  evidenceRelation: EvidenceRelation;

  /** What is known about the activity of the PHONE (not of the person). */
  motion: {
    state: MotionState;
    confidence: MotionConfidence;
    /** The phone stood still during the whole time since the reading. */
    continuousStationaritySupported: boolean;
    /** For how long, going back from now, the activity is known with no break. */
    coverageStartAgeMs: number | null;
    /** Part of the time since the reading is known and part is not. */
    gapsDetected: boolean;
  };

  scoreBreakdown: {
    accuracyFactor: number;
    freshnessFactor: number;
    movementFactor: number;
    consistencyFactor: number;
    /** The score before the limits. */
    rawScore: number;
    /** The limits that applied, for example "single_fix". */
    appliedCaps: string[];
  } | null;

  diagnostics: {
    /** Short fixed labels that explain the answer. Never coordinates. */
    reasonCodes: string[];
    /** The steps the request went through, in order. */
    phaseTrace: string[];
    initialFixTimedOut: boolean;
    recoveryAttempted: boolean;
    samplesReceived: number;
    uniqueSamplesRetained: number;
    samplesAnalyzed: number;
    outliersFlagged: number;
    durationMs: number;
    fallbackCause: string | null;
    /**
     * true: the system marked the reading as simulated. false: it did not,
     * which proves nothing. null: the system said nothing, or there was no
     * reading.
     */
    simulatedLocationReported: boolean | null;
  };
}

export interface SessionOptions {
  /**
   * - `requests_only` (default): the session only remembers the readings
   *   obtained by its requests. Opening it does not turn the GPS on.
   * - `foreground_monitoring`: the session keeps collecting readings while it
   *   is open and the app is visible, so that a request finds a history
   *   already there. It needs the location permission and uses more battery.
   */
  collectionMode?: 'requests_only' | 'foreground_monitoring';
  /** One reading is kept every this many milliseconds. Default 5000. */
  monitoringIntervalMs?: number;
  /** The collecting stops by itself after this long. Default 900000 (15 minutes). */
  maxMonitoringDurationMs?: number;
}

export interface Session {
  id: string;
  /**
   * - `not_requested`: the session does not collect.
   * - `active`: it is collecting.
   * - `blocked`: collecting was asked for but could not start; `reasonCodes`
   *   says why. The session is still open and can be used for requests.
   */
  monitoring: 'not_requested' | 'active' | 'blocked';
  reasonCodes: string[];
}

export interface ProximityOptions {
  /** The whole request never takes longer than this. Default 12000. */
  totalDeadlineMs?: number;
  /** How long the first attempt may wait. Default 4500, never above the total. */
  initialFixBudgetMs?: number;
  /** How long the extra time may last. Default 7000. 0 means no extra time. */
  recoveryWindowMs?: number;
  /** A reading up to this old counts as obtained now. Default 10000. */
  maxFreshAgeMs?: number;
  /** A reading older than this is never used. Default 60000. */
  maxFallbackAgeMs?: number;
  /** Above this error the answer is only "degraded". Default 50. */
  maxHorizontalAccuracyMeters?: number;
  /** Use what the phone reports about its activity, when it can. Default true. */
  enableMotionAssistance?: boolean;
  /** Allow an answer taken from readings remembered by the session. Default true. */
  allowHistoricalFallback?: boolean;
  /** The fastest speed a courier is assumed to move. Default 45 (162 km/h). */
  maxPlausibleSpeedMps?: number;
  /** Default 'zero_score'. */
  mockLocationPolicy?: MockLocationPolicy;
}

export interface ProximityRequest {
  /** Leave out for a request that works alone and remembers nothing. */
  sessionId?: string;
  destination: Coordinates;
  radiusMeters: number;
  options?: ProximityOptions;
  /** Aborting it cancels the request: the answer has status 'cancelled'. */
  signal?: AbortSignal;
}

export type PermissionStatus =
  'not_determined' | 'denied' | 'restricted' | 'granted';

export interface Authorization {
  location: PermissionStatus;
  /** Shortcut for `location === 'granted'`. */
  locationGranted: boolean;
  precision: AccuracyAuthorization;
  motion: PermissionStatus | 'unsupported';
}

export interface Capabilities {
  locationServicesEnabled: boolean;
  /** Android: Google Play services are present. iOS: always true. */
  locationProviderAvailable: boolean;
  motionSupported: boolean;
  mockDetectionSupported: boolean;
  simulationEnabled: boolean;
  reasonCodes: string[];
}

/** A reading your app makes up, for a demo or a test. */
export interface SimulatedReading {
  latitude: number;
  longitude: number;
  /** The GPS error to pretend. */
  accuracyMeters: number;
  /** How old the reading already is when it is handed over. Default 0. */
  ageMs?: number;
  speedMps?: number;
  speedAccuracyMps?: number;
  /** As if the system had marked it as simulated. Default false. */
  flaggedAsMock?: boolean;
  /** As if it had been taken with an approximate permission. Default false. */
  approximate?: boolean;
}

/**
 * Replaces the GPS with readings your app provides. For demos and tests.
 *
 * While it is enabled the real GPS is never used and no permission is
 * needed. Every report then carries the reason code
 * `simulated_location_source`, so a simulated answer can never be mistaken
 * for a real one.
 */
export interface SimulationApi {
  enable(): Promise<void>;
  disable(): Promise<void>;
  /** Hands readings over as if the GPS had just delivered them. */
  pushReadings(readings: SimulatedReading[]): Promise<void>;
  /**
   * Says what the phone "is doing". `null` means the phone stopped
   * reporting its activity.
   */
  setPhoneActivity(
    state: Exclude<MotionState, 'unknown'> | null,
    confidence?: Exclude<MotionConfidence, 'unknown'>
  ): Promise<void>;
}

export interface LocationConfidenceApi {
  /** What the phone can do. Never shows a dialog and never turns the GPS on. */
  getCapabilities(): Promise<Capabilities>;
  /** The permissions as they are. Never shows a dialog. */
  getAuthorization(): Promise<Authorization>;
  /** Shows the system's permission dialog. The only call that asks the user. */
  requestLocationPermission(): Promise<Authorization>;
  requestMotionPermission(): Promise<Authorization>;

  startSession(options?: SessionOptions): Promise<Session>;
  /** Forgets the readings of a session; the session stays open. */
  clearHistory(sessionId: string): Promise<void>;
  /** Closes a session and forgets its readings. Safe to call twice. */
  stopSession(sessionId: string): Promise<void>;

  getProximityReport(request: ProximityRequest): Promise<ProximityReport>;

  simulation: SimulationApi;
}
