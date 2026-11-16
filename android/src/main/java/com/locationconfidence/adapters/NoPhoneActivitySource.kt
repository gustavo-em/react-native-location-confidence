package com.locationconfidence.adapters

import com.locationconfidence.ports.PhoneActivitySource

/**
 * Android does not report the phone activity in this version of the library.
 *
 * The Android API for it (Activity Recognition) delivers its results late,
 * through a broadcast, and needs one more permission. It was left for a later
 * version (decision D-02 in docs/SPEC_REVIEW.md). Until then the engine is
 * told "not supported" and treats the activity as unknown, which is the
 * cautious choice. The readings still work without it.
 */
internal class NoPhoneActivitySource : PhoneActivitySource {
  override fun start(listener: PhoneActivitySource.Listener) {
    // Nothing to start.
  }

  override fun stop() {
    // Nothing to stop.
  }
}
