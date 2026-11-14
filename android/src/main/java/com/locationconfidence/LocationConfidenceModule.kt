package com.locationconfidence

import com.facebook.react.bridge.Arguments
import com.facebook.react.bridge.Promise
import com.facebook.react.bridge.ReactApplicationContext
import com.facebook.react.bridge.ReadableMap
import com.locationconfidence.core.NativeCore

/**
 * TurboModule inbound adapter.
 *
 * Phase 0.5 (integration spike): only proves the JS -> Kotlin -> JNI -> C
 * path and the Codegen transport types. No location logic yet.
 */
class LocationConfidenceModule(reactContext: ReactApplicationContext) :
  NativeLocationConfidenceSpec(reactContext) {

  override fun getAbiVersion(promise: Promise) {
    val version = NativeCore.abiVersionOrNull()
    if (version == null) {
      promise.reject("platform_unsupported", "Native core library is not available for this ABI")
      return
    }
    promise.resolve(version.toDouble())
  }

  override fun spikeEcho(requestId: String, input: ReadableMap, promise: Promise) {
    val version = NativeCore.abiVersionOrNull()
    if (version == null) {
      promise.reject("platform_unsupported", "Native core library is not available for this ABI")
      return
    }

    val sessionId = if (input.isNull("sessionId")) null else input.getString("sessionId")
    val codes = input.getArray("codes")

    val tags = Arguments.createArray()
    if (codes != null) {
      for (index in 0 until codes.size()) {
        tags.pushString(codes.getString(index))
      }
    }

    val items = Arguments.createArray()
    items.pushMap(
      Arguments.createMap().apply {
        putString("label", "radiusMeters")
        putDouble("value", input.getDouble("radiusMeters"))
      }
    )
    items.pushMap(
      Arguments.createMap().apply {
        putString("label", "absent")
        putNull("value")
      }
    )

    val result = Arguments.createMap().apply {
      putString("requestId", requestId)
      putDouble("abiVersion", version.toDouble())
      putString("platform", "android")
      putString("status", if (input.getBoolean("enabled")) "ok" else "degraded")
      putString("mode", input.getString("mode"))
      if (sessionId == null) putNull("sessionId") else putString("sessionId", sessionId)
      putNull("maybeNumber")
      putBoolean("flag", input.getBoolean("enabled"))
      putArray("tags", tags)
      putArray("items", items)
    }
    promise.resolve(result)
  }

  override fun cancelRequest(requestId: String) {
    // Spike: nothing to cancel yet.
  }

  companion object {
    const val NAME = NativeLocationConfidenceSpec.NAME
  }
}
