/*
 * Checks what the app passed in and fills in the defaults.
 *
 * Everything here runs in JavaScript, BEFORE anything native is called. A
 * request that makes no sense throws InvalidArgumentError and the GPS is
 * never turned on for it.
 *
 * No geography and no scoring happens in this file: it only looks at
 * ranges and types. The rules about locations live in the C++ engine.
 */
import { InvalidArgumentError } from './errors';
import type {
  NativeEvaluateRequest,
  NativeSessionOptions,
  NativeSimulatedReading,
} from './NativeLocationConfidence';
import type {
  EvaluateRequest,
  SessionOptions,
  SimulatedReading,
} from './types';

/** The limits and defaults of every option, in one place. */
export const LIMITS = {
  radiusMeters: { min: 0, max: 100000 },
  totalDeadlineMs: { min: 1000, max: 60000, default: 12000 },
  initialFixBudgetMs: { min: 0, default: 4500 },
  recoveryWindowMs: { min: 0, max: 20000, default: 7000 },
  maxFreshAgeMs: { min: 0, max: 120000, default: 10000 },
  maxFallbackAgeMs: { min: 0, max: 120000, default: 60000 },
  maxHorizontalAccuracyMeters: { default: 50 },
  maxPlausibleSpeedMps: { default: 45 },
  monitoringIntervalMs: { min: 1000, max: 60000, default: 5000 },
  maxMonitoringDurationMs: { min: 1000, max: 3600000, default: 900000 },
} as const;

function isAFiniteNumber(value: unknown): value is number {
  return typeof value === 'number' && Number.isFinite(value);
}

/** A finite number, or the error that names the argument. */
function requireNumber(argument: string, value: unknown): number {
  if (!isAFiniteNumber(value)) {
    throw new InvalidArgumentError(argument, 'must be a finite number');
  }
  return value;
}

/** A whole number inside a range (the limits included). */
function requireWholeNumberBetween(
  argument: string,
  value: unknown,
  min: number,
  max: number
): number {
  const number = requireNumber(argument, value);
  if (!Number.isInteger(number)) {
    throw new InvalidArgumentError(argument, 'must be a whole number');
  }
  if (number < min || number > max) {
    throw new InvalidArgumentError(
      argument,
      `must be between ${min} and ${max}`
    );
  }
  return number;
}

function requirePositiveNumber(argument: string, value: unknown): number {
  const number = requireNumber(argument, value);
  if (number <= 0) {
    throw new InvalidArgumentError(argument, 'must be greater than 0');
  }
  return number;
}

function requireBoolean(argument: string, value: unknown): boolean {
  if (typeof value !== 'boolean') {
    throw new InvalidArgumentError(argument, 'must be true or false');
  }
  return value;
}

function requireObject(argument: string, value: unknown): void {
  if (typeof value !== 'object' || value === null) {
    throw new InvalidArgumentError(argument, 'must be an object');
  }
}

/** Computers can store "negative zero". It means 0; this makes it so. */
function withoutNegativeZero(value: number): number {
  return value === 0 ? 0 : value;
}

function requireLatitude(argument: string, value: unknown): number {
  const latitude = requireNumber(argument, value);
  if (latitude < -90 || latitude > 90) {
    throw new InvalidArgumentError(argument, 'must be between -90 and 90');
  }
  return withoutNegativeZero(latitude);
}

function requireLongitude(argument: string, value: unknown): number {
  const longitude = requireNumber(argument, value);
  if (longitude < -180 || longitude > 180) {
    throw new InvalidArgumentError(argument, 'must be between -180 and 180');
  }
  return withoutNegativeZero(longitude);
}

/** Session ids are short texts made of digits, handed out by the library. */
export function requireSessionId(argument: string, value: unknown): string {
  if (typeof value !== 'string' || value.length === 0) {
    throw new InvalidArgumentError(argument, 'must be a non-empty string');
  }
  return value;
}

/**
 * Checks the options of a session and fills in the defaults.
 * The answer has every field the native side expects.
 */
export function checkSessionOptions(
  options: SessionOptions | undefined
): NativeSessionOptions {
  if (options !== undefined) {
    requireObject('options', options);
  }
  const given = options ?? {};

  const collectionMode = given.collectionMode ?? 'evaluations_only';
  const modeIsKnown =
    collectionMode === 'evaluations_only' ||
    collectionMode === 'foreground_monitoring';
  if (!modeIsKnown) {
    throw new InvalidArgumentError(
      'options.collectionMode',
      "must be 'evaluations_only' or 'foreground_monitoring'"
    );
  }

  const timeBetweenReadingsMs = requireWholeNumberBetween(
    'options.monitoringIntervalMs',
    given.monitoringIntervalMs ?? LIMITS.monitoringIntervalMs.default,
    LIMITS.monitoringIntervalMs.min,
    LIMITS.monitoringIntervalMs.max
  );

  // Collecting never goes on for longer than one hour in a row: a longer
  // wish is shortened to that, so a forgotten session cannot drain the
  // battery for a whole shift.
  const wantedLongestCollectingTimeMs = requireWholeNumberBetween(
    'options.maxMonitoringDurationMs',
    given.maxMonitoringDurationMs ?? LIMITS.maxMonitoringDurationMs.default,
    LIMITS.maxMonitoringDurationMs.min,
    Number.MAX_SAFE_INTEGER
  );
  const longestCollectingTimeMs = Math.min(
    wantedLongestCollectingTimeMs,
    LIMITS.maxMonitoringDurationMs.max
  );

  return {
    keepCollectingReadings: collectionMode === 'foreground_monitoring',
    timeBetweenReadingsMs,
    longestCollectingTimeMs,
  };
}

/**
 * Checks an evaluation request and fills in the defaults.
 * The answer has every field the native side expects.
 */
export function checkEvaluateRequest(
  request: EvaluateRequest
): NativeEvaluateRequest {
  requireObject('request', request);
  requireObject('request.destination', request.destination);

  const latitude = requireLatitude(
    'destination.latitude',
    request.destination.latitude
  );
  const longitude = requireLongitude(
    'destination.longitude',
    request.destination.longitude
  );

  const radiusMeters = requireNumber('radiusMeters', request.radiusMeters);
  const radiusIsInRange =
    radiusMeters > LIMITS.radiusMeters.min &&
    radiusMeters <= LIMITS.radiusMeters.max;
  if (!radiusIsInRange) {
    throw new InvalidArgumentError(
      'radiusMeters',
      `must be greater than 0 and at most ${LIMITS.radiusMeters.max}`
    );
  }

  const sessionId =
    request.sessionId === undefined
      ? null
      : requireSessionId('sessionId', request.sessionId);

  if (request.options !== undefined) {
    requireObject('options', request.options);
  }
  const options = request.options ?? {};

  const totalDeadlineMs = requireWholeNumberBetween(
    'options.totalDeadlineMs',
    options.totalDeadlineMs ?? LIMITS.totalDeadlineMs.default,
    LIMITS.totalDeadlineMs.min,
    LIMITS.totalDeadlineMs.max
  );

  // The first attempt can never be longer than the whole evaluation.
  const wantedInitialFixBudgetMs = requireWholeNumberBetween(
    'options.initialFixBudgetMs',
    options.initialFixBudgetMs ?? LIMITS.initialFixBudgetMs.default,
    LIMITS.initialFixBudgetMs.min,
    LIMITS.totalDeadlineMs.max
  );
  const initialFixBudgetMs = Math.min(
    wantedInitialFixBudgetMs,
    totalDeadlineMs
  );

  const recoveryWindowMs = requireWholeNumberBetween(
    'options.recoveryWindowMs',
    options.recoveryWindowMs ?? LIMITS.recoveryWindowMs.default,
    LIMITS.recoveryWindowMs.min,
    LIMITS.recoveryWindowMs.max
  );

  const maxFreshAgeMs = requireWholeNumberBetween(
    'options.maxFreshAgeMs',
    options.maxFreshAgeMs ?? LIMITS.maxFreshAgeMs.default,
    LIMITS.maxFreshAgeMs.min,
    LIMITS.maxFreshAgeMs.max
  );

  const maxFallbackAgeMs = requireWholeNumberBetween(
    'options.maxFallbackAgeMs',
    options.maxFallbackAgeMs ?? LIMITS.maxFallbackAgeMs.default,
    LIMITS.maxFallbackAgeMs.min,
    LIMITS.maxFallbackAgeMs.max
  );

  if (maxFallbackAgeMs < maxFreshAgeMs) {
    throw new InvalidArgumentError(
      'options.maxFallbackAgeMs',
      'must not be smaller than options.maxFreshAgeMs'
    );
  }

  const maxHorizontalAccuracyMeters = requirePositiveNumber(
    'options.maxHorizontalAccuracyMeters',
    options.maxHorizontalAccuracyMeters ??
      LIMITS.maxHorizontalAccuracyMeters.default
  );

  const maxPlausibleSpeedMps = requirePositiveNumber(
    'options.maxPlausibleSpeedMps',
    options.maxPlausibleSpeedMps ?? LIMITS.maxPlausibleSpeedMps.default
  );

  const enableMotionAssistance = requireBoolean(
    'options.enableMotionAssistance',
    options.enableMotionAssistance ?? true
  );

  const allowHistoricalFallback = requireBoolean(
    'options.allowHistoricalFallback',
    options.allowHistoricalFallback ?? true
  );

  const mockLocationPolicy = options.mockLocationPolicy ?? 'zero_score';
  const policyIsKnown =
    mockLocationPolicy === 'zero_score' || mockLocationPolicy === 'flag_only';
  if (!policyIsKnown) {
    throw new InvalidArgumentError(
      'options.mockLocationPolicy',
      "must be 'zero_score' or 'flag_only'"
    );
  }

  return {
    sessionId,
    latitude,
    longitude,
    radiusMeters,
    totalDeadlineMs,
    initialFixBudgetMs,
    recoveryWindowMs,
    maxFreshAgeMs,
    maxFallbackAgeMs,
    maxHorizontalAccuracyMeters,
    enableMotionAssistance,
    allowHistoricalFallback,
    maxPlausibleSpeedMps,
    zeroScoreForMockLocation: mockLocationPolicy === 'zero_score',
  };
}

/** Checks the readings an app makes up for the simulation. */
export function checkSimulatedReadings(
  readings: SimulatedReading[]
): NativeSimulatedReading[] {
  if (!Array.isArray(readings)) {
    throw new InvalidArgumentError('readings', 'must be an array');
  }

  return readings.map((reading, index) => {
    const name = `readings[${index}]`;
    requireObject(name, reading);

    const accuracyMeters = requireNumber(
      `${name}.accuracyMeters`,
      reading.accuracyMeters
    );
    if (accuracyMeters < 0) {
      throw new InvalidArgumentError(
        `${name}.accuracyMeters`,
        'must not be negative'
      );
    }

    const ageMs = requireNumber(`${name}.ageMs`, reading.ageMs ?? 0);
    if (ageMs < 0) {
      throw new InvalidArgumentError(`${name}.ageMs`, 'must not be negative');
    }

    return {
      latitude: requireLatitude(`${name}.latitude`, reading.latitude),
      longitude: requireLongitude(`${name}.longitude`, reading.longitude),
      accuracyMeters,
      ageMs,
      speedMps:
        reading.speedMps === undefined
          ? null
          : requireNumber(`${name}.speedMps`, reading.speedMps),
      speedAccuracyMps:
        reading.speedAccuracyMps === undefined
          ? null
          : requireNumber(`${name}.speedAccuracyMps`, reading.speedAccuracyMps),
      flaggedAsMock: requireBoolean(
        `${name}.flaggedAsMock`,
        reading.flaggedAsMock ?? false
      ),
      approximate: requireBoolean(
        `${name}.approximate`,
        reading.approximate ?? false
      ),
    };
  });
}
