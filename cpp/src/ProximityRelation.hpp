// Geometric relation between a measured fix and a circular destination.
//
// "Supports inside" means the evidence is compatible with being inside the
// radius. It is never a confirmation of physical presence and never an
// authorization for the host app's business decision.
#ifndef LCE_PROXIMITY_RELATION_HPP
#define LCE_PROXIMITY_RELATION_HPP

#include <cstdint>

namespace lce {

enum class EvidenceRelation : std::uint8_t { SupportsInside, SupportsOutside, Inconclusive };

// Integrity gates. Quality thresholds are deliberately absent: the effective
// uncertainty already carries accuracy and age, so applying them again would
// only produce false "inconclusive" results for large radii.
struct RelationGates {
  bool hasCandidate = false;
  bool ageReliable = false;
  bool withinFallbackAge = false;
  bool permissionAuthorized = false;
  bool strongJumpSuspicion = false;
  bool blockedByMockPolicy = false;
};

// Finite, greater than zero and at most kMaxRadiusMeters.
bool isValidRadiusMeters(double radiusMeters) noexcept;

// distanceMeters: fix to destination center. effectiveUncertaintyMeters:
// conservative margin around the fix. Comparisons use no tolerance: exactly
// d + u == r supports inside, exactly d - u == r stays inconclusive.
EvidenceRelation classifyRelation(double distanceMeters,
                                  double effectiveUncertaintyMeters,
                                  double radiusMeters,
                                  const RelationGates& gates) noexcept;

}  // namespace lce

#endif  // LCE_PROXIMITY_RELATION_HPP
