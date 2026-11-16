package com.locationconfidence

import com.locationconfidence.adapters.SingleThreadQueue
import com.locationconfidence.application.EngineCoordinator
import com.locationconfidence.core.CoreEngine
import com.locationconfidence.models.ActivityConfidence
import com.locationconfidence.models.LocationPermission
import com.locationconfidence.models.PhoneActivity
import com.locationconfidence.models.Report
import com.locationconfidence.models.SessionOpened
import com.locationconfidence.models.SessionSettings
import com.locationconfidence.ports.Clock
import com.locationconfidence.ports.StartRefusal
import java.util.concurrent.ConcurrentHashMap
import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit
import java.util.concurrent.atomic.AtomicInteger
import org.junit.After
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Test

/**
 * The coordinator with the REAL engine (through JNI, built for this
 * computer) and pretend pieces around it: a GPS that delivers what the test
 * says, a clock the test moves by hand, a queue with no thread.
 *
 * What is checked here is the part Kotlin is responsible for:
 *   - the GPS is turned on and off exactly when the engine says;
 *   - nothing is left running after an evaluation ends, however it ends;
 *   - each evaluation gets exactly one answer;
 *   - a delivery that arrives too late changes nothing.
 */
class EngineCoordinatorTest {
  private val clock = FakeClock()
  private val queue = ManualQueue(clock)
  private val gps = FakeLocationSource()
  private val activitySensor = FakePhoneActivitySource()
  private val device = FakeDeviceState()
  private lateinit var coordinator: EngineCoordinator

  /** Every report that came back, with the id the app gave. */
  private val reports = ArrayList<Pair<String, Report>>()
  private val refusals = ArrayList<StartRefusal>()

  @Before
  fun buildTheCoordinator() {
    val engine = CoreEngine.createOrNull() ?: error("the native library was not loaded")
    coordinator = EngineCoordinator(engine, queue, clock, gps, activitySensor, device)
  }

  @After
  fun shutDown() {
    coordinator.shutDown()
  }

  private fun evaluate(appId: String, sessionId: Int = 0, settings: com.locationconfidence.models.EvaluationSettings = usualEvaluation()) {
    coordinator.evaluate(appId, sessionId, settings, { reports.add(appId to it) }, { refusals.add(it) })
  }

  private fun reportOf(appId: String): Report =
    reports.firstOrNull { it.first == appId }?.second ?: error("no report for $appId")

  private fun howManyReportsOf(appId: String): Int = reports.count { it.first == appId }

  private fun openSession(keepCollecting: Boolean, timeBetweenMs: Long = 5000): SessionOpened {
    var opened: SessionOpened? = null
    coordinator.openSession(SessionSettings(keepCollecting, timeBetweenMs, 900000)) { opened = it }
    return opened ?: error("the session was not opened")
  }

  /** Nothing is left running: no GPS, no activity updates, no timer. */
  private fun everythingIsQuiet(): Boolean =
    !gps.isOn && !activitySensor.isOn && queue.howManyTimersAreSet == 0

  // --- One evaluation ----------------------------------------------------------

  @Test
  fun REC_001_the_GPS_goes_on_a_good_reading_arrives_everything_goes_off() {
    assertTrue(everythingIsQuiet())

    evaluate("a")

    assertTrue(gps.isOn)
    assertEquals(1000, gps.timeBetweenReadingsMs)
    assertTrue(activitySensor.isOn)
    assertEquals(1, queue.howManyTimersAreSet)
    assertTrue(reports.isEmpty())

    clock.now = 1200
    assertTrue(gps.deliver(readingNorth(1200, 20.0)))

    assertEquals(1, reports.size)
    val report = reportOf("a")
    assertEquals(0, report.status) // available
    assertEquals(0, report.source) // fresh
    assertEquals(1200, report.durationMs)

    assertTrue(everythingIsQuiet())
    assertEquals(1, gps.timesStarted)
    assertEquals(1, gps.timesStopped)
  }

  @Test
  fun REC_005_REC_011_with_no_reading_the_timers_alone_end_the_evaluation() {
    evaluate("a")

    queue.advanceTo(4500) // the first attempt is over
    assertTrue(reports.isEmpty())
    assertTrue(gps.isOn)
    assertEquals(1, queue.howManyTimersAreSet)

    queue.advanceTo(11500) // the extra time is over
    assertEquals(1, reports.size)
    assertEquals(2, reportOf("a").status) // unavailable
    assertEquals(11500, reportOf("a").durationMs)
    assertTrue(reportOf("a").reasons.containsAll(listOf("initial_fix_timeout", "recovery_window_timeout")))

    assertTrue(everythingIsQuiet())
  }

  @Test
  fun CON_002_CON_006_cancelling_ends_it_once_and_cancelling_again_does_nothing() {
    evaluate("a")
    clock.now = 2000

    coordinator.cancel("a")

    assertEquals(1, reports.size)
    assertEquals(3, reportOf("a").status) // cancelled
    assertTrue(reportOf("a").reasons.contains("request_cancelled"))
    assertTrue(everythingIsQuiet())

    coordinator.cancel("a")
    coordinator.cancel("an id nobody gave")
    assertEquals(1, reports.size)
  }

  @Test
  fun CON_018_PLT_015_a_delivery_that_arrives_after_the_GPS_was_turned_off_is_ignored() {
    evaluate("a")
    val listenerOfTheStoppedGps = gps.lastListener!!
    clock.now = 1000
    gps.deliver(readingNorth(1000, 20.0))
    assertEquals(1, reports.size)
    assertFalse(gps.isOn)

    // The system still had something on its way.
    listenerOfTheStoppedGps.onReadings(listOf(readingNorth(1500, 20.0)))
    listenerOfTheStoppedGps.onReadings(listOf(readingNorth(1500, 20.0)))
    listenerOfTheStoppedGps.onFailure()

    assertEquals(1, reports.size)
    assertTrue(everythingIsQuiet())

    // And it does not leak into the next evaluation either.
    evaluate("b")
    listenerOfTheStoppedGps.onReadings(listOf(readingNorth(1600, 20.0)))
    assertEquals(0, howManyReportsOf("b"))
    assertTrue(gps.isOn)
  }

  @Test
  fun CON_019_CON_020_a_timer_and_a_reading_that_race_give_one_answer() {
    evaluate("a")
    val listener = gps.lastListener!!
    queue.advanceTo(4500)
    queue.advanceTo(11500)
    assertEquals(1, reports.size)

    listener.onReadings(listOf(readingNorth(11600, 20.0))) // too late
    queue.advanceTo(60000)

    assertEquals(1, reports.size)
    assertEquals(2, reportOf("a").status)
  }

  @Test
  fun GEO_012_a_request_that_makes_no_sense_is_refused_and_turns_nothing_on() {
    evaluate("a", settings = usualEvaluation().copy(destinationLatitude = 91.0))

    assertEquals(listOf(StartRefusal.INVALID_REQUEST), refusals)
    assertTrue(reports.isEmpty())
    assertEquals(0, gps.timesStarted)
    assertTrue(everythingIsQuiet())
  }

  // --- Several evaluations --------------------------------------------------------

  @Test
  fun CON_013_two_evaluations_share_one_GPS_and_cancelling_one_keeps_it_on() {
    evaluate("a")
    evaluate("b")
    assertEquals(1, gps.timesStarted)

    coordinator.cancel("a")
    assertEquals(3, reportOf("a").status)
    assertTrue(gps.isOn)
    assertEquals(0, gps.timesStopped)

    clock.now = 1000
    gps.deliver(readingNorth(1000, 20.0))
    assertEquals(0, reportOf("b").status)
    assertEquals(1, howManyReportsOf("a"))
    assertEquals(1, howManyReportsOf("b"))
    assertTrue(everythingIsQuiet())
    assertEquals(1, gps.timesStopped)
  }

  @Test
  fun CON_012_two_evaluations_with_different_addresses_get_different_answers() {
    evaluate("near")
    evaluate("far", settings = usualEvaluation().copy(destinationLongitude = 0.01, radiusMeters = 50.0))

    clock.now = 1000
    gps.deliver(readingNorth(1000, 20.0))

    assertEquals(0, reportOf("near").relation) // supports inside
    assertEquals(100.0, reportOf("near").radiusMeters, 0.0)
    assertEquals(1, reportOf("far").relation) // supports outside
    assertEquals(50.0, reportOf("far").radiusMeters, 0.0)
  }

  @Test
  fun CON_016_a_hundred_evaluations_cancelled_one_answer_each_and_nothing_left_on() {
    for (round in 0 until 100) {
      val id = "request-$round"
      evaluate(id)
      clock.now += 300
      coordinator.cancel(id)
      gps.lastListener!!.onReadings(listOf(readingNorth(clock.now, 20.0))) // late
      assertEquals(1, howManyReportsOf(id))
      assertTrue(everythingIsQuiet())
    }
    assertEquals(100, reports.size)
    assertEquals(100, gps.timesStarted)
    assertEquals(100, gps.timesStopped)
  }

  // --- The state of the phone ------------------------------------------------------

  @Test
  fun PLT_003_PLT_005_PLT_009_when_the_GPS_cannot_be_used_it_is_never_turned_on() {
    val broken = listOf(
      phoneWhereEverythingWorks().copy(locationPermission = LocationPermission.DENIED) to "permission_denied",
      phoneWhereEverythingWorks().copy(locationPermission = LocationPermission.NOT_DETERMINED) to "permission_not_determined",
      phoneWhereEverythingWorks().copy(locationIsTurnedOn = false) to "location_services_disabled",
      phoneWhereEverythingWorks().copy(
        locationProvider = com.locationconfidence.models.LocationProviderState.PLAY_SERVICES_MISSING,
      ) to "play_services_unavailable",
      phoneWhereEverythingWorks().copy(appIsInForeground = false) to "foreground_required",
    )

    for ((index, case) in broken.withIndex()) {
      device.state = case.first
      evaluate("case-$index")

      assertEquals(2, reportOf("case-$index").status) // unavailable
      assertTrue(case.second, reportOf("case-$index").reasons.contains(case.second))
    }
    assertEquals(0, gps.timesStarted)
    assertTrue(everythingIsQuiet())
  }

  @Test
  fun CON_009_CON_010_CON_011_the_background_ends_the_evaluation_and_nothing_restarts() {
    val session = openSession(keepCollecting = true)
    evaluate("a", session.sessionId)
    assertTrue(gps.isOn)

    coordinator.appWentToBackground()

    assertEquals(3, reportOf("a").status)
    assertTrue(reportOf("a").reasons.contains("app_backgrounded"))
    assertTrue(everythingIsQuiet())
    val timesStartedSoFar = gps.timesStarted

    coordinator.appCameToForeground()
    queue.advanceTo(600000)
    assertTrue(everythingIsQuiet())
    assertEquals(timesStartedSoFar, gps.timesStarted) // the session did not start collecting again
  }

  @Test
  fun a_permission_taken_away_while_the_app_was_not_visible_wipes_what_was_remembered() {
    val session = openSession(keepCollecting = true)
    gps.deliver(readingNorth(0, 20.0))

    // The user goes to the settings and takes the permission away.
    coordinator.appWentToBackground()
    device.state = phoneWhereEverythingWorks().copy(locationPermission = LocationPermission.DENIED)
    coordinator.appCameToForeground()

    // The user gives it back. The reading from before must not be there.
    device.state = phoneWhereEverythingWorks()
    clock.now = 2000
    evaluate("a", session.sessionId)
    queue.advanceTo(30000)

    assertEquals(2, reportOf("a").status) // unavailable: nothing remembered
    assertEquals(0, reportOf("a").readingsExamined)
  }

  @Test
  fun REC_023_when_the_GPS_fails_the_evaluation_ends_and_the_GPS_is_turned_off() {
    evaluate("a")

    gps.fail()

    assertEquals(1, reports.size)
    assertTrue(reportOf("a").reasons.contains("native_provider_unavailable"))
    assertTrue(everythingIsQuiet())
  }

  // --- Sessions ----------------------------------------------------------------------

  @Test
  fun SES_001_a_usual_session_turns_nothing_on() {
    val session = openSession(keepCollecting = false)

    assertEquals(1, session.sessionId)
    assertTrue(everythingIsQuiet())
    assertEquals(0, gps.timesStarted)
  }

  @Test
  fun SES_002_SES_008_SES_011_a_collecting_session_keeps_the_GPS_on_at_its_own_pace() {
    val session = openSession(keepCollecting = true, timeBetweenMs = 5000)
    assertTrue(gps.isOn)
    assertEquals(5000, gps.timeBetweenReadingsMs)
    assertFalse(activitySensor.isOn) // collecting alone does not need the activity

    // An evaluation asks for readings faster; the same GPS is used.
    evaluate("a", session.sessionId)
    assertEquals(1000, gps.timeBetweenReadingsMs)
    assertTrue(activitySensor.isOn)

    clock.now = 1000
    gps.deliver(readingNorth(1000, 20.0))
    assertEquals(0, reportOf("a").status)

    // Back to the pace of the session.
    assertTrue(gps.isOn)
    assertEquals(5000, gps.timeBetweenReadingsMs)
    assertFalse(activitySensor.isOn)

    var closed = false
    coordinator.closeSession(session.sessionId) { closed = true }
    assertTrue(closed)
    assertTrue(everythingIsQuiet())
  }

  @Test
  fun CON_007_closing_a_session_ends_its_evaluations() {
    val session = openSession(keepCollecting = false)
    evaluate("a", session.sessionId)

    coordinator.closeSession(session.sessionId) { }

    assertEquals(3, reportOf("a").status)
    assertTrue(reportOf("a").reasons.contains("session_stopped"))
    assertTrue(everythingIsQuiet())
  }

  @Test
  fun CON_015_a_hundred_sessions_opened_and_closed_leave_nothing_on() {
    for (round in 0 until 100) {
      val session = openSession(keepCollecting = true)
      gps.deliver(readingNorth(clock.now, 20.0))
      evaluate("request-$round", session.sessionId)
      clock.now += 500
      coordinator.closeSession(session.sessionId) { }
      assertTrue(everythingIsQuiet())
    }
    assertEquals(100, reports.size)
  }

  // --- The phone activity ---------------------------------------------------------------

  @Test
  fun what_the_activity_sensor_reports_reaches_the_report() {
    evaluate("a")
    assertTrue(activitySensor.isOn)

    activitySensor.report(PhoneActivity.WALKING, sinceMs = 0)
    clock.now = 1000
    gps.deliver(readingNorth(1000, 20.0))

    assertEquals(1, reportOf("a").activityNow) // walking
    assertEquals(0, reportOf("a").activityConfidenceNow) // high
    assertFalse(activitySensor.isOn)
  }

  // --- Simulation ----------------------------------------------------------------------

  @Test
  fun in_the_simulation_the_real_GPS_is_never_used_and_no_permission_is_needed() {
    device.state = phoneWhereEverythingWorks().copy(locationPermission = LocationPermission.DENIED)
    coordinator.setSimulationEnabled(true) { }

    evaluate("simulated")
    assertEquals(0, gps.timesStarted)
    assertEquals(1, queue.howManyTimersAreSet)

    clock.now = 1000
    coordinator.simulateReadings(listOf(readingNorth(1000, 20.0))) { }

    val report = reportOf("simulated")
    assertEquals(0, report.status) // available
    assertTrue(report.reasons.contains("simulated_location_source"))
    assertEquals(0, queue.howManyTimersAreSet)

    // Readings given when nothing is listening are dropped, like a GPS that is off.
    coordinator.simulateReadings(listOf(readingNorth(1500, 20.0))) { }

    // Out of the simulation the real state of the phone counts again.
    coordinator.setSimulationEnabled(false) { }
    evaluate("real")
    assertEquals(2, reportOf("real").status)
    assertTrue(reportOf("real").reasons.contains("permission_denied"))
    assertFalse(reportOf("real").reasons.contains("simulated_location_source"))
    assertEquals(0, gps.timesStarted)
  }

  @Test
  fun in_the_simulation_the_app_also_says_what_the_phone_is_doing() {
    coordinator.setSimulationEnabled(true) { }
    val session = openSession(keepCollecting = true)

    coordinator.simulateReadings(listOf(readingNorth(0, 20.0, 5.0))) { }
    coordinator.simulatePhoneActivity(PhoneActivity.STANDING_STILL, ActivityConfidence.HIGH) { }

    // 10 seconds later an evaluation gets no new reading.
    clock.now = 10000
    evaluate("a", session.sessionId)
    assertFalse(activitySensor.isOn) // the real sensor is left alone
    queue.advanceTo(14500)

    val report = reportOf("a")
    assertEquals(2, report.source) // cached
    assertTrue(report.standingStillSinceTheReading)
    assertEquals(5.0 + 0.5 * 14.5, report.howFarPhoneCouldBeMeters, 1e-9)

    // "The phone stopped reporting": from now on its activity is unknown.
    coordinator.simulatePhoneActivity(null, ActivityConfidence.HIGH) { }
    clock.now = 20000
    evaluate("b", session.sessionId)
    queue.advanceTo(40000)
    assertFalse(reportOf("b").standingStillSinceTheReading)
  }

  @Test
  fun turning_the_simulation_on_stops_a_real_GPS_that_was_running() {
    openSession(keepCollecting = true)
    assertTrue(gps.isOn)

    coordinator.setSimulationEnabled(true) { }

    assertFalse(gps.isOn)
    var isOn = false
    coordinator.isSimulationOn { isOn = it }
    assertTrue(isOn)
  }

  // --- The end ----------------------------------------------------------------------------

  @Test
  fun CON_017_CON_028_after_shut_down_nobody_is_answered_and_nothing_runs() {
    evaluate("a")
    val listener = gps.lastListener!!

    coordinator.shutDown()

    assertTrue(reports.isEmpty()) // JavaScript is gone: there is nobody to answer
    assertFalse(gps.isOn)
    assertFalse(activitySensor.isOn)
    assertTrue(queue.isShutDown)

    // Whatever arrives afterwards is ignored and nothing crashes.
    listener.onReadings(listOf(readingNorth(1000, 20.0)))
    evaluate("b")
    coordinator.cancel("a")
    coordinator.appWentToBackground()
    coordinator.shutDown()
    assertTrue(reports.isEmpty())
  }

  // --- Many threads -----------------------------------------------------------------------

  @Test
  fun CON_023_calls_from_many_threads_are_put_in_line_one_answer_each() {
    // The real queue (one background thread) and a real clock this time.
    val realQueue = SingleThreadQueue()
    val realClock = Clock { System.nanoTime() / 1_000_000L }
    val engine = CoreEngine.createOrNull() ?: error("the native library was not loaded")
    val busyCoordinator = EngineCoordinator(engine, realQueue, realClock, FakeLocationSource(), FakePhoneActivitySource(), device)
    busyCoordinator.setSimulationEnabled(true) { }

    val threads = 4
    val perThread = 250
    val answers = ConcurrentHashMap<String, AtomicInteger>()
    val allAnswered = CountDownLatch(threads * perThread)

    val workers = (0 until threads).map { thread ->
      Thread {
        for (count in 0 until perThread) {
          val id = "t$thread-$count"
          answers[id] = AtomicInteger(0)
          busyCoordinator.evaluate(
            id,
            0,
            usualEvaluation(),
            onReport = {
              answers[id]!!.incrementAndGet()
              allAnswered.countDown()
            },
            onRefused = {
              answers[id]!!.incrementAndGet()
              allAnswered.countDown()
            },
          )
          if (count % 2 == 0) {
            busyCoordinator.simulateReadings(listOf(readingNorth(realClock.nowMs(), 20.0))) { }
          }
          busyCoordinator.cancel(id)
        }
      }
    }
    workers.forEach { it.start() }
    workers.forEach { it.join() }

    assertTrue("every evaluation must be answered", allAnswered.await(30, TimeUnit.SECONDS))
    assertEquals(threads * perThread, answers.size)
    assertTrue(answers.values.all { it.get() == 1 })

    busyCoordinator.shutDown()
  }
}
