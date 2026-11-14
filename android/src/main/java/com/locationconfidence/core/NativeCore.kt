package com.locationconfidence.core

/**
 * Single JNI entry point into the shared C++ core.
 *
 * The native methods are registered from JNI_OnLoad (see JniBindings.cpp).
 * This class and its native methods are kept by consumer-rules.pro so that
 * R8 in the host app cannot rename or strip them.
 */
internal object NativeCore {
  private val loadError: Throwable? = try {
    System.loadLibrary("location-confidence")
    null
  } catch (error: UnsatisfiedLinkError) {
    error
  }

  /** True when the native library was loaded for the current ABI. */
  val isAvailable: Boolean
    get() = loadError == null

  /** ABI version of the compiled core, or null when the library is unavailable. */
  fun abiVersionOrNull(): Int? = if (isAvailable) nativeAbiVersion() else null

  @JvmStatic
  private external fun nativeAbiVersion(): Int
}
