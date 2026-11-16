// Shared helpers for host tests. Synthetic coordinates only.
#ifndef LCE_TEST_SUPPORT_HPP
#define LCE_TEST_SUPPORT_HPP

#include <cstdint>
#include <initializer_list>
#include <random>

#include "GeoMath.hpp"
#include "LocationHistory.hpp"
#include "PhoneActivityTimeline.hpp"
#include "ProximityRelation.hpp"
#include "QualityScore.hpp"
#include "ReadingChecker.hpp"
#include "Types.hpp"

namespace lce_test {

// A reading, not yet checked, that passes the entrance check unless a test
// breaks one of its fields.
inline lce::UncheckedReading validUncheckedReading(std::int64_t capturedAtMs = 1000) {
  lce::UncheckedReading unchecked;
  unchecked.latitudeDegrees = -20.8117;
  unchecked.longitudeDegrees = -49.3768;
  unchecked.hasAccuracy = true;
  unchecked.horizontalAccuracyMeters = 10.0;
  unchecked.capturedAtMs = capturedAtMs;
  unchecked.timeReliable = true;
  return unchecked;
}

inline lce::LocationReading readingAt(std::int64_t capturedAtMs,
                                    double latitudeDegrees = 0.0,
                                    double longitudeDegrees = 0.0,
                                    double accuracyMeters = 10.0) {
  lce::LocationReading reading;
  reading.position = lce::Coordinates{latitudeDegrees, longitudeDegrees};
  reading.horizontalAccuracyMeters = accuracyMeters;
  reading.capturedAtMs = capturedAtMs;
  return reading;
}

// A reading taken `seconds` after the start, `metersNorth` meters north of
// the starting point (latitude 0, longitude 0).
inline lce::LocationReading readingNorth(double seconds,
                                         double metersNorth,
                                         double gpsErrorMeters = 5.0) {
  const double pi = 3.14159265358979323846;
  const double degreesPerMeter = 180.0 / (pi * lce::EARTH_RADIUS_METERS);
  return readingAt(static_cast<std::int64_t>(seconds * 1000.0), metersNorth * degreesPerMeter,
                   0.0, gpsErrorMeters);
}

// A history holding these readings. "Now" is the moment of the newest one,
// so nothing is thrown away for being old.
inline lce::LocationHistory historyWith(std::initializer_list<lce::LocationReading> readings) {
  std::int64_t newestCaptureMs = 0;
  for (const lce::LocationReading& reading : readings) {
    if (reading.capturedAtMs > newestCaptureMs) {
      newestCaptureMs = reading.capturedAtMs;
    }
  }
  lce::LocationHistory history;
  for (const lce::LocationReading& reading : readings) {
    history.store(reading, newestCaptureMs);
  }
  return history;
}

// Seconds written in a test, as the milliseconds the engine works with.
inline std::int64_t secondsToMs(double seconds) {
  return static_cast<std::int64_t>(seconds * 1000.0);
}

// One report from the phone: "from this second to that second I was doing
// this". The phone is very sure unless the test says otherwise.
inline lce::PhoneActivityReport activityReport(
    lce::PhoneActivity activity,
    double fromSeconds,
    double toSeconds,
    lce::PhoneActivityConfidence confidence = lce::PhoneActivityConfidence::High) {
  lce::PhoneActivityReport report;
  report.startedAtMs = secondsToMs(fromSeconds);
  report.endedAtMs = secondsToMs(toSeconds);
  report.activity = activity;
  report.confidence = confidence;
  return report;
}

// Everything the phone reported about its activity.
inline lce::PhoneActivityHistory activityHistoryWith(
    std::initializer_list<lce::PhoneActivityReport> reports) {
  lce::PhoneActivityHistory history;
  for (const lce::PhoneActivityReport& report : reports) {
    history.store(report);
  }
  return history;
}

// The phone reported one single activity.
inline lce::PhoneActivityHistory phoneReported(lce::PhoneActivity activity,
                                               double fromSeconds,
                                               double toSeconds) {
  return activityHistoryWith({activityReport(activity, fromSeconds, toSeconds)});
}

// The phone reported nothing about its activity.
inline lce::PhoneActivityHistory phoneReportedNothing() {
  return lce::PhoneActivityHistory{};
}

// The best possible reading: every grade is 1 and no limit applies.
inline lce::ScoreInput perfectScoreInput() {
  lce::ScoreInput input;
  input.hasCandidate = true;
  input.ageReliable = true;
  input.horizontalAccuracyMeters = 0.0;
  input.ageMs = 0.0;
  input.possibleMovementMeters = 0.0;
  input.movementIsUnknown = false;
  input.consistency = lce::Consistency::ThreeOrMoreAgree;
  input.source = lce::FixSource::Fresh;
  input.onlyApproximatePermission = false;
  input.suspectedJump = false;
  input.mockSignal = lce::MockSignal::NotFlaggedByOs;
  input.mockPolicy = lce::MockPolicy::ZeroScore;
  return input;
}

inline lce::RelationGates openGates() {
  lce::RelationGates gates;
  gates.hasCandidate = true;
  gates.ageReliable = true;
  gates.withinFallbackAge = true;
  gates.permissionAuthorized = true;
  gates.strongJumpSuspicion = false;
  gates.blockedByMockPolicy = false;
  return gates;
}

// Deterministic generator. std::mt19937_64 is fully specified by the
// standard; the standard distributions are not, so the mapping to a range
// is done here to get the same sequence on every standard library.
class DeterministicRng {
 public:
  explicit DeterministicRng(std::uint64_t seed) : engine_(seed) {}

  // Uniform in [0, 1).
  double unit() { return static_cast<double>(engine_() >> 11) * (1.0 / 9007199254740992.0); }

  double uniform(double low, double high) { return low + (high - low) * unit(); }

  std::uint64_t below(std::uint64_t bound) { return engine_() % bound; }

 private:
  std::mt19937_64 engine_;
};

}  // namespace lce_test

#endif  // LCE_TEST_SUPPORT_HPP
