/*
 * The errors the library throws.
 *
 * Only programming mistakes and a broken installation are thrown. Things
 * that simply happen (no permission, GPS turned off, timeout, cancel) are
 * NOT errors: they come back as an assessment with a `status` and
 * `reasonCodes`.
 */

/** Base class of every error thrown by the library. */
export class LocationConfidenceError extends Error {
  /** A short fixed label, for example "invalid_argument". */
  readonly code: string;

  constructor(code: string, message: string) {
    super(message);
    this.name = 'LocationConfidenceError';
    this.code = code;
  }
}

/**
 * An argument makes no sense: a latitude of 91, a negative radius, a timeout
 * that is not a number. It is thrown before anything native is called, so
 * the GPS is never turned on for a broken request.
 */
export class InvalidArgumentError extends LocationConfidenceError {
  /** Which argument is wrong, for example "destination.latitude". */
  readonly argument: string;

  constructor(argument: string, message: string) {
    super('invalid_argument', `${argument}: ${message}`);
    this.name = 'InvalidArgumentError';
    this.argument = argument;
  }
}

/**
 * The native part of the library is missing or does not match this
 * JavaScript: the app was not rebuilt after installing, the engine was not
 * compiled for this phone's processor, or the versions differ.
 */
export class NativeModuleError extends LocationConfidenceError {
  constructor(code: string, message: string) {
    super(code, message);
    this.name = 'NativeModuleError';
  }
}
