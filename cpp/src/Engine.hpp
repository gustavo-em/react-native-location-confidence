// The engine: the one object the platform code talks to.
//
//   platform code (Kotlin on Android, Swift on iOS)
//        |
//        |  "open a session"        "start a request"         "cancel it"
//        |  "readings arrived"      "the phone activity changed"
//        |  "time passed"           "the app went to the background"  ...
//        v
//      Engine
//        |-- sessions              each one remembers its own GPS readings
//        |-- requests              the ones that are running (ProximityRequest.cpp)
//        `-- phone activity        one timeline for the whole phone
//
// The engine never touches the GPS, a timer or the clock. After EVERY call,
// the platform code asks it two things and obeys:
//
//   takeFinishedReport()        "did any request finish?"
//                               -> hand each report to the app
//
//   whatThePlatformMustDoNow()  "should the GPS be on? how often?"
//                               "should phone activity updates be on?"
//                               "when must I call whenTimePasses() next?"
//
// The answers are a description of how things must be NOW, not a list of
// orders to carry out one by one. If the engine says "the GPS must be on" ten
// times in a row, the platform turns it on once. That makes mistakes hard:
// there is no order that can be lost or carried out twice.
//
// Because all the decisions are here, the Kotlin and the Swift code stay
// small and behave the same way.
//
// The engine must be used from one thread at a time. The platform code
// guarantees that by sending everything through a single queue.
//
// A picture of the order of the calls, with examples worked step by step, is
// at the top of Engine.cpp.
#ifndef LCE_ENGINE_HPP
#define LCE_ENGINE_HPP

#include <array>
#include <cstddef>
#include <cstdint>

#include "Report.hpp"
#include "ProximityRequest.hpp"
#include "LocationHistory.hpp"
#include "PhoneActivityTimeline.hpp"
#include "ReadingChecker.hpp"
#include "ReasonCodes.hpp"

namespace lce {

// --- Limits ----------------------------------------------------------------------

inline constexpr std::size_t MAX_OPEN_SESSIONS = 8;
inline constexpr std::size_t MAX_RUNNING_REQUESTS = 16;
inline constexpr std::size_t MAX_RUNNING_REQUESTS_PER_SESSION = 4;

// Reports of finished requests wait here until the platform takes them.
inline constexpr std::size_t MAX_REPORTS_WAITING = MAX_RUNNING_REQUESTS + 4;

// While a request is running, the GPS is asked for a reading every second.
inline constexpr std::int64_t TIME_BETWEEN_READINGS_DURING_REQUEST_MS = 1000;

// --- Sessions --------------------------------------------------------------------

// What the app chose when it opened a session.
struct SessionSettings {
  // False (the usual case): the session only remembers the readings obtained
  // by its requests. Opening it does not turn the GPS on.
  // True: the session keeps collecting readings while it is open, so that an
  // request finds a history already there.
  bool keepCollectingReadings = false;

  // Only used when collecting: one reading is kept every this many
  // milliseconds, and the collecting stops by itself after the longest time.
  std::int64_t timeBetweenReadingsMs = 5000;
  std::int64_t longestCollectingTimeMs = 900000;  // 15 minutes

  DeviceState device;
};

// Is a session collecting readings?
enum class CollectingState : std::uint8_t {
  NotRequested,  // the app did not ask for it
  Active,
  Blocked,  // the app asked, but it could not start (no permission, ...)
  Stopped,  // it was active and stopped (time limit, background, ...)
};

// The answer to "open a session".
struct SessionOpened {
  // 0 means the session was NOT opened: too many sessions are open already.
  std::uint32_t sessionId = 0;
  CollectingState collecting = CollectingState::NotRequested;

  // Why the collecting is blocked, or why the session was not opened.
  ReasonSet reasons;
};

// --- Requests -----------------------------------------------------------------

// The answer to "start a request".
struct RequestStarted {
  enum class Result : std::uint8_t {
    // The request exists. Its report will come out of
    // takeFinishedReport(), maybe right away.
    Accepted,
    // The address or the radius makes no sense. Nothing was started. This is
    // a mistake of the calling code, not something that happens to users.
    InvalidRequest,
    // The platform is not taking the finished reports. Nothing was started.
    TooManyReportsWaiting,
  };

  Result result = Result::InvalidRequest;
  std::uint32_t requestId = 0;
};

// --- What the platform must do ---------------------------------------------------

struct PlatformWork {
  // Should the GPS be delivering readings right now, and how often?
  bool locationUpdatesAreWanted = false;
  std::int64_t timeBetweenLocationUpdatesMs = TIME_BETWEEN_READINGS_DURING_REQUEST_MS;

  // Should the phone be reporting its activity right now?
  bool phoneActivityUpdatesAreWanted = false;

  // When must whenTimePasses() be called next? Only meaningful when
  // `wakeUpIsWanted` is true.
  bool wakeUpIsWanted = false;
  std::int64_t wakeUpAtMs = 0;
};

// --- The engine --------------------------------------------------------------------

class Engine {
 public:
  // --- Sessions ---
  SessionOpened openSession(const SessionSettings& settings, std::int64_t nowMs) noexcept;

  // Forgets the readings of a session; the session stays open.
  // Returns false when there is no such session.
  bool clearSessionHistory(std::uint32_t sessionId) noexcept;

  // Closes a session: its requests are stopped and its readings are
  // forgotten. Closing a session that does not exist does nothing.
  void closeSession(std::uint32_t sessionId, std::int64_t nowMs) noexcept;

  // --- Requests ---
  // `sessionId` 0 means "no session": the request works alone and
  // remembers nothing after it ends.
  RequestStarted startRequest(std::uint32_t sessionId,
                                    const RequestSettings& settings,
                                    std::int64_t nowMs) noexcept;

  // Cancelling a request that already ended, or that never existed, does
  // nothing.
  void cancelRequest(std::uint32_t requestId, std::int64_t nowMs) noexcept;

  // --- Things that happen ---
  void whenReadingsArrive(const UncheckedReading* readings,
                          std::size_t howMany,
                          std::int64_t nowMs) noexcept;

  // "Since `sinceMs` the phone is doing this." The activity stays the same
  // until the next call, or until whenPhoneActivityUpdatesStop().
  void whenPhoneActivityChanges(PhoneActivity activity,
                                PhoneActivityConfidence confidence,
                                std::int64_t sinceMs,
                                std::int64_t nowMs) noexcept;

  // The phone stopped reporting its activity. From now on it is unknown.
  void whenPhoneActivityUpdatesStop(std::int64_t nowMs) noexcept;

  void whenTimePasses(std::int64_t nowMs) noexcept;
  void whenAppGoesToBackground(std::int64_t nowMs) noexcept;
  void whenLocationPermissionIsLost(std::int64_t nowMs) noexcept;
  void whenLocationProviderFails(std::int64_t nowMs) noexcept;

  // True: the app replaced the GPS with readings of its own (a demo, a
  // test). Every report then carries the reason "simulated_location_source".
  void setReadingsAreSimulated(bool readingsAreSimulated) noexcept {
    readingsAreSimulated_ = readingsAreSimulated;
  }

  // --- What the platform asks after every call ---
  PlatformWork whatThePlatformMustDoNow() const noexcept;

  // Gives one finished report and returns true, or returns false when none
  // is waiting. Call it again and again until it returns false.
  bool takeFinishedReport(Report& report) noexcept;

  // --- For tests and diagnostics ---
  std::size_t howManySessionsAreOpen() const noexcept;
  std::size_t howManyRequestsAreRunning() const noexcept;
  std::size_t howManyReportsAreWaiting() const noexcept { return howManyReportsWaiting_; }
  // How many readings a session remembers (0 when there is no such session).
  std::size_t howManyReadingsSessionRemembers(std::uint32_t sessionId) const noexcept;
  CollectingState collectingStateOfSession(std::uint32_t sessionId) const noexcept;

 private:
  struct Session {
    bool isOpen = false;
    std::uint32_t id = 0;
    LocationHistory history;

    CollectingState collecting = CollectingState::NotRequested;
    std::int64_t collectingEndsAtMs = 0;
    std::int64_t timeBetweenReadingsMs = 0;
    bool hasKeptAReading = false;
    std::int64_t lastReadingKeptAtMs = 0;

    // True when the collecting stopped because its time was over. The
    // reports of this session then say "monitoring_duration_reached", so the
    // app knows why the history is no longer being filled.
    bool collectingTimeIsOver = false;
  };

  struct RequestSlot {
    bool isInUse = false;
    ProximityRequest request;

    // The session it belongs to, as a position in `sessions_`.
    bool hasSession = false;
    std::size_t sessionPosition = 0;

    // Readings of a request that has no session. Wiped when it ends.
    LocationHistory ownHistory;
  };

  // Finding things. Each returns the size of its list when nothing is found.
  std::size_t findSession(std::uint32_t sessionId) const noexcept;
  std::size_t findFreeSessionSlot() const noexcept;
  std::size_t findFreeRequestSlot() const noexcept;
  std::size_t countRequestsOfSession(std::size_t sessionPosition) const noexcept;

  // The readings a request looks at: of its session, or its own.
  const LocationHistory& historyOf(const RequestSlot& slot) const noexcept;

  // Everything the phone reported about its activity, with the activity that
  // is still going on counted until `nowMs`.
  PhoneActivityHistory phoneActivityUntil(std::int64_t nowMs) const noexcept;
  void closeTheActivityThatIsGoingOn(std::int64_t atMs) noexcept;

  void forgetOldThings(std::int64_t nowMs) noexcept;

  void storeOneReading(LocationHistory& history,
                       const ReadingCheck& check,
                       const PhoneActivityHistory& activityHistory,
                       std::int64_t nowMs,
                       NotesAboutNewReadings& notes) noexcept;

  void stopEveryRequest(WhyTheRequestStopped why, std::int64_t nowMs) noexcept;
  void stopCollectingInEverySession() noexcept;

  void putReportInTheWaitingList(const Report& report) noexcept;
  void putReportWithNoRequestInTheWaitingList(std::uint32_t requestId,
                                                 std::uint32_t sessionId,
                                                 const RequestSettings& settings,
                                                 ReasonCode why) noexcept;
  void collectFinishedRequests() noexcept;

  std::array<Session, MAX_OPEN_SESSIONS> sessions_{};
  std::array<RequestSlot, MAX_RUNNING_REQUESTS> requestSlots_{};

  // Reports waiting for the platform, oldest first.
  std::array<Report, MAX_REPORTS_WAITING> reportsWaiting_{};
  std::size_t howManyReportsWaiting_ = 0;

  // Phone activity: the reports that already ended, plus the one going on.
  PhoneActivityHistory activityReportsThatEnded_;
  bool anActivityIsGoingOn_ = false;
  PhoneActivityReport activityGoingOn_;

  // Numbers handed out to sessions and requests. They start at 1 and are
  // never used twice, so an old number can never point at something new.
  std::uint32_t nextSessionId_ = 1;
  std::uint32_t nextRequestId_ = 1;

  bool readingsAreSimulated_ = false;
};

}  // namespace lce

#endif  // LCE_ENGINE_HPP
