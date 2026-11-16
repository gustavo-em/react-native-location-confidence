package com.locationconfidence

import android.Manifest
import android.os.Build
import androidx.lifecycle.DefaultLifecycleObserver
import androidx.lifecycle.Lifecycle
import androidx.lifecycle.LifecycleOwner
import androidx.lifecycle.ProcessLifecycleOwner
import com.facebook.react.bridge.Arguments
import com.facebook.react.bridge.Promise
import com.facebook.react.bridge.ReactApplicationContext
import com.facebook.react.bridge.ReadableArray
import com.facebook.react.bridge.ReadableMap
import com.facebook.react.bridge.UiThreadUtil
import com.facebook.react.bridge.WritableArray
import com.facebook.react.bridge.WritableMap
import com.facebook.react.modules.core.PermissionAwareActivity
import com.facebook.react.modules.core.PermissionListener
import com.locationconfidence.adapters.AndroidDeviceState
import com.locationconfidence.adapters.ElapsedRealtimeClock
import com.locationconfidence.adapters.FusedLocationSource
import com.locationconfidence.adapters.NoPhoneActivitySource
import com.locationconfidence.adapters.SingleThreadQueue
import com.locationconfidence.application.EngineCoordinator
import com.locationconfidence.core.CoreEngine
import com.locationconfidence.core.NativeCore
import com.locationconfidence.models.ActivityConfidence
import com.locationconfidence.models.CollectingState
import com.locationconfidence.models.EvaluationSettings
import com.locationconfidence.models.LocationProviderState
import com.locationconfidence.models.MockPolicy
import com.locationconfidence.models.MockSignal
import com.locationconfidence.models.PhoneActivity
import com.locationconfidence.models.PhoneActivitySupport
import com.locationconfidence.models.Reading
import com.locationconfidence.models.Report
import com.locationconfidence.models.SessionSettings
import com.locationconfidence.ports.StartRefusal
import java.util.concurrent.Executor

/**
 * What JavaScript sees on Android.
 *
 * React Native creates one of these and calls its functions when JavaScript
 * calls `NativeLocationConfidence.something()`. The list of functions comes
 * from src/NativeLocationConfidence.ts: Codegen turns that file into the
 * class `NativeLocationConfidenceSpec`, and this class fills it in.
 *
 * This class is only a translator and a builder:
 *
 *   - it BUILDS the pieces once (the engine, the GPS source, the queue) and
 *     hands them to the coordinator;
 *   - for each call from JavaScript it TRANSLATES the arguments into Kotlin
 *     objects, asks the coordinator, and translates the answer back.
 *
 * No rule about locations lives here.
 *
 *   JavaScript
 *     -> LocationConfidenceModule    (this file: translate)
 *       -> EngineCoordinator         (tell the engine, then obey it)
 *         -> CoreEngine -> JNI       (Kotlin objects <-> lists of numbers)
 *           -> the C++ engine        (every decision)
 */
class LocationConfidenceModule(reactContext: ReactApplicationContext) :
  NativeLocationConfidenceSpec(reactContext) {

  private val deviceState = AndroidDeviceState(reactContext)
  private val clock = ElapsedRealtimeClock()

  /**
   * Null when the native library could not be loaded on this phone (for
   * example, an app built without the .so for this processor). Every call
   * then answers "platform_unsupported" instead of crashing.
   */
  private val coordinator: EngineCoordinator? = CoreEngine.createOrNull()?.let { engine ->
    val queue = SingleThreadQueue()
    // Google Play services calls back on this executor: straight onto our queue.
    val onTheQueue = Executor { task -> queue.post { task.run() } }
    EngineCoordinator(
      engine = engine,
      queue = queue,
      clock = clock,
      realLocationSource = FusedLocationSource(reactContext, deviceState, onTheQueue),
      phoneActivitySource = NoPhoneActivitySource(),
      realDeviceState = deviceState,
    )
  }

  /**
   * Tells us when the whole app goes to the background and comes back.
   * "Background" here means the app is really not visible. A permission
   * dialog or the notification shade on top of the app does not count.
   */
  private val appVisibilityObserver = object : DefaultLifecycleObserver {
    override fun onStart(owner: LifecycleOwner) {
      deviceState.appIsInForeground = true
      coordinator?.appCameToForeground()
    }

    override fun onStop(owner: LifecycleOwner) {
      deviceState.appIsInForeground = false
      coordinator?.appWentToBackground()
    }
  }

  init {
    // The lifecycle of the app may only be watched from the main thread.
    UiThreadUtil.runOnUiThread {
      val lifecycle = ProcessLifecycleOwner.get().lifecycle
      deviceState.appIsInForeground = lifecycle.currentState.isAtLeast(Lifecycle.State.STARTED)
      lifecycle.addObserver(appVisibilityObserver)
    }
  }

  /** React Native is tearing the module down (reload, or the app is closing). */
  override fun invalidate() {
    UiThreadUtil.runOnUiThread {
      ProcessLifecycleOwner.get().lifecycle.removeObserver(appVisibilityObserver)
    }
    coordinator?.shutDown()
    super.invalidate()
  }

  // --- Capabilities and permissions ------------------------------------------

  override fun getCapabilities(promise: Promise) {
    val reasons = Arguments.createArray()
    val engineIsMissing = coordinator == null
    if (engineIsMissing) {
      reasons.pushString("platform_unsupported")
    }

    val locationIsOn = deviceState.locationIsTurnedOn()
    if (!locationIsOn) {
      reasons.pushString("location_services_disabled")
    }

    val providerIsAvailable = deviceState.locationProvider() == LocationProviderState.AVAILABLE
    if (!providerIsAvailable) {
      reasons.pushString("play_services_unavailable")
    }
    reasons.pushString("motion_unavailable") // see NoPhoneActivitySource

    fun answer(simulationIsOn: Boolean) {
      val capabilities = Arguments.createMap().apply {
        putBoolean("locationServicesEnabled", locationIsOn)
        putBoolean("locationProviderAvailable", providerIsAvailable)
        putBoolean("motionSupported", false)
        // Android tells when a location comes from a "mock location" app.
        putBoolean("mockDetectionSupported", true)
        putBoolean("simulationEnabled", simulationIsOn)
        putDouble("coreAbiVersion", (NativeCore.abiVersionOrNull() ?: 0).toDouble())
        putArray("reasonCodes", reasons)
      }
      promise.resolve(capabilities)
    }

    if (coordinator == null) {
      answer(false)
    } else {
      coordinator.isSimulationOn { simulationIsOn -> answer(simulationIsOn) }
    }
  }

  override fun getAuthorization(promise: Promise) {
    promise.resolve(authorization())
  }

  /**
   * Shows Android's permission dialog. This is the ONLY place where the
   * library asks the user for something, and it only runs when the app calls
   * it. Opening a session or evaluating never asks.
   */
  override fun requestLocationPermission(promise: Promise) {
    UiThreadUtil.runOnUiThread {
      val activity = reactApplicationContext.currentActivity as? PermissionAwareActivity
      if (activity == null) {
        promise.reject("no_activity", "There is no screen on which to show the permission dialog")
        return@runOnUiThread
      }

      val listener = PermissionListener { requestCode, _, _ ->
        val isOurRequest = requestCode == LOCATION_PERMISSION_REQUEST_CODE
        if (isOurRequest) {
          deviceState.locationPermissionWasAsked = true
          promise.resolve(authorization())
        }
        isOurRequest
      }

      // Both are asked together. Since Android 12 the user chooses whether to
      // give the precise location or only an approximate one.
      activity.requestPermissions(
        arrayOf(Manifest.permission.ACCESS_FINE_LOCATION, Manifest.permission.ACCESS_COARSE_LOCATION),
        LOCATION_PERMISSION_REQUEST_CODE,
        listener,
      )
    }
  }

  /** The phone activity is not supported on Android in this version: nothing to ask. */
  override fun requestMotionPermission(promise: Promise) {
    promise.resolve(authorization())
  }

  private fun authorization(): WritableMap = Arguments.createMap().apply {
    putString("location", deviceState.locationPermission().publicText)
    putString("precision", deviceState.permissionPrecision().publicText)
    putString("motion", PhoneActivitySupport.NOT_SUPPORTED.publicText)
  }

  // --- Sessions --------------------------------------------------------------

  override fun startSession(options: ReadableMap, promise: Promise) {
    val coordinator = coordinatorOrReject(promise) ?: return

    val settings = SessionSettings(
      keepCollectingReadings = options.getBoolean("keepCollectingReadings"),
      timeBetweenReadingsMs = options.getDouble("timeBetweenReadingsMs").toLong(),
      longestCollectingTimeMs = options.getDouble("longestCollectingTimeMs").toLong(),
    )

    coordinator.openSession(settings) { opened ->
      val wasOpened = opened.sessionId != 0
      // "Stopped" cannot happen right after opening; the app only needs to
      // tell "active" from "blocked" from "not requested".
      val monitoring = if (opened.collecting == CollectingState.STOPPED) {
        CollectingState.BLOCKED.publicText
      } else {
        opened.collecting.publicText
      }

      promise.resolve(
        Arguments.createMap().apply {
          putString("id", if (wasOpened) opened.sessionId.toString() else "")
          putString("monitoring", monitoring)
          putArray("reasonCodes", toArray(opened.reasons))
        },
      )
    }
  }

  override fun clearHistory(sessionId: String, promise: Promise) {
    val coordinator = coordinatorOrReject(promise) ?: return
    coordinator.clearHistory(toSessionNumber(sessionId)) { promise.resolve(null) }
  }

  override fun stopSession(sessionId: String, promise: Promise) {
    val coordinator = coordinatorOrReject(promise) ?: return
    coordinator.closeSession(toSessionNumber(sessionId)) { promise.resolve(null) }
  }

  // --- Evaluations -----------------------------------------------------------

  override fun evaluate(requestId: String, request: ReadableMap, promise: Promise) {
    val coordinator = coordinatorOrReject(promise) ?: return

    val sessionIdFromTheApp: String? =
      if (request.isNull("sessionId")) null else request.getString("sessionId")

    val settings = EvaluationSettings(
      destinationLatitude = request.getDouble("latitude"),
      destinationLongitude = request.getDouble("longitude"),
      radiusMeters = request.getDouble("radiusMeters"),
      newestAgeThatCountsAsFreshMs = request.getDouble("maxFreshAgeMs").toLong(),
      oldestAgeAllowedMs = request.getDouble("maxFallbackAgeMs").toLong(),
      worstGpsErrorAcceptedMeters = request.getDouble("maxHorizontalAccuracyMeters"),
      fastestPossibleSpeedMps = request.getDouble("maxPlausibleSpeedMps"),
      usePhoneActivity = request.getBoolean("enableMotionAssistance"),
      allowAnswerFromHistory = request.getBoolean("allowHistoricalFallback"),
      mockPolicy = if (request.getBoolean("zeroScoreForMockLocation")) {
        MockPolicy.ZERO_SCORE
      } else {
        MockPolicy.FLAG_ONLY
      },
      totalTimeAllowedMs = request.getDouble("totalDeadlineMs").toLong(),
      firstAttemptTimeMs = request.getDouble("initialFixBudgetMs").toLong(),
      extraTimeMs = request.getDouble("recoveryWindowMs").toLong(),
    )

    // No session: the engine's number for that is 0.
    val sessionNumber = if (sessionIdFromTheApp == null) 0 else toSessionNumber(sessionIdFromTheApp)

    coordinator.evaluate(
      idGivenByTheApp = requestId,
      sessionId = sessionNumber,
      settings = settings,
      onReport = { report -> promise.resolve(toMap(report, sessionIdFromTheApp)) },
      onRefused = { refusal ->
        when (refusal) {
          StartRefusal.INVALID_REQUEST ->
            promise.reject("invalid_argument", "The destination or the radius is not valid")
          StartRefusal.ENGINE_IS_FULL ->
            promise.reject("native_internal_error", "The engine could not take one more evaluation")
        }
      },
    )
  }

  override fun cancelRequest(requestId: String) {
    coordinator?.cancel(requestId)
  }

  // --- Simulation ------------------------------------------------------------

  override fun setSimulationEnabled(enabled: Boolean, promise: Promise) {
    val coordinator = coordinatorOrReject(promise) ?: return
    coordinator.setSimulationEnabled(enabled) { promise.resolve(null) }
  }

  override fun simulateReadings(readings: ReadableArray, promise: Promise) {
    val coordinator = coordinatorOrReject(promise) ?: return

    val nowMs = clock.nowMs()
    val made = ArrayList<Reading>()
    for (index in 0 until readings.size()) {
      val reading = readings.getMap(index) ?: continue
      made.add(
        Reading(
          latitude = reading.getDouble("latitude"),
          longitude = reading.getDouble("longitude"),
          accuracyMeters = reading.getDouble("accuracyMeters"),
          capturedAtMs = nowMs - reading.getDouble("ageMs").toLong(),
          onlyApproximatePermission = reading.getBoolean("approximate"),
          speedMps = if (reading.isNull("speedMps")) null else reading.getDouble("speedMps"),
          speedAccuracyMps =
            if (reading.isNull("speedAccuracyMps")) null else reading.getDouble("speedAccuracyMps"),
          mockSignal = if (reading.getBoolean("flaggedAsMock")) MockSignal.FLAGGED else MockSignal.NOT_FLAGGED,
        ),
      )
    }

    coordinator.simulateReadings(made) { promise.resolve(null) }
  }

  override fun simulatePhoneActivity(activity: Double, confidence: Double, promise: Promise) {
    val coordinator = coordinatorOrReject(promise) ?: return

    // -1 (or any number that is not an activity) means "the phone stopped reporting".
    val chosenActivity = PhoneActivity.values().firstOrNull {
      it.number == activity.toInt() && it != PhoneActivity.UNKNOWN
    }
    val chosenConfidence = ActivityConfidence.values().firstOrNull { it.number == confidence.toInt() }
      ?: ActivityConfidence.UNKNOWN

    coordinator.simulatePhoneActivity(chosenActivity, chosenConfidence) { promise.resolve(null) }
  }

  // --- Helpers ---------------------------------------------------------------

  private fun coordinatorOrReject(promise: Promise): EngineCoordinator? {
    if (coordinator == null) {
      promise.reject(
        "platform_unsupported",
        "The native library of react-native-location-confidence is not available for this " +
          "device's processor (${Build.SUPPORTED_ABIS.joinToString()})",
      )
    }
    return coordinator
  }

  /**
   * Session ids travel to JavaScript as text ("1", "2"...). A text that is
   * not a number becomes -1, which no session has, so the engine answers
   * "unknown_session".
   */
  private fun toSessionNumber(sessionId: String): Int = sessionId.toIntOrNull() ?: -1

  private fun toArray(texts: List<String>): WritableArray =
    Arguments.createArray().apply { texts.forEach { pushString(it) } }

  /** The report as the flat object described by NativeAssessment in the TypeScript spec. */
  private fun toMap(report: Report, sessionIdFromTheApp: String?): WritableMap =
    Arguments.createMap().apply {
      putDouble("transportVersion", (NativeCore.abiVersionOrNull() ?: 0).toDouble())
      if (sessionIdFromTheApp == null) putNull("sessionId") else putString("sessionId", sessionIdFromTheApp)
      putDouble("status", report.status.toDouble())
      putDouble("source", report.source.toDouble())
      putBoolean("hasPosition", report.hasPosition)
      putDouble("latitude", report.latitude)
      putDouble("longitude", report.longitude)
      putDouble("distanceMeters", report.distanceToDestinationMeters)
      putDouble("horizontalAccuracyMeters", report.gpsErrorMeters)
      putDouble("effectiveUncertaintyMeters", report.howFarPhoneCouldBeMeters)
      putDouble("locationAgeMs", report.readingAgeMs.toDouble())
      putDouble("mockSignal", report.mockSignal.toDouble())
      putDouble("radiusMeters", report.radiusMeters)
      putDouble("ageReliability", report.ageReliability.toDouble())
      putDouble("permissionPrecision", report.permissionPrecision.toDouble())
      putDouble("evidenceRelation", report.relation.toDouble())
      putDouble("qualityScore", report.qualityScore.toDouble())
      putBoolean("hasScoreBreakdown", report.hasScoreBreakdown)
      putDouble("accuracyFactor", report.accuracyFactor)
      putDouble("freshnessFactor", report.freshnessFactor)
      putDouble("movementFactor", report.movementFactor)
      putDouble("consistencyFactor", report.consistencyFactor)
      putDouble("rawScore", report.rawScore.toDouble())
      putArray("appliedCaps", toArray(report.limitsApplied))
      putDouble("motionState", report.activityNow.toDouble())
      putDouble("motionConfidence", report.activityConfidenceNow.toDouble())
      putBoolean("continuousStationaritySupported", report.standingStillSinceTheReading)
      putBoolean("hasCoverageStart", report.activityKnownUntilNow)
      putDouble("coverageStartAgeMs", report.activityKnownSinceAgeMs.toDouble())
      putBoolean("gapsDetected", report.activityHasGaps)
      putArray("reasonCodes", toArray(report.reasons))
      putArray("phaseTrace", toArray(report.steps))
      putBoolean("initialFixTimedOut", report.firstAttemptTimedOut)
      putBoolean("recoveryAttempted", report.extraTimeWasUsed)
      if (report.fallbackCause == null) putNull("fallbackCause") else putString("fallbackCause", report.fallbackCause)
      putDouble("samplesReceived", report.readingsReceived.toDouble())
      putDouble("uniqueSamplesRetained", report.readingsKept.toDouble())
      putDouble("samplesAnalyzed", report.readingsExamined.toDouble())
      putDouble("outliersFlagged", report.gpsJumpsFound.toDouble())
      putDouble("durationMs", report.durationMs.toDouble())
    }

  companion object {
    const val NAME = NativeLocationConfidenceSpec.NAME

    /** Any number works; it only has to be the same when Android answers. */
    private const val LOCATION_PERMISSION_REQUEST_CODE = 41977
  }
}
