package com.locationconfidence

import com.locationconfidence.models.ActivityConfidence
import com.locationconfidence.models.DeviceState
import com.locationconfidence.models.EvaluationSettings
import com.locationconfidence.models.LocationPermission
import com.locationconfidence.models.LocationProviderState
import com.locationconfidence.models.MockPolicy
import com.locationconfidence.models.PermissionPrecision
import com.locationconfidence.models.PhoneActivity
import com.locationconfidence.models.PhoneActivitySupport
import com.locationconfidence.models.Reading
import com.locationconfidence.ports.Cancellable
import com.locationconfidence.ports.Clock
import com.locationconfidence.ports.DeviceStateSource
import com.locationconfidence.ports.LocationSource
import com.locationconfidence.ports.PhoneActivitySource
import com.locationconfidence.ports.SerialQueue

/*
 * Pretend pieces for the tests. They stand in for the GPS, the clock, the
 * queue and the state of the phone, so a test decides exactly what happens
 * and when. No test waits for real time.
 */

/** A clock the test moves by hand. */
class FakeClock : Clock {
  var now: Long = 0L
  override fun nowMs(): Long = now
}

/**
 * A queue with no thread: a task runs as soon as it is posted, and a timer
 * fires when the test moves the clock past its moment with [advanceTo].
 */
class ManualQueue(private val clock: FakeClock) : SerialQueue {
  private class Timer(val firesAtMs: Long, val task: () -> Unit) {
    var isCancelled = false
  }

  private val waitingTasks = ArrayDeque<() -> Unit>()
  private val timers = ArrayList<Timer>()
  private var isRunningATask = false
  var isShutDown = false
    private set

  /** Timers that are set and were not cancelled. */
  val howManyTimersAreSet: Int
    get() = timers.count { !it.isCancelled }

  override fun post(task: () -> Unit) {
    if (isShutDown) return
    waitingTasks.addLast(task)

    // A task posted from inside another task waits for that one to end, as
    // it would on a real queue.
    if (isRunningATask) return
    isRunningATask = true
    while (waitingTasks.isNotEmpty()) {
      waitingTasks.removeFirst().invoke()
    }
    isRunningATask = false
  }

  override fun postDelayed(delayMs: Long, task: () -> Unit): Cancellable {
    val timer = Timer(clock.now + delayMs, task)
    timers.add(timer)
    return Cancellable { timer.isCancelled = true }
  }

  override fun shutDownAfter(lastTask: () -> Unit) {
    post(lastTask)
    isShutDown = true
    timers.clear()
  }

  /** Moves the clock to [targetMs], firing every timer that is due on the way, in order. */
  fun advanceTo(targetMs: Long) {
    while (true) {
      val next = timers.filter { !it.isCancelled && it.firesAtMs <= targetMs }
        .minByOrNull { it.firesAtMs } ?: break
      timers.remove(next)
      if (next.firesAtMs > clock.now) {
        clock.now = next.firesAtMs
      }
      post(next.task)
    }
    timers.removeAll { it.isCancelled }
    if (targetMs > clock.now) {
      clock.now = targetMs
    }
  }
}

/** A GPS that delivers what the test gives it, and only while it is on. */
class FakeLocationSource : LocationSource {
  private var listener: LocationSource.Listener? = null

  /** Kept after stop(), so a test can make a delivery arrive too late. */
  var lastListener: LocationSource.Listener? = null
    private set

  var timesStarted = 0
    private set
  var timesStopped = 0
    private set
  var timeBetweenReadingsMs = 0L
    private set

  val isOn: Boolean
    get() = listener != null

  override fun start(timeBetweenReadingsMs: Long, listener: LocationSource.Listener) {
    timesStarted += 1
    this.timeBetweenReadingsMs = timeBetweenReadingsMs
    this.listener = listener
    lastListener = listener
  }

  override fun stop() {
    timesStopped += 1
    listener = null
  }

  /** Returns false when the GPS is off and nothing was delivered. */
  fun deliver(vararg readings: Reading): Boolean {
    val current = listener ?: return false
    current.onReadings(readings.toList())
    return true
  }

  fun fail() {
    listener?.onFailure()
  }
}

class FakePhoneActivitySource : PhoneActivitySource {
  private var listener: PhoneActivitySource.Listener? = null
  var timesStarted = 0
    private set

  val isOn: Boolean
    get() = listener != null

  override fun start(listener: PhoneActivitySource.Listener) {
    timesStarted += 1
    this.listener = listener
  }

  override fun stop() {
    listener = null
  }

  fun report(activity: PhoneActivity, sinceMs: Long) {
    listener?.onActivityChanged(activity, ActivityConfidence.HIGH, sinceMs)
  }
}

/** The state of the phone, set by the test. */
class FakeDeviceState : DeviceStateSource {
  var state: DeviceState = phoneWhereEverythingWorks()
  override fun currentDeviceState(): DeviceState = state
}

fun phoneWhereEverythingWorks() = DeviceState(
  locationPermission = LocationPermission.GRANTED,
  permissionPrecision = PermissionPrecision.PRECISE,
  locationIsTurnedOn = true,
  locationProvider = LocationProviderState.AVAILABLE,
  appIsInForeground = true,
  phoneActivity = PhoneActivitySupport.AVAILABLE,
)

/** The usual evaluation: the address at latitude 0, longitude 0, radius 100 meters. */
fun usualEvaluation() = EvaluationSettings(
  destinationLatitude = 0.0,
  destinationLongitude = 0.0,
  radiusMeters = 100.0,
  newestAgeThatCountsAsFreshMs = 10000,
  oldestAgeAllowedMs = 60000,
  worstGpsErrorAcceptedMeters = 50.0,
  fastestPossibleSpeedMps = 45.0,
  usePhoneActivity = true,
  allowAnswerFromHistory = true,
  mockPolicy = MockPolicy.ZERO_SCORE,
  totalTimeAllowedMs = 12000,
  firstAttemptTimeMs = 4500,
  extraTimeMs = 7000,
)

/** A reading some meters north of the address, taken at [capturedAtMs]. */
fun readingNorth(capturedAtMs: Long, metersNorth: Double, gpsErrorMeters: Double = 8.0): Reading {
  val degreesPerMeter = 180.0 / (3.14159265358979323846 * 6371008.8)
  return Reading(
    latitude = metersNorth * degreesPerMeter,
    longitude = 0.0,
    accuracyMeters = gpsErrorMeters,
    capturedAtMs = capturedAtMs,
  )
}
