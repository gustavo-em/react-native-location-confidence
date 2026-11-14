package com.locationconfidence

import com.facebook.react.bridge.ReactApplicationContext

class LocationConfidenceModule(reactContext: ReactApplicationContext) :
  NativeLocationConfidenceSpec(reactContext) {

  override fun multiply(a: Double, b: Double): Double {
    return a * b
  }

  companion object {
    const val NAME = NativeLocationConfidenceSpec.NAME
  }
}
