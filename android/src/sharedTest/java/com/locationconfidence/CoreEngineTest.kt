package com.locationconfidence

import com.locationconfidence.core.CoreEngine
import com.locationconfidence.core.NativeCore
import com.locationconfidence.models.ActivityConfidence
import com.locationconfidence.models.CollectingState
import com.locationconfidence.models.LocationPermission
import com.locationconfidence.models.MockSignal
import com.locationconfidence.models.PhoneActivity
import com.locationconfidence.models.SessionSettings
import com.locationconfidence.ports.StartRefusal
import org.junit.After
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Test

/**
 * The engine through the real JNI code, on this computer.
 *
 * These tests check the BRIDGE: that every value keeps its meaning on the way
 * from Kotlin to C++ and back. The rules of the engine itself are tested in
 * C++ (cpp/tests).
 */
class CoreEngineTest {
  private lateinit var engine: CoreEngine

  @Before
  fun createTheEngine() {
    engine = CoreEngine.createOrNull() ?: error("the native library was not loaded")
  }

  @After
  fun destroyTheEngine() {
    engine.destroy()
  }

  @Test
  fun the_library_is_loaded_and_its_version_is_the_expected_one() {
    assertTrue(NativeCore.isAvailable)
    assertEquals(2, NativeCore.abiVersionOrNull())
  }

  /**
   * The same script, with the same expected numbers, as "golden scenario" in
   * cpp/tests/TestFacade.cpp and in the iOS tests. If the three agree, the
   * values cross each bridge without being changed.
   */
  @Test
  fun golden_scenario_one_request_one_reading_every_field_of_the_report() {
    val device = phoneWhereEverythingWorks()

    // second 0: the app asks.
    val started = engine.startRequest(0, usualRequest(), device, 0)
    assertNull(started.refusal)
    assertEquals(1, started.requestId)

    val work = engine.whatThePlatformMustDoNow()
    assertTrue(work.locationUpdatesAreWanted)
    assertEquals(1000, work.timeBetweenLocationUpdatesMs)
    assertTrue(work.phoneActivityUpdatesAreWanted)
    assertTrue(work.wakeUpIsWanted)
    assertEquals(4500, work.wakeUpAtMs)
    assertNull(engine.takeFinishedReport())

    // second 2: a reading taken at second 1, 20 meters from the address,
    // with a GPS error of 8 meters.
    val reading = readingNorth(1000, 20.0, 8.0).copy(mockSignal = MockSignal.NOT_FLAGGED)
    engine.readingsArrived(listOf(reading), 2000)

    val report = engine.takeFinishedReport() ?: error("no report")
    assertNull(engine.takeFinishedReport())

    assertEquals(1, report.requestId)
    assertEquals(0, report.sessionId)
    assertEquals(0, report.status) // available
    assertEquals(0, report.source) // fresh

    assertTrue(report.hasPosition)
    assertEquals(reading.latitude, report.latitude, 0.0)
    assertEquals(0.0, report.longitude, 0.0)
    assertEquals(20.0, report.distanceToDestinationMeters, 1e-7)
    assertEquals(8.0, report.gpsErrorMeters, 0.0)
    assertEquals(53.0, report.howFarPhoneCouldBeMeters, 0.0) // 8 + 45 x 1 second
    assertEquals(1000, report.readingAgeMs)
    assertEquals(1, report.mockSignal) // not flagged

    assertEquals(100.0, report.radiusMeters, 0.0)
    assertEquals(1, report.ageReliability) // monotonic
    assertEquals(0, report.permissionPrecision) // precise
    assertEquals(0, report.relation) // supports inside

    assertEquals(75, report.qualityScore)
    assertTrue(report.hasScoreBreakdown)
    assertEquals(0.92, report.accuracyFactor, 1e-12)
    assertEquals(59.0 / 60.0, report.freshnessFactor, 1e-12)
    assertEquals(0.55, report.movementFactor, 1e-12)
    assertEquals(0.60, report.consistencyFactor, 1e-12)
    assertEquals(81, report.rawScore)
    assertEquals(listOf("single_fix"), report.limitsApplied)

    assertEquals(5, report.activityNow) // unknown
    assertEquals(3, report.activityConfidenceNow) // unknown
    assertFalse(report.standingStillSinceTheReading)
    assertFalse(report.activityKnownUntilNow)
    assertEquals(0, report.activityKnownSinceAgeMs)
    assertFalse(report.activityHasGaps)

    assertEquals(listOf("uncertainty_model_heuristic"), report.reasons)
    assertEquals(listOf("started", "first_attempt", "finished"), report.steps)
    assertFalse(report.firstAttemptTimedOut)
    assertFalse(report.extraTimeWasUsed)
    assertNull(report.fallbackCause)

    assertEquals(1, report.readingsReceived)
    assertEquals(1, report.readingsKept)
    assertEquals(1, report.readingsExamined)
    assertEquals(0, report.gpsJumpsFound)
    assertEquals(2000, report.durationMs)

    val after = engine.whatThePlatformMustDoNow()
    assertFalse(after.locationUpdatesAreWanted)
    assertFalse(after.phoneActivityUpdatesAreWanted)
    assertFalse(after.wakeUpIsWanted)
  }

  @Test
  fun a_request_that_times_out_carries_its_flags_its_steps_and_its_fallback_cause() {
    engine.startRequest(0, usualRequest(), phoneWhereEverythingWorks(), 1000)
    engine.timePassed(5500)
    engine.timePassed(12500)

    val report = engine.takeFinishedReport() ?: error("no report")
    assertEquals(2, report.status) // unavailable
    assertEquals(3, report.source) // unavailable
    assertFalse(report.hasPosition)
    assertEquals(0, report.qualityScore)
    assertFalse(report.hasScoreBreakdown)
    assertEquals(0, report.ageReliability) // not applicable
    assertTrue(report.firstAttemptTimedOut)
    assertTrue(report.extraTimeWasUsed)
    assertEquals("initial_fix_timeout", report.fallbackCause)
    assertEquals(11500, report.durationMs)
    assertEquals(listOf("no_candidate"), report.limitsApplied)
    assertEquals(
      listOf("started", "first_attempt", "extra_time", "answer_from_history", "finished"),
      report.steps,
    )
    assertTrue(report.reasons.containsAll(listOf("initial_fix_timeout", "recovery_window_timeout", "no_usable_fix", "history_empty")))
  }

  @Test
  fun every_optional_value_of_a_reading_crosses_and_a_missing_one_stays_missing() {
    val device = phoneWhereEverythingWorks()

    // A speed with its error makes the distance small: (0.3 + 2 x 0.5) x 1 + 1.25 = 2.55.
    engine.startRequest(0, usualRequest(), device, 0)
    val withSpeed = readingNorth(0, 20.0).copy(speedMps = 0.3, speedAccuracyMps = 0.5, headingDegrees = 90.0)
    engine.readingsArrived(listOf(withSpeed), 1000)
    assertEquals(8.0 + 2.55, engine.takeFinishedReport()!!.howFarPhoneCouldBeMeters, 1e-9)

    // No speed: the fastest speed is assumed, 8 + 45 = 53.
    engine.startRequest(0, usualRequest(), device, 0)
    engine.readingsArrived(listOf(readingNorth(0, 20.0)), 1000)
    assertEquals(53.0, engine.takeFinishedReport()!!.howFarPhoneCouldBeMeters, 0.0)

    // No GPS error: the reading is thrown away.
    engine.startRequest(0, usualRequest(), device, 0)
    engine.readingsArrived(listOf(readingNorth(0, 20.0).copy(accuracyMeters = null)), 1000)
    engine.timePassed(30000)
    val thrownAway = engine.takeFinishedReport()!!
    assertFalse(thrownAway.hasPosition)
    assertTrue(thrownAway.reasons.contains("location_accuracy_unavailable"))

    // The marks: simulated, approximate, estimated time.
    engine.startRequest(0, usualRequest(), device, 0)
    val marked = readingNorth(0, 20.0).copy(
      mockSignal = MockSignal.FLAGGED,
      onlyApproximatePermission = true,
      captureTimeWasEstimated = true,
    )
    engine.readingsArrived(listOf(marked), 1000)
    engine.timePassed(30000)
    val markedReport = engine.takeFinishedReport()!!
    assertEquals(2, markedReport.mockSignal) // flagged
    assertEquals(2, markedReport.ageReliability) // estimated
    assertEquals(0, markedReport.qualityScore)
    assertTrue(markedReport.reasons.containsAll(listOf("possible_mock_location", "precision_reduced")))
  }

  @Test
  fun a_long_delivery_crosses_in_one_call_and_every_reading_is_counted() {
    val strict = usualRequest().copy(worstGpsErrorAcceptedMeters = 1.0) // keeps it running
    engine.startRequest(0, strict, phoneWhereEverythingWorks(), 0)

    val readings = (0 until 40).map { readingNorth(it * 100L, 20.0) }
    engine.readingsArrived(readings, 4000)
    engine.timePassed(12000)

    val report = engine.takeFinishedReport()!!
    assertEquals(40, report.readingsReceived)
    assertEquals(40, report.readingsKept)
    assertEquals(10, report.readingsExamined)
  }

  @Test
  fun the_state_of_the_phone_crosses() {
    val denied = phoneWhereEverythingWorks().copy(locationPermission = LocationPermission.DENIED)
    engine.startRequest(0, usualRequest(), denied, 0)

    val report = engine.takeFinishedReport()!!
    assertEquals(2, report.status)
    assertTrue(report.reasons.contains("permission_denied"))
    assertFalse(engine.whatThePlatformMustDoNow().locationUpdatesAreWanted)
  }

  @Test
  fun a_session_is_opened_collects_and_is_closed() {
    val device = phoneWhereEverythingWorks()
    val settings = SessionSettings(keepCollectingReadings = true, timeBetweenReadingsMs = 5000, longestCollectingTimeMs = 900000)

    val opened = engine.openSession(settings, device, 0)
    assertEquals(1, opened.sessionId)
    assertEquals(CollectingState.ACTIVE, opened.collecting)
    assertTrue(opened.reasons.isEmpty())

    val work = engine.whatThePlatformMustDoNow()
    assertTrue(work.locationUpdatesAreWanted)
    assertEquals(5000, work.timeBetweenLocationUpdatesMs)
    assertEquals(900000, work.wakeUpAtMs)

    assertTrue(engine.clearSessionHistory(opened.sessionId))
    assertFalse(engine.clearSessionHistory(999))

    engine.closeSession(opened.sessionId, 1000)
    assertFalse(engine.whatThePlatformMustDoNow().locationUpdatesAreWanted)

    // Blocked collecting says why.
    val blocked = engine.openSession(settings, device.copy(locationIsTurnedOn = false), 2000)
    assertEquals(CollectingState.BLOCKED, blocked.collecting)
    assertEquals(listOf("location_services_disabled"), blocked.reasons)
  }

  @Test
  fun the_phone_activity_crosses() {
    val device = phoneWhereEverythingWorks()
    val session = engine.openSession(
      SessionSettings(keepCollectingReadings = true, timeBetweenReadingsMs = 5000, longestCollectingTimeMs = 900000),
      device,
      0,
    )
    engine.readingsArrived(listOf(readingNorth(0, 20.0, 5.0)), 0)
    engine.phoneActivityChanged(PhoneActivity.STANDING_STILL, ActivityConfidence.HIGH, 0, 500)

    engine.startRequest(session.sessionId, usualRequest(), device, 10000)
    engine.timePassed(14500)

    val report = engine.takeFinishedReport()!!
    assertEquals(0, report.activityNow) // standing still
    assertEquals(0, report.activityConfidenceNow) // high
    assertTrue(report.standingStillSinceTheReading)
    assertTrue(report.activityKnownUntilNow)
    assertEquals(14500, report.activityKnownSinceAgeMs)
    assertEquals(5.0 + 0.5 * 14.5, report.howFarPhoneCouldBeMeters, 1e-9)
    assertEquals(2, report.source) // cached
  }

  @Test
  fun a_request_that_makes_no_sense_is_refused() {
    val broken = usualRequest().copy(destinationLatitude = 91.0)
    val started = engine.startRequest(0, broken, phoneWhereEverythingWorks(), 0)

    assertEquals(StartRefusal.INVALID_REQUEST, started.refusal)
    assertNull(engine.takeFinishedReport())
    assertFalse(engine.whatThePlatformMustDoNow().locationUpdatesAreWanted)
  }

  @Test
  fun the_reasons_above_number_31_cross_too() {
    // "simulated_location_source" is reason number 43: it travels in the
    // upper half of the 64 bits.
    engine.setReadingsAreSimulated(true)
    engine.startRequest(0, usualRequest(), phoneWhereEverythingWorks(), 0)
    engine.readingsArrived(listOf(readingNorth(0, 20.0)), 1000)

    val report = engine.takeFinishedReport()!!
    assertTrue(report.reasons.contains("simulated_location_source"))
    assertTrue(report.reasons.contains("uncertainty_model_heuristic"))
  }

  @Test
  fun CON_025_a_thousand_requests_through_JNI_leave_nothing_behind() {
    val device = phoneWhereEverythingWorks()
    for (round in 0 until 1000) {
      val now = round * 20000L
      engine.startRequest(0, usualRequest(), device, now)
      engine.readingsArrived(listOf(readingNorth(now, 20.0)), now + 1000)
      assertNotNull(engine.takeFinishedReport())
      assertNull(engine.takeFinishedReport())
    }
    assertFalse(engine.whatThePlatformMustDoNow().locationUpdatesAreWanted)
  }

  @Test
  fun after_destroy_calls_are_answered_and_nothing_crashes() {
    engine.destroy()
    engine.destroy() // twice

    engine.timePassed(1000)
    assertNull(engine.takeFinishedReport())
    assertFalse(engine.whatThePlatformMustDoNow().locationUpdatesAreWanted)
    assertEquals(StartRefusal.ENGINE_IS_FULL, engine.startRequest(0, usualRequest(), phoneWhereEverythingWorks(), 0).refusal)
  }
}
