package com.locationconfidence.ports

import com.locationconfidence.models.ActivityConfidence
import com.locationconfidence.models.DeviceState
import com.locationconfidence.models.EvaluationSettings
import com.locationconfidence.models.PhoneActivity
import com.locationconfidence.models.PlatformWork
import com.locationconfidence.models.Reading
import com.locationconfidence.models.Report
import com.locationconfidence.models.SessionOpened
import com.locationconfidence.models.SessionSettings

/*
 * The "ports": everything the coordinator needs from the outside world,
 * written as small interfaces.
 *
 * The coordinator (EngineCoordinator.kt) only knows these interfaces. It does
 * not know Google Play services, Android permissions, threads or JNI. The
 * real pieces are in the `adapters` package; the tests plug in pretend ones
 * (a GPS that delivers what the test says, a clock the test moves by hand).
 */

/** The current time in milliseconds, on a clock that only moves forward. */
fun interface Clock {
  fun nowMs(): Long
}

/** Something that was scheduled and can still be called off. */
fun interface Cancellable {
  fun cancel()
}

/**
 * Runs tasks one at a time, in the order they were given.
 *
 * Everything that touches the engine goes through one of these. That is what
 * makes it safe: two things never use the engine at the same moment.
 */
interface SerialQueue {
  fun post(task: () -> Unit)

  /** Runs the task after the delay, unless it is cancelled before. */
  fun postDelayed(delayMs: Long, task: () -> Unit): Cancellable

  /** Runs one last task and then stops accepting tasks. */
  fun shutDownAfter(lastTask: () -> Unit)
}

/** Where the GPS readings come from. */
interface LocationSource {
  interface Listener {
    fun onReadings(readings: List<Reading>)

    /** The source broke and will deliver nothing more. */
    fun onFailure()
  }

  /**
   * Starts delivering readings, about one every [timeBetweenReadingsMs].
   * Calling it while already started only changes the pace.
   */
  fun start(timeBetweenReadingsMs: Long, listener: Listener)

  /** Stops delivering. Safe to call when it is not started. */
  fun stop()
}

/** Where reports about the phone activity come from. */
interface PhoneActivitySource {
  interface Listener {
    /** "Since [sinceMs] the phone is doing this." */
    fun onActivityChanged(activity: PhoneActivity, confidence: ActivityConfidence, sinceMs: Long)
  }

  fun start(listener: Listener)
  fun stop()
}

/** Permission, location switch, provider, foreground: read at one moment. */
fun interface DeviceStateSource {
  fun currentDeviceState(): DeviceState
}

/** What can go wrong when an evaluation is started. */
enum class StartRefusal {
  /** The address or the radius makes no sense. */
  INVALID_REQUEST,

  /** The engine could not take one more evaluation. */
  ENGINE_IS_FULL,
}

/** The answer to "start an evaluation": an id, or why it was refused. */
data class EvaluationStart(val requestId: Int, val refusal: StartRefusal?)

/**
 * The engine, seen from Kotlin. One function for each function of lce.h.
 * The real one (CoreEngine.kt) goes through JNI.
 */
interface CoreEnginePort {
  fun openSession(settings: SessionSettings, device: DeviceState, nowMs: Long): SessionOpened
  fun clearSessionHistory(sessionId: Int): Boolean
  fun closeSession(sessionId: Int, nowMs: Long)

  fun startEvaluation(sessionId: Int, settings: EvaluationSettings, device: DeviceState, nowMs: Long): EvaluationStart
  fun cancelEvaluation(requestId: Int, nowMs: Long)

  fun readingsArrived(readings: List<Reading>, nowMs: Long)
  fun phoneActivityChanged(activity: PhoneActivity, confidence: ActivityConfidence, sinceMs: Long, nowMs: Long)
  fun phoneActivityUpdatesStopped(nowMs: Long)
  fun timePassed(nowMs: Long)
  fun appWentToBackground(nowMs: Long)
  fun locationPermissionWasLost(nowMs: Long)
  fun locationProviderFailed(nowMs: Long)
  fun setReadingsAreSimulated(simulated: Boolean)

  fun whatThePlatformMustDoNow(): PlatformWork

  /** One finished report, or null when none is waiting. */
  fun takeFinishedReport(): Report?

  /** Destroys the engine. Nothing may be called afterwards. */
  fun destroy()
}
