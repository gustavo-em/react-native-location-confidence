import Foundation

/// Swift entry point used by the Objective-C++ TurboModule class.
///
/// Phase 0.5 (integration spike): proves that Swift inside the pod can call
/// the C facade of the shared core (`lce.h`) directly, with no Objective-C++
/// wrapper in between, and that the `.mm` TurboModule can call Swift.
@objc(LCECoreBridge)
public final class CoreBridge: NSObject {
  /// ABI version compiled into the shared core.
  @objc public static func abiVersion() -> UInt32 {
    return lce_abi_version()
  }

  /// Builds the spike echo payload from Foundation types.
  @objc public static func spikeEcho(
    requestId: String,
    sessionId: String?,
    radiusMeters: Double,
    enabled: Bool,
    mode: String,
    codes: [String]
  ) -> [String: Any] {
    let items: [[String: Any]] = [
      ["label": "radiusMeters", "value": radiusMeters],
      ["label": "absent", "value": NSNull()],
    ]

    return [
      "requestId": requestId,
      "abiVersion": NSNumber(value: lce_abi_version()),
      "platform": "ios",
      "status": enabled ? "ok" : "degraded",
      "mode": mode,
      "sessionId": sessionId ?? NSNull(),
      "maybeNumber": NSNull(),
      "flag": enabled,
      "tags": codes,
      "items": items,
    ]
  }
}
