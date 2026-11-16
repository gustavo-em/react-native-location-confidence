// One evaluation, from the moment the app asks until the answer is ready.
//
// The app asks "is the courier near this address?". Answering takes time,
// because the GPS may need a few seconds to give a good reading, or may give
// nothing at all. This file decides how long to wait and when to stop.
// (The spec calls this piece "RecoveryPolicy".)
//
// An evaluation goes through these steps:
//
//   FIRST ATTEMPT   The GPS is turned on and we wait for a good reading.
//                   Normally up to 4.5 seconds.
//                     a good reading arrives      -> finished ("fresh")
//                     a weak reading arrives      -> go to EXTRA TIME right away
//                     nothing arrives in time     -> look at the history:
//                         a remembered reading is good enough  -> finished
//                         otherwise                            -> EXTRA TIME
//
//   EXTRA TIME      We keep waiting, normally up to 7 seconds more.
//                     a good reading arrives      -> finished ("recovered")
//                     the extra time ends         -> finished with the best
//                                                    we remember, or with nothing
//
//   At any moment   the total time is over (normally 12 seconds), the app
//                   cancels, the app goes to the background, the session is
//                   closed, the permission is lost     -> finished
//
// Whatever happens, an evaluation ALWAYS finishes, and it finishes once.
//
// This file never talks to the GPS and never reads a clock. The platform code
// (Kotlin or Swift) tells it what happened and what time it is:
//
//     "new readings arrived"      whenReadingsArrive()
//     "time passed"               whenTimePasses()
//     "stop"                      stop()
//
// and asks two questions after each of those:
//
//     "should the GPS be on?"     wantsLocationUpdates()
//     "when must I call again?"   nextWakeUpAtMs()
//
// A picture of the order of the calls, with examples worked step by step, is
// at the top of EvaluationRequest.cpp.
#ifndef LCE_EVALUATION_REQUEST_HPP
#define LCE_EVALUATION_REQUEST_HPP

#include <cstdint>

#include "Assessment.hpp"
#include "LocationHistory.hpp"
#include "PhoneActivityTimeline.hpp"
#include "ReasonCodes.hpp"

namespace lce {

// Can the phone give us locations at all?
enum class LocationProviderState : std::uint8_t {
  Available,
  Unavailable,          // the system has no way to give locations right now
  PlayServicesMissing,  // Android without Google Play services
};

// Can the phone tell us about its activity (walking, standing still...)?
enum class PhoneActivitySupport : std::uint8_t {
  Available,
  NotSupported,        // no sensor, or not implemented on this platform
  PermissionDenied,
  PermissionNotAsked,  // the app never asked the user
};

// What the platform code tells us about the phone at one moment.
// Everything starts at the cautious value: nothing is allowed until the
// platform says so.
struct DeviceState {
  LocationPermission locationPermission = LocationPermission::NotDetermined;
  PermissionPrecision permissionPrecision = PermissionPrecision::Unknown;
  bool locationIsTurnedOn = false;  // the "Location" switch of the phone
  LocationProviderState locationProvider = LocationProviderState::Unavailable;
  bool appIsInForeground = false;
  PhoneActivitySupport phoneActivity = PhoneActivitySupport::NotSupported;
};

// Everything one evaluation needs to know when it starts.
struct EvaluationSettings {
  // The address, the radius and the limits the app chose.
  WhatTheAppAskedFor asked;

  // The whole evaluation never takes longer than this. Normally 12 seconds.
  std::int64_t totalTimeAllowedMs = 12000;

  // How long the first attempt may wait for a good reading. Normally 4.5 s.
  std::int64_t firstAttemptTimeMs = 4500;

  // How long the extra time may last. Normally 7 seconds. Zero means "no
  // extra time".
  std::int64_t extraTimeMs = 7000;

  DeviceState device;
};

// What happened to one batch of readings that the GPS delivered.
struct NotesAboutNewReadings {
  std::uint32_t howManyArrived = 0;
  std::uint32_t howManyWereKept = 0;  // stored in the history

  // Why some were thrown away ("location_invalid_coordinates"...) and other
  // things worth telling ("samples_duplicate"...).
  ReasonSet reasons;
};

// Why an evaluation is being stopped from the outside.
enum class WhyTheEvaluationStopped : std::uint8_t {
  CancelledByTheApp,
  AppWentToBackground,
  SessionWasClosed,
  LocationPermissionWasLost,
  LocationProviderFailed,
};

class EvaluationRequest {
 public:
  // Starts the evaluation. When the GPS cannot be used at all (no
  // permission, location turned off, app not in the foreground...), the
  // evaluation finishes right here.
  //
  //   history, activityHistory   what the engine remembers right now
  //   nowMs                      the current time
  void start(std::uint32_t requestId,
             std::uint32_t sessionId,
             const EvaluationSettings& settings,
             const LocationHistory& history,
             const PhoneActivityHistory& activityHistory,
             std::int64_t nowMs) noexcept;

  // The GPS delivered readings and the engine already stored them.
  void whenReadingsArrive(const LocationHistory& history,
                          const PhoneActivityHistory& activityHistory,
                          std::int64_t nowMs,
                          const NotesAboutNewReadings& notes) noexcept;

  // Time passed. Call this at the moment nextWakeUpAtMs() asked for; calling
  // it earlier or more often is harmless.
  void whenTimePasses(const LocationHistory& history,
                      const PhoneActivityHistory& activityHistory,
                      std::int64_t nowMs) noexcept;

  // Stops the evaluation from the outside.
  void stop(WhyTheEvaluationStopped why,
            const LocationHistory& history,
            const PhoneActivityHistory& activityHistory,
            std::int64_t nowMs) noexcept;

  // True when the answer is ready. After that, nothing changes it anymore.
  bool isFinished() const noexcept { return phase_ == Phase::Finished; }

  // True while this evaluation needs the GPS to be on.
  bool wantsLocationUpdates() const noexcept { return !isFinished(); }

  // True while this evaluation can use reports about the phone activity.
  bool wantsPhoneActivityUpdates() const noexcept;

  // The next moment whenTimePasses() must be called. Only meaningful while
  // the evaluation is not finished.
  std::int64_t nextWakeUpAtMs() const noexcept;

  // The final report. Only meaningful when the evaluation is finished.
  const Assessment& report() const noexcept { return report_; }

  std::uint32_t requestId() const noexcept { return requestId_; }
  std::uint32_t sessionId() const noexcept { return sessionId_; }

 private:
  enum class Phase : std::uint8_t { FirstAttempt, ExtraTime, Finished };

  bool theTotalTimeIsOver(std::int64_t nowMs) const noexcept { return nowMs >= mustFinishAtMs_; }

  void rememberStep(EvaluationStep step) noexcept;
  void beginExtraTime(const LocationHistory& history, std::int64_t nowMs) noexcept;
  void finishWithWhatWeRemember(const LocationHistory& history,
                                const PhoneActivityHistory& activityHistory,
                                std::int64_t nowMs) noexcept;
  void finishWith(Assessment report, std::int64_t nowMs) noexcept;

  std::uint32_t requestId_ = 0;
  std::uint32_t sessionId_ = 0;
  EvaluationSettings settings_;

  // A request that was never started counts as finished.
  Phase phase_ = Phase::Finished;

  std::int64_t startedAtMs_ = 0;
  std::int64_t mustFinishAtMs_ = 0;        // start + total time allowed
  std::int64_t currentPhaseEndsAtMs_ = 0;  // end of the first attempt or of the extra time

  // Which readings this request obtained itself (see Assessment.hpp).
  WhatTheRequestKnows whatThisRequestKnows_;

  // Notes that go into the final report.
  std::array<EvaluationStep, MAX_STEPS_IN_REPORT> steps_{};
  std::size_t howManySteps_ = 0;
  bool firstAttemptTimedOut_ = false;
  bool extraTimeWasUsed_ = false;
  bool hasFallbackCause_ = false;
  ReasonCode fallbackCause_ = ReasonCode::NoUsableFix;
  std::uint32_t readingsReceived_ = 0;
  std::uint32_t readingsKept_ = 0;
  ReasonSet reasons_;

  Assessment report_;
};

}  // namespace lce

#endif  // LCE_EVALUATION_REQUEST_HPP
