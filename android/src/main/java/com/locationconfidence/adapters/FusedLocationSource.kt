package com.locationconfidence.adapters

import android.annotation.SuppressLint
import android.content.Context
import android.location.Location
import android.os.Build
import com.google.android.gms.location.FusedLocationProviderClient
import com.google.android.gms.location.LocationCallback
import com.google.android.gms.location.LocationRequest
import com.google.android.gms.location.LocationResult
import com.google.android.gms.location.LocationServices
import com.google.android.gms.location.Priority
import com.locationconfidence.models.MockSignal
import com.locationconfidence.models.Reading
import com.locationconfidence.models.ReadingProvider
import com.locationconfidence.ports.LocationSource
import java.util.concurrent.Executor

/**
 * The real GPS on Android: the "fused location provider" of Google Play
 * services, which mixes GPS, Wi-Fi and cell towers.
 *
 * It only translates. Each Android `Location` becomes a [Reading] with
 * exactly what Android said, and nothing is judged here: whether a reading
 * is good, old or suspicious is the engine's decision.
 *
 * The caller must have checked the location permission before [start].
 */
internal class FusedLocationSource(
  private val context: Context,
  private val deviceState: AndroidDeviceState,
  /** The thread Google Play services uses to call us back. */
  private val callbackExecutor: Executor,
) : LocationSource {

  private var client: FusedLocationProviderClient? = null
  private var callback: LocationCallback? = null

  @SuppressLint("MissingPermission") // checked by the engine before the GPS is wanted
  override fun start(timeBetweenReadingsMs: Long, listener: LocationSource.Listener) {
    stop()

    val request = LocationRequest.Builder(
      Priority.PRIORITY_HIGH_ACCURACY,
      maxOf(MINIMUM_TIME_BETWEEN_READINGS_MS, timeBetweenReadingsMs),
    ).build()

    val newCallback = object : LocationCallback() {
      override fun onLocationResult(result: LocationResult) {
        // One call can bring several locations, old and new. Each one keeps
        // its own time; the engine sorts them out.
        listener.onReadings(result.locations.map { toReading(it) })
      }
      // onLocationAvailability() is deliberately not used: "not available" is
      // only a hint, and a reading can still arrive after it.
    }

    try {
      val newClient = LocationServices.getFusedLocationProviderClient(context)
      newClient.requestLocationUpdates(request, callbackExecutor, newCallback)
        .addOnFailureListener(callbackExecutor) { listener.onFailure() }
      client = newClient
      callback = newCallback
    } catch (error: SecurityException) {
      // The user took the permission away between our check and this call.
      listener.onFailure()
    } catch (error: RuntimeException) {
      // Google Play services is missing or broken on this phone.
      listener.onFailure()
    }
  }

  override fun stop() {
    val runningCallback = callback
    if (runningCallback != null) {
      client?.removeLocationUpdates(runningCallback)
    }
    callback = null
    client = null
  }

  private fun toReading(location: Location): Reading {
    val speedAccuracy: Double? =
      if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O && location.hasSpeedAccuracy()) {
        location.speedAccuracyMetersPerSecond.toDouble()
      } else {
        null
      }

    @Suppress("DEPRECATION") // isFromMockProvider is the only choice before Android 12
    val markedAsSimulated =
      if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) location.isMock else location.isFromMockProvider

    return Reading(
      latitude = location.latitude,
      longitude = location.longitude,
      accuracyMeters = if (location.hasAccuracy()) location.accuracy.toDouble() else null,
      // Android stamps the location with the same clock as ElapsedRealtimeClock,
      // in nanoseconds.
      capturedAtMs = location.elapsedRealtimeNanos / 1_000_000L,
      timeIsReliable = true,
      captureTimeWasEstimated = false,
      onlyApproximatePermission = !deviceState.preciseLocationIsGranted(),
      speedMps = if (location.hasSpeed()) location.speed.toDouble() else null,
      speedAccuracyMps = speedAccuracy,
      headingDegrees = if (location.hasBearing()) location.bearing.toDouble() else null,
      provider = ReadingProvider.FUSED,
      // "Not flagged" only repeats what Android said. It proves nothing.
      mockSignal = if (markedAsSimulated) MockSignal.FLAGGED else MockSignal.NOT_FLAGGED,
    )
  }

  private companion object {
    /** Asking for readings more often than this only wastes battery. */
    const val MINIMUM_TIME_BETWEEN_READINGS_MS = 250L
  }
}
