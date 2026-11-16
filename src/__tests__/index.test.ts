import { afterEach, describe, expect, it, jest } from '@jest/globals';
import { FakeNative } from './FakeNative';

/*
 * The entry file is the only one that touches React Native. Here React
 * Native is replaced by an object with the one function the library uses.
 *
 * (Jest only lets a mock read variables whose name starts with "mock".)
 */
let mockModuleReactNativeHasRegistered: unknown = null;

jest.mock('react-native', () => ({
  TurboModuleRegistry: {
    get: () => mockModuleReactNativeHasRegistered,
  },
}));

afterEach(() => {
  jest.resetModules();
  mockModuleReactNativeHasRegistered = null;
});

describe('importing the library', () => {
  it('never fails, even when the native part is missing', async () => {
    mockModuleReactNativeHasRegistered = null;

    const library = require('../index') as typeof import('../index');

    let error: unknown;
    try {
      await library.LocationConfidence.getCapabilities();
    } catch (thrown) {
      error = thrown;
    }
    expect(error).toBeInstanceOf(library.NativeModuleError);
    expect((error as { code: string }).code).toBe('native_module_missing');
    expect((error as Error).message).toContain('Rebuild the app');
    expect((error as Error).message).toContain('Expo Go');
  });

  it('collects nothing: no native function is called by the import alone', () => {
    const native = new FakeNative();
    mockModuleReactNativeHasRegistered = native;

    const library = require('../index') as typeof import('../index');

    expect(typeof library.LocationConfidence.evaluateProximity).toBe(
      'function'
    );
    expect(native.calls).toEqual([]);
  });

  it('uses the module React Native has registered', async () => {
    const native = new FakeNative();
    mockModuleReactNativeHasRegistered = native;

    const library = require('../index') as typeof import('../index');
    const authorization = await library.LocationConfidence.getAuthorization();

    expect(authorization.locationGranted).toBe(true);
    expect(native.calls).toEqual(['getAuthorization']);
  });

  it('exports the object, the three errors and nothing else at run time', () => {
    const library = require('../index') as typeof import('../index');
    expect(Object.keys(library).sort()).toEqual([
      'InvalidArgumentError',
      'LocationConfidence',
      'LocationConfidenceError',
      'NativeModuleError',
    ]);
  });
});
