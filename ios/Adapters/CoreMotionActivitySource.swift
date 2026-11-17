import CoreMotion
import Foundation

/// What the iPhone reports about its own movement: standing still, walking,
/// running, cycling or in a vehicle. It comes from the motion coprocessor,
/// not from the GPS.
///
/// It is about the PHONE, not about the person: a phone left on a table is
/// "standing still" while its owner walks away.
///
/// start() is only called when the permission already exists (see
/// IOSDeviceState.phoneActivitySupport), so it never shows a dialog.
final class CoreMotionActivitySource: PhoneActivitySource {
  private let clock: Clock
  private let deliveries = OperationQueue()

  // Touched on `deliveries` only.
  private var manager: CMMotionActivityManager?

  init(clock: Clock) {
    self.clock = clock
    deliveries.maxConcurrentOperationCount = 1
  }

  func start(listener: PhoneActivitySourceListener) {
    deliveries.addOperation {
      let newManager = CMMotionActivityManager()
      self.manager = newManager

      newManager.startActivityUpdates(to: self.deliveries) { [weak self] activity in
        guard let self = self, let activity = activity else { return }

        // iOS says since WHEN the phone is doing this, on the wall clock.
        // The engine's clock is another one, so the moment is worked out:
        // "it started 4 s ago, so 4 s ago on the engine's clock".
        let secondsAgo = max(0, Date().timeIntervalSince(activity.startDate))
        let sinceMs = self.clock.nowMs() - Int64((secondsAgo * 1000).rounded())

        listener.onActivityChanged(
          Self.phoneActivity(of: activity),
          confidence: Self.confidence(of: activity),
          sinceMs: sinceMs)
      }
    }
  }

  func stop() {
    deliveries.addOperation {
      self.manager?.stopActivityUpdates()
      self.manager = nil
    }
  }

  /// Shows the system's "Motion & Fitness" dialog, if the user was never
  /// asked. iOS has no function that only asks, so one empty question about
  /// the past is made: that is what brings the dialog up.
  static func requestPermission(whenAnswered: @escaping () -> Void) {
    let manager = CMMotionActivityManager()
    let now = Date()
    manager.queryActivityStarting(from: now, to: now, to: OperationQueue.main) { _, _ in
      _ = manager  // kept alive until iOS answers
      whenAnswered()
    }
  }

  /// iOS can say several things at once ("in a vehicle" and "standing still"
  /// at a red light). The one that allows the fastest movement wins, so the
  /// engine never assumes the phone moved less than it could have.
  private static func phoneActivity(of activity: CMMotionActivity) -> PhoneActivity {
    if activity.automotive {
      return .inVehicle
    } else if activity.cycling {
      return .cycling
    } else if activity.running {
      return .running
    } else if activity.walking {
      return .walking
    } else if activity.stationary {
      return .standingStill
    } else {
      return .unknown
    }
  }

  private static func confidence(of activity: CMMotionActivity) -> ActivityConfidence {
    switch activity.confidence {
    case .high:
      return .high
    case .medium:
      return .medium
    case .low:
      return .low
    @unknown default:
      return .unknown
    }
  }
}
