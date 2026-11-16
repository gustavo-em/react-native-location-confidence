package com.locationconfidence.application

import com.locationconfidence.models.ActivityConfidence
import com.locationconfidence.models.DeviceState
import com.locationconfidence.models.EvaluationSettings
import com.locationconfidence.models.LocationPermission
import com.locationconfidence.models.LocationProviderState
import com.locationconfidence.models.PermissionPrecision
import com.locationconfidence.models.PhoneActivity
import com.locationconfidence.models.PhoneActivitySupport
import com.locationconfidence.models.PlatformWork
import com.locationconfidence.models.Reading
import com.locationconfidence.models.Report
import com.locationconfidence.models.SessionOpened
import com.locationconfidence.models.SessionSettings
import com.locationconfidence.ports.Cancellable
import com.locationconfidence.ports.Clock
import com.locationconfidence.ports.CoreEnginePort
import com.locationconfidence.ports.DeviceStateSource
import com.locationconfidence.ports.LocationSource
import com.locationconfidence.ports.PhoneActivitySource
import com.locationconfidence.ports.SerialQueue
import com.locationconfidence.ports.StartRefusal

/**
 * The link between the phone and the engine.
 *
 * The engine (C++) takes every decision, but it cannot touch the phone: it
 * has no GPS, no timer and no clock. This class is its hands. It does three
 * things and nothing else:
 *
 *   1. It TELLS the engine what happens:
 *        the app asked for something      openSession(), evaluate(), cancel()...
 *        the GPS delivered readings       (from the LocationSource)
 *        the timer fired                  (from the SerialQueue)
 *        the app went to the background   appWentToBackground()
 *
 *   2. Right after telling it, it OBEYS the engine: [obeyTheEngine]
 *        - takes the finished reports and hands each one to whoever asked;
 *        - turns the GPS on or off, as the engine says;
 *        - sets one timer for the moment the engine wants to be called again.
 *
 *   3. It keeps everything on ONE queue, so two things never use the engine
 *      at the same moment.
 *
 * It decides nothing about locations. There is no "if the reading is good"
 * anywhere in this file: that lives in the engine, and it is the same code
 * on Android and on iOS.
 *
 * THE ORDER OF THE CALLS, for one evaluation:
 *
 *   evaluate()                                   <- called from the React Native thread
 *     `-- queue.post                             now on the engine's queue
 *           |-- engine.startEvaluation()
 *           `-- obeyTheEngine()
 *                 |-- engine.takeFinishedReport()        nothing yet
 *                 |-- engine.whatThePlatformMustDoNow()  "GPS on, wake up in 4.5 s"
 *                 |-- adjustLocationUpdates()            locationSource.start()
 *                 `-- adjustTimer()                      queue.postDelayed()
 *
 *   the GPS delivers a reading                   <- called from a thread of the GPS
 *     `-- queue.post
 *           |-- engine.readingsArrived()
 *           `-- obeyTheEngine()
 *                 |-- engine.takeFinishedReport()        the report! -> onReport()
 *                 |-- engine.whatThePlatformMustDoNow()  "GPS off, no wake up"
 *                 |-- adjustLocationUpdates()            locationSource.stop()
 *                 `-- adjustTimer()                      the timer is cancelled
 *
 * This class only knows the interfaces of the `ports` package, so the tests
 * run it with a pretend GPS and a clock they move by hand.
 */
class EngineCoordinator(
  private val engine: CoreEnginePort,
  private val queue: SerialQueue,
  private val clock: Clock,
  private val realLocationSource: LocationSource,
  private val phoneActivitySource: PhoneActivitySource,
  private val realDeviceState: DeviceStateSource,
) {

  /** Someone who is waiting for the report of an evaluation. */
  private class Waiting(val idGivenByTheApp: String, val onReport: (Report) -> Unit)

  // Everything below is only touched from the queue.

  /** Who is waiting, by the request id the engine gave. */
  private val waitingByRequestId = HashMap<Int, Waiting>()

  /** The engine's request id, by the id the app gave (the app cancels by its own id). */
  private val requestIdByAppId = HashMap<String, Int>()

  // How the phone is right now, so we only change what needs to change.
  private var locationUpdatesAreOn = false
  private var timeBetweenReadingsMs = 0L
  private var activityUpdatesAreOn = false
  private var timer: Cancellable? = null
  private var timerIsSetForMs = 0L

  // Each time the GPS is (re)started this number grows. A delivery carries
  // the number it was started with; one that arrives with an old number comes
  // from a GPS we already turned off, and is ignored.
  private var locationUpdatesNumber = 0
  private var activityUpdatesNumber = 0

  // Simulation: the app feeds readings of its own instead of the GPS.
  private var simulationIsOn = false
  private val simulatedLocationSource = SimulatedLocationSource()

  private var locationWasGrantedLastTime = false
  private var isShutDown = false

  // --- What the app asks for -------------------------------------------------

  fun openSession(settings: SessionSettings, onOpened: (SessionOpened) -> Unit) {
    queue.post {
      if (isShutDown) return@post
      val opened = engine.openSession(settings, currentDeviceState(), clock.nowMs())
      obeyTheEngine()
      onOpened(opened)
    }
  }

  fun clearHistory(sessionId: Int, onDone: () -> Unit) {
    queue.post {
      if (isShutDown) return@post
      engine.clearSessionHistory(sessionId)
      obeyTheEngine()
      onDone()
    }
  }

  fun closeSession(sessionId: Int, onDone: () -> Unit) {
    queue.post {
      if (isShutDown) return@post
      engine.closeSession(sessionId, clock.nowMs())
      obeyTheEngine()
      onDone()
    }
  }

  /**
   * Starts an evaluation.
   *
   * Exactly one of the two callbacks is called, exactly once:
   *   [onReport]  with the final report, sooner or later;
   *   [onRefused] at once, when the engine did not accept the request.
   * Neither is called after [shutDown].
   */
  fun evaluate(
    idGivenByTheApp: String,
    sessionId: Int,
    settings: EvaluationSettings,
    onReport: (Report) -> Unit,
    onRefused: (StartRefusal) -> Unit,
  ) {
    queue.post {
      if (isShutDown) return@post

      val started = engine.startEvaluation(sessionId, settings, currentDeviceState(), clock.nowMs())

      val refusal = started.refusal
      if (refusal != null) {
        onRefused(refusal)
        return@post
      }

      // Remember who is waiting BEFORE obeying: the report may already be there.
      waitingByRequestId[started.requestId] = Waiting(idGivenByTheApp, onReport)
      requestIdByAppId[idGivenByTheApp] = started.requestId
      obeyTheEngine()
    }
  }

  /** Cancels an evaluation by the id the app gave. Unknown ids are ignored. */
  fun cancel(idGivenByTheApp: String) {
    queue.post {
      if (isShutDown) return@post
      val requestId = requestIdByAppId[idGivenByTheApp] ?: return@post
      engine.cancelEvaluation(requestId, clock.nowMs())
      obeyTheEngine()
    }
  }

  // --- What happens to the app -----------------------------------------------

  fun appWentToBackground() {
    queue.post {
      if (isShutDown) return@post
      engine.appWentToBackground(clock.nowMs())
      obeyTheEngine()
    }
  }

  /**
   * The app is visible again. Nothing restarts by itself. We only check
   * whether the user took the location permission away while the app was
   * not visible.
   */
  fun appCameToForeground() {
    queue.post {
      if (isShutDown) return@post

      // Looking at the phone also updates `locationWasGrantedLastTime`, so
      // the value from before is kept aside first.
      val wasGrantedBefore = locationWasGrantedLastTime
      realDeviceState.currentDeviceState().also { real ->
        locationWasGrantedLastTime = real.locationPermission == LocationPermission.GRANTED
      }
      val isGrantedNow = locationWasGrantedLastTime

      if (wasGrantedBefore && !isGrantedNow) {
        engine.locationPermissionWasLost(clock.nowMs())
        obeyTheEngine()
      }
    }
  }

  // --- Simulation (demos and tests) ------------------------------------------

  /**
   * Turns the simulation on or off. While it is on, the real GPS is never
   * used and no permission is needed: the readings are the ones the app
   * gives to [simulateReadings]. Every report then carries the reason
   * "simulated_location_source".
   */
  fun setSimulationEnabled(enabled: Boolean, onDone: () -> Unit) {
    queue.post {
      if (isShutDown) return@post
      if (enabled != simulationIsOn) {
        // Stop whichever source is running; obeyTheEngine() starts the other
        // one if the engine still wants readings.
        stopLocationUpdates()
        simulationIsOn = enabled
        engine.setReadingsAreSimulated(enabled)
        if (!enabled) {
          // What the phone "was doing" in the simulation is not true anymore.
          engine.phoneActivityUpdatesStopped(clock.nowMs())
        }
        obeyTheEngine()
      }
      onDone()
    }
  }

  /** Hands made-up readings to the engine, as if the GPS had delivered them. */
  fun simulateReadings(readings: List<Reading>, onDone: () -> Unit) {
    queue.post {
      if (isShutDown) return@post
      if (simulationIsOn) {
        simulatedLocationSource.deliver(readings)
      }
      onDone()
    }
  }

  /**
   * Says what the phone "is doing" in the simulation. `null` means the phone
   * stopped reporting its activity.
   */
  fun simulatePhoneActivity(activity: PhoneActivity?, confidence: ActivityConfidence, onDone: () -> Unit) {
    queue.post {
      if (isShutDown) return@post
      if (simulationIsOn) {
        val nowMs = clock.nowMs()
        if (activity == null) {
          engine.phoneActivityUpdatesStopped(nowMs)
        } else {
          engine.phoneActivityChanged(activity, confidence, nowMs, nowMs)
        }
        obeyTheEngine()
      }
      onDone()
    }
  }

  fun isSimulationOn(onAnswer: (Boolean) -> Unit) {
    queue.post { onAnswer(simulationIsOn) }
  }

  // --- The end ---------------------------------------------------------------

  /**
   * Stops everything and destroys the engine. Called when React Native tears
   * the module down. Evaluations that were still running get NO answer: the
   * JavaScript side that was waiting for them no longer exists.
   */
  fun shutDown() {
    queue.shutDownAfter {
      isShutDown = true
      stopLocationUpdates()
      stopActivityUpdates()
      timer?.cancel()
      timer = null
      waitingByRequestId.clear()
      requestIdByAppId.clear()
      engine.destroy()
    }
  }

  // --- Obeying the engine ----------------------------------------------------

  /**
   * Called after EVERY call into the engine. It takes the finished reports
   * and makes the phone match what the engine wants right now.
   */
  private fun obeyTheEngine() {
    // 1. Finished reports go to whoever is waiting for them.
    while (true) {
      val report = engine.takeFinishedReport() ?: break
      val waiting = waitingByRequestId.remove(report.requestId) ?: continue
      requestIdByAppId.remove(waiting.idGivenByTheApp)
      waiting.onReport(report)
    }

    // 2. The GPS, the activity updates and the timer.
    val work = engine.whatThePlatformMustDoNow()
    adjustLocationUpdates(work)
    adjustActivityUpdates(work)
    adjustTimer(work)
  }

  private fun adjustLocationUpdates(work: PlatformWork) {
    val mustStart = work.locationUpdatesAreWanted && !locationUpdatesAreOn
    val mustChangeThePace = work.locationUpdatesAreWanted && locationUpdatesAreOn &&
      work.timeBetweenLocationUpdatesMs != timeBetweenReadingsMs
    val mustStop = !work.locationUpdatesAreWanted && locationUpdatesAreOn

    if (mustStart || mustChangeThePace) {
      startLocationUpdates(work.timeBetweenLocationUpdatesMs)
    } else if (mustStop) {
      stopLocationUpdates()
    }
  }

  private fun startLocationUpdates(timeBetweenMs: Long) {
    locationUpdatesNumber += 1
    val startedWithNumber = locationUpdatesNumber

    val listener = object : LocationSource.Listener {
      // The GPS calls these from its own thread: go to the queue first.
      override fun onReadings(readings: List<Reading>) {
        queue.post {
          val comesFromTheCurrentStart = startedWithNumber == locationUpdatesNumber
          if (isShutDown || !locationUpdatesAreOn || !comesFromTheCurrentStart) {
            return@post // a late delivery from a GPS we already turned off
          }
          engine.readingsArrived(readings, clock.nowMs())
          obeyTheEngine()
        }
      }

      override fun onFailure() {
        queue.post {
          val comesFromTheCurrentStart = startedWithNumber == locationUpdatesNumber
          if (isShutDown || !locationUpdatesAreOn || !comesFromTheCurrentStart) {
            return@post
          }
          engine.locationProviderFailed(clock.nowMs())
          obeyTheEngine()
        }
      }
    }

    currentLocationSource().start(timeBetweenMs, listener)
    locationUpdatesAreOn = true
    timeBetweenReadingsMs = timeBetweenMs
  }

  private fun stopLocationUpdates() {
    if (locationUpdatesAreOn) {
      currentLocationSource().stop()
      locationUpdatesAreOn = false
      locationUpdatesNumber += 1 // anything still on its way is now "old"
    }
  }

  private fun adjustActivityUpdates(work: PlatformWork) {
    // In the simulation the app says what the phone is doing; the real
    // sensor is left alone.
    val wanted = work.phoneActivityUpdatesAreWanted && !simulationIsOn

    if (wanted && !activityUpdatesAreOn) {
      activityUpdatesNumber += 1
      val startedWithNumber = activityUpdatesNumber

      phoneActivitySource.start(object : PhoneActivitySource.Listener {
        override fun onActivityChanged(activity: PhoneActivity, confidence: ActivityConfidence, sinceMs: Long) {
          queue.post {
            val comesFromTheCurrentStart = startedWithNumber == activityUpdatesNumber
            if (isShutDown || !activityUpdatesAreOn || !comesFromTheCurrentStart) {
              return@post
            }
            engine.phoneActivityChanged(activity, confidence, sinceMs, clock.nowMs())
            obeyTheEngine()
          }
        }
      })
      activityUpdatesAreOn = true
    } else if (!wanted && activityUpdatesAreOn) {
      stopActivityUpdates()
      // From now on nobody is watching: the activity is unknown again.
      engine.phoneActivityUpdatesStopped(clock.nowMs())
    }
  }

  private fun stopActivityUpdates() {
    if (activityUpdatesAreOn) {
      phoneActivitySource.stop()
      activityUpdatesAreOn = false
      activityUpdatesNumber += 1
    }
  }

  /** There is one timer for the whole engine: "call me again at this moment". */
  private fun adjustTimer(work: PlatformWork) {
    val alreadySetForThatMoment = timer != null && timerIsSetForMs == work.wakeUpAtMs

    if (!work.wakeUpIsWanted) {
      timer?.cancel()
      timer = null
    } else if (!alreadySetForThatMoment) {
      timer?.cancel()

      val delayMs = maxOf(0L, work.wakeUpAtMs - clock.nowMs())
      timerIsSetForMs = work.wakeUpAtMs
      timer = queue.postDelayed(delayMs) {
        if (isShutDown) return@postDelayed
        timer = null
        engine.timePassed(clock.nowMs())
        obeyTheEngine()
      }
    }
  }

  // --- Helpers ---------------------------------------------------------------

  private fun currentLocationSource(): LocationSource =
    if (simulationIsOn) simulatedLocationSource else realLocationSource

  /**
   * What the engine is told about the phone. In the simulation everything
   * "works": the readings do not come from the phone, so its permission and
   * its GPS do not matter. Only the foreground state stays real.
   */
  private fun currentDeviceState(): DeviceState {
    val real = realDeviceState.currentDeviceState()
    locationWasGrantedLastTime = real.locationPermission == LocationPermission.GRANTED

    if (!simulationIsOn) {
      return real
    }
    return DeviceState(
      locationPermission = LocationPermission.GRANTED,
      permissionPrecision = PermissionPrecision.PRECISE,
      locationIsTurnedOn = true,
      locationProvider = LocationProviderState.AVAILABLE,
      appIsInForeground = real.appIsInForeground,
      phoneActivity = PhoneActivitySupport.AVAILABLE,
    )
  }

  /**
   * The pretend GPS of the simulation. It delivers what [deliver] is given,
   * and only while it is started, like a real GPS.
   */
  private class SimulatedLocationSource : LocationSource {
    private var listener: LocationSource.Listener? = null

    override fun start(timeBetweenReadingsMs: Long, listener: LocationSource.Listener) {
      this.listener = listener
    }

    override fun stop() {
      listener = null
    }

    fun deliver(readings: List<Reading>) {
      listener?.onReadings(readings)
    }
  }
}
