import { NativeModuleError } from './errors';
import { createLocationConfidence } from './LocationConfidence';
import NativeLocationConfidence from './NativeLocationConfidence';

/**
 * The native module, or the error that says what is wrong with the
 * installation. Importing the library never fails: the check only runs when
 * the app calls something.
 */
function requireTheNativeModule() {
  if (NativeLocationConfidence == null) {
    throw new NativeModuleError(
      'native_module_missing',
      'The native part of react-native-location-confidence was not found. ' +
        'Rebuild the app after installing the package (on iOS, run ' +
        '"pod install" first). The library needs the New Architecture ' +
        'and does not work in Expo Go.'
    );
  }
  return NativeLocationConfidence;
}

/**
 * Tells how good the location evidence is for "the phone is inside this
 * delivery radius", while the app is on the screen.
 *
 * It never approves or refuses a delivery: it reports an index from 0 to
 * 100 (not a percentage), what the evidence points to, and why.
 */
export const LocationConfidence = createLocationConfidence(
  requireTheNativeModule
);

export {
  InvalidArgumentError,
  LocationConfidenceError,
  NativeModuleError,
} from './errors';

export type {
  AccuracyAuthorization,
  AgeReliability,
  Authorization,
  Capabilities,
  Coordinates,
  EvidenceRelation,
  FixSource,
  LocationConfidenceApi,
  MockLocationPolicy,
  MotionConfidence,
  MotionState,
  PermissionStatus,
  ProximityOptions,
  ProximityReport,
  ProximityRequest,
  ReportStatus,
  Session,
  SessionOptions,
  SimulatedReading,
  SimulationApi,
} from './types';
