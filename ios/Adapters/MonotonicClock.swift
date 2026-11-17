import Foundation

/// The clock of the engine: milliseconds that only move forward, and that
/// keep counting while the phone sleeps. Changing the date in the phone's
/// settings does not touch it. The age of every reading is measured on it.
final class MonotonicClock: Clock {
  func nowMs() -> Int64 {
    return Int64(clock_gettime_nsec_np(CLOCK_MONOTONIC) / 1_000_000)
  }
}
