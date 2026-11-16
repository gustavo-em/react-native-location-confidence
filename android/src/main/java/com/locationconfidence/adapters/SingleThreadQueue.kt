package com.locationconfidence.adapters

import com.locationconfidence.ports.Cancellable
import com.locationconfidence.ports.SerialQueue
import java.util.concurrent.RejectedExecutionException
import java.util.concurrent.ScheduledThreadPoolExecutor
import java.util.concurrent.TimeUnit

/**
 * The real queue: one background thread that runs tasks one at a time, in
 * the order they were given.
 *
 * Every call into the engine goes through this thread, so the engine is
 * never used by two threads at once and never blocks the screen.
 */
internal class SingleThreadQueue : SerialQueue {
  private val executor = ScheduledThreadPoolExecutor(1) { task ->
    Thread(task, "LocationConfidence").apply { isDaemon = true }
  }.apply {
    // A cancelled timer leaves the queue at once instead of waiting for its time.
    removeOnCancelPolicy = true
  }

  override fun post(task: () -> Unit) {
    try {
      executor.execute(task)
    } catch (ignored: RejectedExecutionException) {
      // The queue was shut down: the module is gone, there is nobody to answer.
    }
  }

  override fun postDelayed(delayMs: Long, task: () -> Unit): Cancellable {
    return try {
      val scheduled = executor.schedule(task, delayMs, TimeUnit.MILLISECONDS)
      Cancellable { scheduled.cancel(false) }
    } catch (ignored: RejectedExecutionException) {
      Cancellable { }
    }
  }

  override fun shutDownAfter(lastTask: () -> Unit) {
    post(lastTask)
    // Tasks already in line still run; timers that have not fired are dropped.
    executor.executeExistingDelayedTasksAfterShutdownPolicy = false
    executor.shutdown()
  }
}
