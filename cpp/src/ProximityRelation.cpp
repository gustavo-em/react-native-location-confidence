#include "ProximityRelation.hpp"

#include <cmath>

#include "Types.hpp"

namespace lce {

namespace {

bool everyTrustCheckPasses(const RelationGates& gates) noexcept {
  return gates.hasCandidate && gates.ageReliable && gates.withinFallbackAge &&
         gates.permissionAuthorized && !gates.strongJumpSuspicion && !gates.blockedByMockPolicy;
}

// A distance must be a normal number (not infinity, not NaN) and not negative.
bool isUsableDistance(double meters) noexcept {
  return std::isfinite(meters) && meters >= 0.0;
}

}  // namespace

bool isValidRadiusMeters(double radiusMeters) noexcept {
  return std::isfinite(radiusMeters) && radiusMeters > 0.0 && radiusMeters <= MAX_RADIUS_METERS;
}

EvidenceRelation classifyRelation(double distanceToDestinationMeters,
                                  double uncertaintyMeters,
                                  double radiusMeters,
                                  const RelationGates& gates) noexcept {
  // If we do not trust the reading, we do not take a side.
  if (!everyTrustCheckPasses(gates)) {
    return EvidenceRelation::Inconclusive;
  }

  // Broken numbers also mean we do not take a side.
  const bool numbersAreUsable = isUsableDistance(distanceToDestinationMeters) &&
                                isUsableDistance(uncertaintyMeters) &&
                                isValidRadiusMeters(radiusMeters);
  if (!numbersAreUsable) {
    return EvidenceRelation::Inconclusive;
  }

  // The phone is somewhere in a circle around the reading. Seen from the
  // delivery address, the phone is therefore:
  //   - at most  (distance + uncertainty) away, and
  //   - at least (distance - uncertainty) away, but never less than 0.
  const double farthestThePhoneCouldBe = distanceToDestinationMeters + uncertaintyMeters;

  const double distanceMinusUncertainty = distanceToDestinationMeters - uncertaintyMeters;
  const double closestThePhoneCouldBe =
      distanceMinusUncertainty > 0.0 ? distanceMinusUncertainty : 0.0;

  // Even in the worst case the phone is inside the delivery circle.
  const bool insideEvenInTheWorstCase = farthestThePhoneCouldBe <= radiusMeters;
  if (insideEvenInTheWorstCase) {
    return EvidenceRelation::SupportsInside;
  }

  // Even in the best case the phone is outside the delivery circle.
  const bool outsideEvenInTheBestCase = closestThePhoneCouldBe > radiusMeters;
  if (outsideEvenInTheBestCase) {
    return EvidenceRelation::SupportsOutside;
  }

  // The phone could be on either side of the border.
  return EvidenceRelation::Inconclusive;
}

}  // namespace lce
