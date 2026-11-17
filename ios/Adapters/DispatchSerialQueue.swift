import Foundation

/// The real queue: one line of tasks, handled one at a time, off the main
/// thread. Everything that touches the engine waits its turn here.
final class DispatchSerialQueue: SerialQueue {
  private let queue = DispatchQueue(label: "LocationConfidence")

  // Only read and written on `queue`.
  private var isShutDown = false

  func post(_ task: @escaping () -> Void) {
    queue.async {
      if self.isShutDown { return }
      task()
    }
  }

  func postDelayed(_ delayMs: Int64, _ task: @escaping () -> Void) -> Cancellable {
    let work = DispatchWorkItem { [weak self] in
      guard let self = self, !self.isShutDown else { return }
      task()
    }
    queue.asyncAfter(deadline: .now() + .milliseconds(Int(max(0, delayMs))), execute: work)
    return CancellableWork(work)
  }

  func shutDownAfter(_ lastTask: @escaping () -> Void) {
    queue.async {
      if self.isShutDown { return }
      lastTask()
      self.isShutDown = true
    }
  }
}

/// A task that is waiting for its moment and can still be called off.
private final class CancellableWork: Cancellable {
  private let work: DispatchWorkItem

  init(_ work: DispatchWorkItem) {
    self.work = work
  }

  func cancel() {
    work.cancel()
  }
}
