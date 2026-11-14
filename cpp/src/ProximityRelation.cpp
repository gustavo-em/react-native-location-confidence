#include "ProximityRelation.hpp"

#include <cmath>

#include "Types.hpp"

namespace lce {

bool isValidRadiusMeters(double radiusMeters) noexcept {
  return std::isfinite(radiusMeters) && radiusMeters > 0.0 && radiusMeters <= kMaxRadiusMeters;
}

EvidenceRelation classifyRelation(double distanceMeters,
                                  double effectiveUncertaintyMeters,
                                  double radiusMeters,
                                  const RelationGates& gates) noexcept {
  const bool gatesMet = gates.hasCandidate && gates.ageReliable && gates.withinFallbackAge &&
                        gates.permissionAuthorized && !gates.strongJumpSuspicion &&
                        !gates.blockedByMockPolicy;
  if (!gatesMet) {
    return EvidenceRelation::Inconclusive;
  }

  const bool inputsUsable = std::isfinite(distanceMeters) && distanceMeters >= 0.0 &&
                            std::isfinite(effectiveUncertaintyMeters) &&
                            effectiveUncertaintyMeters >= 0.0 && isValidRadiusMeters(radiusMeters);
  if (!inputsUsable) {
    return EvidenceRelation::Inconclusive;
  }

  if (distanceMeters + effectiveUncertaintyMeters <= radiusMeters) {
    return EvidenceRelation::SupportsInside;
  }

  const double nearestPossible = distanceMeters - effectiveUncertaintyMeters;
  if ((nearestPossible > 0.0 ? nearestPossible : 0.0) > radiusMeters) {
    return EvidenceRelation::SupportsOutside;
  }

  return EvidenceRelation::Inconclusive;
}

}  // namespace lce
