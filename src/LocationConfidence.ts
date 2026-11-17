/*
 * The public object of the library: `LocationConfidence`.
 *
 * It is a thin layer. For every call it:
 *
 *   1. checks the arguments          (validation.ts, nothing native yet)
 *   2. calls the native side         (Kotlin on Android, Swift on iOS)
 *   3. renames the answer            (mapper.ts)
 *
 * No rule about locations lives here: the decisions are made by the C++
 * engine, the same one on Android and iOS.
 *
 *   your app
 *     -> LocationConfidence           (this file)
 *       -> the native module          (turns the GPS on and off)
 *         -> the C++ engine           (every decision)
 */
import {
  InvalidArgumentError,
  LocationConfidenceError,
  NativeModuleError,
} from './errors';
import {
  MOTION_CONFIDENCE_TEXTS,
  MOTION_STATE_TEXTS,
  reportCancelledBeforeItStarted,
  toProximityReport,
  toAuthorization,
  toCapabilities,
  toSession,
} from './mapper';
import type { Spec } from './NativeLocationConfidence';
import type {
  ProximityReport,
  ProximityRequest,
  LocationConfidenceApi,
  SessionOptions,
  SimulatedReading,
} from './types';
import {
  checkProximityRequest,
  checkSessionOptions,
  checkSimulatedReadings,
  requireSessionId,
} from './validation';

/** The native functions this file needs. Tests hand over a fake one. */
export type NativePort = Omit<Spec, 'getConstants'>;

/** The codes the native side uses when it rejects a call. */
const CODES_THE_NATIVE_SIDE_USES = [
  'platform_unsupported',
  'native_internal_error',
  'no_activity',
];

/** Turns whatever the native side threw into one of the library's errors. */
function toLibraryError(thrown: unknown): Error {
  if (thrown instanceof LocationConfidenceError) {
    return thrown;
  }

  const details = (thrown ?? {}) as { code?: unknown; message?: unknown };
  const message =
    typeof details.message === 'string'
      ? details.message
      : 'The native side failed';

  if (details.code === 'invalid_argument') {
    return new InvalidArgumentError('request', message);
  }

  const codeIsKnown =
    typeof details.code === 'string' &&
    CODES_THE_NATIVE_SIDE_USES.includes(details.code);
  const code = codeIsKnown ? (details.code as string) : 'native_internal_error';
  return new NativeModuleError(code, message);
}

let howManyRequestIdsWereMade = 0;

/**
 * A name for one request, for example "lc-1-k3f9x2". It only has to be
 * different from the other requests that are running, so that cancelling
 * one never cancels another.
 */
function makeRequestId(): string {
  howManyRequestIdsWereMade += 1;
  const randomPart = Math.random().toString(36).slice(2, 8);
  return `lc-${howManyRequestIdsWereMade}-${randomPart}`;
}

/** An AbortSignal, or the error that says the argument is not one. */
function checkSignal(signal: unknown): AbortSignal | undefined {
  if (signal === undefined) {
    return undefined;
  }
  const candidate = signal as Partial<AbortSignal> | null;
  const looksLikeAnAbortSignal =
    candidate !== null &&
    typeof candidate === 'object' &&
    typeof candidate.aborted === 'boolean' &&
    typeof candidate.addEventListener === 'function' &&
    typeof candidate.removeEventListener === 'function';
  if (!looksLikeAnAbortSignal) {
    throw new InvalidArgumentError('signal', 'must be an AbortSignal');
  }
  return candidate as AbortSignal;
}

/**
 * Builds the public object on top of a native module.
 *
 * `getNative` is a function, and not the module itself, so that an app
 * where the native part is missing only fails when it USES the library,
 * with a message that says what to do, and not when it imports it.
 */
export function createLocationConfidence(
  getNative: () => NativePort
): LocationConfidenceApi {
  /** Runs one native call and makes sure only library errors come out. */
  async function callNative<T>(
    call: (native: NativePort) => Promise<T>
  ): Promise<T> {
    try {
      return await call(getNative());
    } catch (thrown) {
      throw toLibraryError(thrown);
    }
  }

  async function getProximityReport(
    request: ProximityRequest
  ): Promise<ProximityReport> {
    // 1. Check everything first. A broken request never reaches the GPS.
    const nativeRequest = checkProximityRequest(request);
    const signal = checkSignal(request.signal);
    const requestId = makeRequestId();

    // 2. Cancelled before it started: answer here, call nothing native.
    if (signal !== undefined && signal.aborted) {
      return reportCancelledBeforeItStarted(
        requestId,
        nativeRequest.sessionId,
        nativeRequest.radiusMeters
      );
    }

    // 3. Run it. If the app aborts in the middle, the native side is told
    //    and it finishes the request with status 'cancelled'. The answer
    //    still comes through the same Promise, exactly once.
    const native = getNative();
    const cancelOnTheNativeSide = () => native.cancelRequest(requestId);
    signal?.addEventListener('abort', cancelOnTheNativeSide);
    try {
      const nativeReport = await native.getReport(requestId, nativeRequest);
      return toProximityReport(requestId, nativeReport);
    } catch (thrown) {
      throw toLibraryError(thrown);
    } finally {
      // An abort that comes after the answer does nothing at all.
      signal?.removeEventListener('abort', cancelOnTheNativeSide);
    }
  }

  return {
    async getCapabilities() {
      return toCapabilities(
        await callNative((native) => native.getCapabilities())
      );
    },

    async getAuthorization() {
      return toAuthorization(
        await callNative((native) => native.getAuthorization())
      );
    },

    async requestLocationPermission() {
      return toAuthorization(
        await callNative((native) => native.requestLocationPermission())
      );
    },

    async requestMotionPermission() {
      return toAuthorization(
        await callNative((native) => native.requestMotionPermission())
      );
    },

    async startSession(options?: SessionOptions) {
      const nativeOptions = checkSessionOptions(options);
      return toSession(
        await callNative((native) => native.startSession(nativeOptions))
      );
    },

    async clearHistory(sessionId: string) {
      const id = requireSessionId('sessionId', sessionId);
      await callNative((native) => native.clearHistory(id));
    },

    async stopSession(sessionId: string) {
      const id = requireSessionId('sessionId', sessionId);
      await callNative((native) => native.stopSession(id));
    },

    async getProximityReport(request: ProximityRequest) {
      try {
        return await getProximityReport(request);
      } catch (thrown) {
        throw toLibraryError(thrown);
      }
    },

    simulation: {
      async enable() {
        await callNative((native) => native.setSimulationEnabled(true));
      },

      async disable() {
        await callNative((native) => native.setSimulationEnabled(false));
      },

      async pushReadings(readings: SimulatedReading[]) {
        const nativeReadings = checkSimulatedReadings(readings);
        await callNative((native) => native.simulateReadings(nativeReadings));
      },

      async setPhoneActivity(state, confidence = 'high') {
        // The numbers are the engine's: 0 standing still ... 4 in a vehicle,
        // and -1 for "the phone stopped reporting what it is doing".
        // The checks look at plain texts because a JavaScript app can pass
        // anything, whatever the types say.
        const stateText: string | null = state;
        const stateNumber =
          stateText === null
            ? -1
            : (MOTION_STATE_TEXTS as readonly string[]).indexOf(stateText);
        const stateIsAllowed =
          stateText === null || (stateNumber >= 0 && stateText !== 'unknown');
        if (!stateIsAllowed) {
          throw new InvalidArgumentError(
            'state',
            "must be 'stationary', 'walking', 'running', 'cycling', " +
              "'vehicle' or null"
          );
        }

        const confidenceText: string = confidence;
        const confidenceNumber = (
          MOTION_CONFIDENCE_TEXTS as readonly string[]
        ).indexOf(confidenceText);
        const confidenceIsAllowed =
          confidenceNumber >= 0 && confidenceText !== 'unknown';
        if (!confidenceIsAllowed) {
          throw new InvalidArgumentError(
            'confidence',
            "must be 'high', 'medium' or 'low'"
          );
        }

        await callNative((native) =>
          native.simulatePhoneActivity(stateNumber, confidenceNumber)
        );
      },
    },
  };
}
