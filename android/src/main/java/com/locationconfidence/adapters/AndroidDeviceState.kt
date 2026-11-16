package com.locationconfidence.adapters

import android.Manifest
import android.content.Context
import android.content.pm.PackageManager
import android.location.LocationManager
import androidx.core.content.ContextCompat
import androidx.core.location.LocationManagerCompat
import com.google.android.gms.common.ConnectionResult
import com.google.android.gms.common.GoogleApiAvailabilityLight
import com.locationconfidence.models.DeviceState
import com.locationconfidence.models.LocationPermission
import com.locationconfidence.models.LocationProviderState
import com.locationconfidence.models.PermissionPrecision
import com.locationconfidence.models.PhoneActivitySupport
import com.locationconfidence.ports.DeviceStateSource

/**
 * Reads the state of the phone: permission, location switch, Google Play
 * services, foreground.
 *
 * It only reads. Nothing here asks the user for anything: asking for a
 * permission is a separate, explicit call of the app
 * (LocationConfidenceModule.requestLocationPermission).
 */
internal class AndroidDeviceState(private val context: Context) : DeviceStateSource {

  /** Updated by the module when the app goes to the background and comes back. */
  @Volatile
  var appIsInForeground: Boolean = true

  /**
   * True after the app asked the user for the location permission in this
   * run. Android does not tell "never asked" from "denied" by itself, so
   * this is how the two are told apart. It is not saved anywhere: after the
   * app restarts, a denied permission is reported as "not determined" until
   * the app asks again.
   */
  @Volatile
  var locationPermissionWasAsked: Boolean = false

  fun preciseLocationIsGranted(): Boolean = isGranted(Manifest.permission.ACCESS_FINE_LOCATION)

  private fun approximateLocationIsGranted(): Boolean =
    isGranted(Manifest.permission.ACCESS_COARSE_LOCATION)

  fun locationPermission(): LocationPermission = when {
    preciseLocationIsGranted() || approximateLocationIsGranted() -> LocationPermission.GRANTED
    locationPermissionWasAsked -> LocationPermission.DENIED
    else -> LocationPermission.NOT_DETERMINED
  }

  /** Since Android 12 the user can give only the approximate location. */
  fun permissionPrecision(): PermissionPrecision = when {
    preciseLocationIsGranted() -> PermissionPrecision.PRECISE
    approximateLocationIsGranted() -> PermissionPrecision.APPROXIMATE
    else -> PermissionPrecision.UNKNOWN
  }

  /** The "Location" switch in the phone's settings. */
  fun locationIsTurnedOn(): Boolean {
    val locationManager = context.getSystemService(Context.LOCATION_SERVICE) as? LocationManager
    return locationManager != null && LocationManagerCompat.isLocationEnabled(locationManager)
  }

  fun locationProvider(): LocationProviderState = try {
    val result = GoogleApiAvailabilityLight.getInstance().isGooglePlayServicesAvailable(context)
    if (result == ConnectionResult.SUCCESS) {
      LocationProviderState.AVAILABLE
    } else {
      LocationProviderState.PLAY_SERVICES_MISSING
    }
  } catch (error: RuntimeException) {
    LocationProviderState.PLAY_SERVICES_MISSING
  }

  override fun currentDeviceState(): DeviceState = DeviceState(
    locationPermission = locationPermission(),
    permissionPrecision = permissionPrecision(),
    locationIsTurnedOn = locationIsTurnedOn(),
    locationProvider = locationProvider(),
    appIsInForeground = appIsInForeground,
    // See NoPhoneActivitySource: not supported on Android in this version.
    phoneActivity = PhoneActivitySupport.NOT_SUPPORTED,
  )

  private fun isGranted(permission: String): Boolean =
    ContextCompat.checkSelfPermission(context, permission) == PackageManager.PERMISSION_GRANTED
}
