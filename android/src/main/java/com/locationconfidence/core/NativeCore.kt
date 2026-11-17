package com.locationconfidence.core

/**
 * The door from Kotlin into the C++ engine.
 *
 * Every function marked `external` has no body here: its body is a C++
 * function in android/src/main/cpp/JniBindings.cpp. The two are connected by
 * name when the library is loaded (see JNI_OnLoad in that file).
 *
 * If you add, remove or change a function here, change the table
 * NATIVE_METHODS in JniBindings.cpp in the same way. Nothing checks that the
 * two match until the app runs.
 *
 * android/consumer-rules.pro tells the app's code shrinker (R8) to keep this
 * class and the names of its `external` functions, because the C++ side
 * looks them up by name.
 *
 * Nothing outside this package calls these functions directly: CoreEngine.kt
 * wraps them and turns the lists of numbers into Kotlin objects.
 */
internal object NativeCore {
  // Loading the library runs JNI_OnLoad. It fails with UnsatisfiedLinkError
  // when the app was built without the .so for this phone's processor.
  private val loadError: Throwable? = try {
    System.loadLibrary("location-confidence")
    null
  } catch (error: UnsatisfiedLinkError) {
    error
  }

  /** True when the native library was loaded for this phone's processor. */
  val isAvailable: Boolean
    get() = loadError == null

  /** Version of the engine's front door, or null when the library is unavailable. */
  fun abiVersionOrNull(): Int? = if (isAvailable) nativeAbiVersion() else null

  @JvmStatic external fun nativeAbiVersion(): Int

  /** Returns the address of a new engine, kept as a number. 0 means "no memory". */
  @JvmStatic external fun nativeCreateEngine(): Long
  @JvmStatic external fun nativeDestroyEngine(engine: Long)

  @JvmStatic external fun nativeOpenSession(engine: Long, settings: DoubleArray, nowMs: Long): DoubleArray?
  @JvmStatic external fun nativeClearSessionHistory(engine: Long, sessionId: Int): Int
  @JvmStatic external fun nativeCloseSession(engine: Long, sessionId: Int, nowMs: Long): Int

  /** Returns the request id, or minus an error code of lce.h. */
  @JvmStatic external fun nativeStartRequest(engine: Long, sessionId: Int, settings: DoubleArray, nowMs: Long): Long
  @JvmStatic external fun nativeCancelRequest(engine: Long, requestId: Int, nowMs: Long): Int

  @JvmStatic external fun nativeReadingsArrived(engine: Long, readings: DoubleArray, howMany: Int, nowMs: Long): Int
  @JvmStatic external fun nativePhoneActivityChanged(engine: Long, activity: Int, confidence: Int, sinceMs: Long, nowMs: Long): Int
  @JvmStatic external fun nativePhoneActivityUpdatesStopped(engine: Long, nowMs: Long): Int
  @JvmStatic external fun nativeTimePassed(engine: Long, nowMs: Long): Int
  @JvmStatic external fun nativeAppWentToBackground(engine: Long, nowMs: Long): Int
  @JvmStatic external fun nativeLocationPermissionWasLost(engine: Long, nowMs: Long): Int
  @JvmStatic external fun nativeLocationProviderFailed(engine: Long, nowMs: Long): Int
  @JvmStatic external fun nativeSetReadingsAreSimulated(engine: Long, simulated: Boolean): Int

  @JvmStatic external fun nativeWhatThePlatformMustDoNow(engine: Long): DoubleArray?

  /** One finished report as a list of numbers, or null when none is waiting. */
  @JvmStatic external fun nativeTakeFinishedReport(engine: Long): DoubleArray?

  /** The public texts: which = 0 reasons, 1 score limits, 2 steps. */
  @JvmStatic external fun nativeTexts(which: Int): Array<String>?
}
