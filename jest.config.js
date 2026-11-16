/*
 * Tests of the TypeScript layer (validation, mapper, AbortSignal).
 *
 * They run in plain Node: nothing here needs React Native, a phone or the
 * native code. The native module is replaced by a fake one.
 */
module.exports = {
  testEnvironment: 'node',
  // A plain directory walk is enough for a handful of files.
  watchman: false,
  roots: ['<rootDir>/src'],
  testMatch: ['**/__tests__/**/*.test.ts'],
  transform: {
    '^.+\\.(ts|tsx)$': [
      'babel-jest',
      {
        configFile: false,
        babelrc: false,
        presets: ['module:@react-native/babel-preset'],
      },
    ],
  },
};
