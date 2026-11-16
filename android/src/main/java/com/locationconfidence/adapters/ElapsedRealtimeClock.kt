package com.locationconfidence.adapters

import android.os.SystemClock
import com.locationconfidence.ports.Clock

/**
 * The real clock: milliseconds since the phone was turned on, counting the
 * time it spent asleep.
 *
 * It only moves forward and it does not change when the user changes the
 * date or the time, so it is safe for measuring how old a reading is.
 * Android stamps every location with this same clock
 * (Location.getElapsedRealtimeNanos), which is why the two can be compared.
 */
internal class ElapsedRealtimeClock : Clock {
  override fun nowMs(): Long = SystemClock.elapsedRealtime()
}
