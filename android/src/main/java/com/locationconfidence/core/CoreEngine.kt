package com.locationconfidence.core

import com.locationconfidence.models.ActivityConfidence
import com.locationconfidence.models.CollectingState
import com.locationconfidence.models.DeviceState
import com.locationconfidence.models.RequestSettings
import com.locationconfidence.models.PhoneActivity
import com.locationconfidence.models.PlatformWork
import com.locationconfidence.models.Reading
import com.locationconfidence.models.Report
import com.locationconfidence.models.SessionOpened
import com.locationconfidence.models.SessionSettings
import com.locationconfidence.ports.CoreEnginePort
import com.locationconfidence.ports.RequestStart
import com.locationconfidence.ports.StartRefusal

/**
 * The C++ engine, wrapped so the rest of the Kotlin code can use plain
 * Kotlin objects.
 *
 * The engine's front door works with C structs, and Kotlin has none. So each
 * struct crosses JNI as a LIST OF NUMBERS (a DoubleArray). This class does
 * the Kotlin half of that:
 *
 *     Kotlin object  ->  list of numbers  ->  JNI  ->  C struct  ->  engine
 *     Kotlin object  <-  list of numbers  <-  JNI  <-  C struct  <-  engine
 *
 * THE ORDER OF THE NUMBERS IS THE CONTRACT. Each `write...` function below
 * puts the numbers in exactly the order that the matching `read...` function
 * of android/src/main/cpp/JniBindings.cpp takes them out, and each `read...`
 * function here takes them out in the order `write...` puts them there.
 * Change one side and you must change the other.
 *
 * Every list that comes back ends with one fixed number, LAYOUT_CHECK. If
 * the two sides ever get out of step, that number is not where it should be
 * and [NumberReader.finish] fails loudly instead of handing out wrong values.
 *
 * This class must be used from one thread at a time. The coordinator does
 * that by going through its serial queue.
 */
internal class CoreEngine private constructor(private var engine: Long) : CoreEnginePort {

  companion object {
    /** Must be the same number as LAYOUT_CHECK in JniBindings.cpp. */
    private const val LAYOUT_CHECK = 7741.0

    /** How many numbers one reading takes (NUMBERS_IN_ONE_READING in JniBindings.cpp). */
    private const val NUMBERS_IN_ONE_READING = 16

    /** How many steps a report always carries (LCE_MAX_STEPS_IN_REPORT in lce.h). */
    private const val STEPS_IN_REPORT = 8

    // Error codes of lce.h, as they come back from nativeStartRequest.
    private const val LCE_ERROR_INVALID_REQUEST = 2

    /**
     * Creates an engine, or returns null when the native library could not be
     * loaded for this phone's processor or there is no memory.
     */
    fun createOrNull(): CoreEngine? {
      if (!NativeCore.isAvailable) {
        return null
      }
      val engine = NativeCore.nativeCreateEngine()
      return if (engine == 0L) null else CoreEngine(engine)
    }
  }

  // The public texts of the reasons, the score limits and the steps. They are
  // asked from the engine once, so Android always shows the engine's own texts.
  private val reasonTexts: Array<String> = NativeCore.nativeTexts(0) ?: emptyArray()
  private val scoreLimitTexts: Array<String> = NativeCore.nativeTexts(1) ?: emptyArray()
  private val stepTexts: Array<String> = NativeCore.nativeTexts(2) ?: emptyArray()

  // --- Sessions ----------------------------------------------------------------

  override fun openSession(settings: SessionSettings, device: DeviceState, nowMs: Long): SessionOpened {
    val numbers = NumberWriter()
    numbers.yesOrNo(settings.keepCollectingReadings)
    numbers.whole(settings.timeBetweenReadingsMs)
    numbers.whole(settings.longestCollectingTimeMs)
    writeDeviceState(device, numbers)

    val answer = NativeCore.nativeOpenSession(engine, numbers.toArray(), nowMs)
      ?: return SessionOpened(0, CollectingState.NOT_REQUESTED, listOf("native_internal_error"))

    val reader = NumberReader(answer)
    val sessionId = reader.whole()
    val collectingNumber = reader.whole()
    val reasons = textsOfBits(reader.bits64(), reasonTexts)
    reader.finish()

    val collecting = CollectingState.values().firstOrNull { it.number == collectingNumber }
      ?: CollectingState.NOT_REQUESTED
    return SessionOpened(sessionId, collecting, reasons)
  }

  override fun clearSessionHistory(sessionId: Int): Boolean =
    NativeCore.nativeClearSessionHistory(engine, sessionId) == 0

  override fun closeSession(sessionId: Int, nowMs: Long) {
    NativeCore.nativeCloseSession(engine, sessionId, nowMs)
  }

  // --- Requests -------------------------------------------------------------

  override fun startRequest(
    sessionId: Int,
    settings: RequestSettings,
    device: DeviceState,
    nowMs: Long,
  ): RequestStart {
    val numbers = NumberWriter()
    numbers.number(settings.destinationLatitude)
    numbers.number(settings.destinationLongitude)
    numbers.number(settings.radiusMeters)
    numbers.whole(settings.newestAgeThatCountsAsFreshMs)
    numbers.whole(settings.oldestAgeAllowedMs)
    numbers.number(settings.worstGpsErrorAcceptedMeters)
    numbers.number(settings.fastestPossibleSpeedMps)
    numbers.yesOrNo(settings.usePhoneActivity)
    numbers.yesOrNo(settings.allowAnswerFromHistory)
    numbers.whole(settings.mockPolicy.number.toLong())
    numbers.whole(settings.totalTimeAllowedMs)
    numbers.whole(settings.firstAttemptTimeMs)
    numbers.whole(settings.extraTimeMs)
    writeDeviceState(device, numbers)

    // A positive answer is the request id; a negative one is minus an error code.
    val answer = NativeCore.nativeStartRequest(engine, sessionId, numbers.toArray(), nowMs)

    return when {
      answer > 0 -> RequestStart(answer.toInt(), null)
      answer == -LCE_ERROR_INVALID_REQUEST.toLong() -> RequestStart(0, StartRefusal.INVALID_REQUEST)
      else -> RequestStart(0, StartRefusal.ENGINE_IS_FULL)
    }
  }

  override fun cancelRequest(requestId: Int, nowMs: Long) {
    NativeCore.nativeCancelRequest(engine, requestId, nowMs)
  }

  // --- Things that happen ------------------------------------------------------

  override fun readingsArrived(readings: List<Reading>, nowMs: Long) {
    if (readings.isEmpty()) {
      return
    }

    // All the readings of one delivery go in ONE list and ONE call. Crossing
    // JNI once per reading would be wasteful.
    val numbers = NumberWriter()
    for (reading in readings) {
      numbers.number(reading.latitude)
      numbers.number(reading.longitude)
      numbers.yesOrNo(reading.accuracyMeters != null)
      numbers.number(reading.accuracyMeters ?: 0.0)
      numbers.whole(reading.capturedAtMs)
      numbers.yesOrNo(reading.timeIsReliable)
      numbers.yesOrNo(reading.captureTimeWasEstimated)
      numbers.yesOrNo(reading.onlyApproximatePermission)
      numbers.yesOrNo(reading.speedMps != null)
      numbers.number(reading.speedMps ?: 0.0)
      numbers.yesOrNo(reading.speedAccuracyMps != null)
      numbers.number(reading.speedAccuracyMps ?: 0.0)
      numbers.yesOrNo(reading.headingDegrees != null)
      numbers.number(reading.headingDegrees ?: 0.0)
      numbers.whole(reading.provider.number.toLong())
      numbers.whole(reading.mockSignal.number.toLong())
    }
    check(numbers.size == readings.size * NUMBERS_IN_ONE_READING) {
      "a reading must take exactly $NUMBERS_IN_ONE_READING numbers"
    }

    NativeCore.nativeReadingsArrived(engine, numbers.toArray(), readings.size, nowMs)
  }

  override fun phoneActivityChanged(
    activity: PhoneActivity,
    confidence: ActivityConfidence,
    sinceMs: Long,
    nowMs: Long,
  ) {
    NativeCore.nativePhoneActivityChanged(engine, activity.number, confidence.number, sinceMs, nowMs)
  }

  override fun phoneActivityUpdatesStopped(nowMs: Long) {
    NativeCore.nativePhoneActivityUpdatesStopped(engine, nowMs)
  }

  override fun timePassed(nowMs: Long) {
    NativeCore.nativeTimePassed(engine, nowMs)
  }

  override fun appWentToBackground(nowMs: Long) {
    NativeCore.nativeAppWentToBackground(engine, nowMs)
  }

  override fun locationPermissionWasLost(nowMs: Long) {
    NativeCore.nativeLocationPermissionWasLost(engine, nowMs)
  }

  override fun locationProviderFailed(nowMs: Long) {
    NativeCore.nativeLocationProviderFailed(engine, nowMs)
  }

  override fun setReadingsAreSimulated(simulated: Boolean) {
    NativeCore.nativeSetReadingsAreSimulated(engine, simulated)
  }

  // --- What the platform asks after every call ---------------------------------

  override fun whatThePlatformMustDoNow(): PlatformWork {
    val answer = NativeCore.nativeWhatThePlatformMustDoNow(engine)
      ?: return PlatformWork(false, 1000, false, false, 0)

    val reader = NumberReader(answer)
    val work = PlatformWork(
      locationUpdatesAreWanted = reader.yesOrNo(),
      timeBetweenLocationUpdatesMs = reader.wholeLong(),
      phoneActivityUpdatesAreWanted = reader.yesOrNo(),
      wakeUpIsWanted = reader.yesOrNo(),
      wakeUpAtMs = reader.wholeLong(),
    )
    reader.finish()
    return work
  }

  override fun takeFinishedReport(): Report? {
    val answer = NativeCore.nativeTakeFinishedReport(engine) ?: return null
    val reader = NumberReader(answer)

    // The same order as writeReport() in JniBindings.cpp.
    val requestId = reader.whole()
    val sessionId = reader.whole()
    val status = reader.whole()
    val source = reader.whole()

    val hasPosition = reader.yesOrNo()
    val latitude = reader.number()
    val longitude = reader.number()
    val distanceToDestinationMeters = reader.number()
    val gpsErrorMeters = reader.number()
    val howFarPhoneCouldBeMeters = reader.number()
    val readingAgeMs = reader.wholeLong()
    val mockSignal = reader.whole()

    val radiusMeters = reader.number()
    val ageReliability = reader.whole()
    val permissionPrecision = reader.whole()
    val relation = reader.whole()

    val qualityScore = reader.whole()
    val hasScoreBreakdown = reader.yesOrNo()
    val accuracyFactor = reader.number()
    val freshnessFactor = reader.number()
    val movementFactor = reader.number()
    val consistencyFactor = reader.number()
    val rawScore = reader.whole()
    val limitsApplied = textsOfBits(reader.wholeLong(), scoreLimitTexts)

    val activityNow = reader.whole()
    val activityConfidenceNow = reader.whole()
    val standingStillSinceTheReading = reader.yesOrNo()
    val activityKnownUntilNow = reader.yesOrNo()
    val activityKnownSinceAgeMs = reader.wholeLong()
    val activityHasGaps = reader.yesOrNo()

    val reasons = textsOfBits(reader.bits64(), reasonTexts)

    // The report always carries 8 step numbers; only the first ones are real.
    val howManySteps = reader.whole()
    val steps = ArrayList<String>()
    for (position in 0 until STEPS_IN_REPORT) {
      val stepNumber = reader.whole()
      if (position < howManySteps) {
        steps.add(stepTexts.getOrElse(stepNumber) { "unknown" })
      }
    }

    val firstAttemptTimedOut = reader.yesOrNo()
    val extraTimeWasUsed = reader.yesOrNo()
    val fallbackCauseNumber = reader.whole() // -1 when there was none
    val fallbackCause = if (fallbackCauseNumber >= 0) reasonTexts.getOrNull(fallbackCauseNumber) else null

    val readingsReceived = reader.whole()
    val readingsKept = reader.whole()
    val readingsExamined = reader.whole()
    val gpsJumpsFound = reader.whole()
    val durationMs = reader.wholeLong()
    reader.finish()

    return Report(
      requestId = requestId,
      sessionId = sessionId,
      status = status,
      source = source,
      hasPosition = hasPosition,
      latitude = latitude,
      longitude = longitude,
      distanceToDestinationMeters = distanceToDestinationMeters,
      gpsErrorMeters = gpsErrorMeters,
      howFarPhoneCouldBeMeters = howFarPhoneCouldBeMeters,
      readingAgeMs = readingAgeMs,
      mockSignal = mockSignal,
      radiusMeters = radiusMeters,
      ageReliability = ageReliability,
      permissionPrecision = permissionPrecision,
      relation = relation,
      qualityScore = qualityScore,
      hasScoreBreakdown = hasScoreBreakdown,
      accuracyFactor = accuracyFactor,
      freshnessFactor = freshnessFactor,
      movementFactor = movementFactor,
      consistencyFactor = consistencyFactor,
      rawScore = rawScore,
      limitsApplied = limitsApplied,
      activityNow = activityNow,
      activityConfidenceNow = activityConfidenceNow,
      standingStillSinceTheReading = standingStillSinceTheReading,
      activityKnownUntilNow = activityKnownUntilNow,
      activityKnownSinceAgeMs = activityKnownSinceAgeMs,
      activityHasGaps = activityHasGaps,
      reasons = reasons,
      steps = steps,
      firstAttemptTimedOut = firstAttemptTimedOut,
      extraTimeWasUsed = extraTimeWasUsed,
      fallbackCause = fallbackCause,
      readingsReceived = readingsReceived,
      readingsKept = readingsKept,
      readingsExamined = readingsExamined,
      gpsJumpsFound = gpsJumpsFound,
      durationMs = durationMs,
    )
  }

  override fun destroy() {
    if (engine != 0L) {
      NativeCore.nativeDestroyEngine(engine)
      // The address is forgotten, so a mistake after this point calls the
      // engine with 0, which the front door answers with an error code
      // instead of touching memory that is gone.
      engine = 0L
    }
  }

  // --- Helpers -----------------------------------------------------------------

  private fun writeDeviceState(device: DeviceState, numbers: NumberWriter) {
    numbers.whole(device.locationPermission.number.toLong())
    numbers.whole(device.permissionPrecision.number.toLong())
    numbers.yesOrNo(device.locationIsTurnedOn)
    numbers.whole(device.locationProvider.number.toLong())
    numbers.yesOrNo(device.appIsInForeground)
    numbers.whole(device.phoneActivity.number.toLong())
  }

  /**
   * Turns yes/no bits into a list of texts: when bit N is set, the text
   * number N is in the list.
   */
  private fun textsOfBits(bits: Long, texts: Array<String>): List<String> {
    val result = ArrayList<String>()
    for (number in texts.indices) {
      val bitIsSet = (bits ushr number) and 1L == 1L
      if (bitIsSet) {
        result.add(texts[number])
      }
    }
    return result
  }

  /** Collects numbers one after the other for a list that goes to C++. */
  private class NumberWriter {
    private val numbers = ArrayList<Double>()

    val size: Int
      get() = numbers.size

    fun number(value: Double) {
      numbers.add(value)
    }

    fun whole(value: Long) {
      numbers.add(value.toDouble())
    }

    fun yesOrNo(value: Boolean) {
      numbers.add(if (value) 1.0 else 0.0)
    }

    fun toArray(): DoubleArray = numbers.toDoubleArray()
  }

  /** Reads numbers one after the other from a list that came from C++. */
  private class NumberReader(private val numbers: DoubleArray) {
    private var position = 0

    fun number(): Double {
      val value = numbers[position]
      position += 1
      return value
    }

    fun whole(): Int = number().toInt()

    fun wholeLong(): Long = number().toLong()

    fun yesOrNo(): Boolean = number() != 0.0

    /** 64 yes/no bits that crossed as two numbers: the lower 32 bits, then the upper 32. */
    fun bits64(): Long {
      val lower = number().toLong()
      val upper = number().toLong()
      return (upper shl 32) or lower
    }

    /** Checks that everything was read and that the list ends with the check number. */
    fun finish() {
      val checkIsThere = position == numbers.size - 1 && numbers[position] == LAYOUT_CHECK
      check(checkIsThere) {
        "the list of numbers from the engine does not have the expected layout: " +
          "CoreEngine.kt and JniBindings.cpp are out of step"
      }
    }
  }
}
