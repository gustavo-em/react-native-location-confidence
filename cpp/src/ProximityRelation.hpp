// Answers one question: does the evidence say the phone is inside the
// delivery radius, outside it, or can we not tell?
//
// The idea: a GPS reading is not an exact point. The phone could really be
// anywhere within some distance of it (the "uncertainty"). So we imagine a
// circle of uncertainty around the reading and compare it with the delivery
// circle:
//
//   uncertainty circle entirely inside the delivery circle  -> SupportsInside
//   uncertainty circle entirely outside the delivery circle -> SupportsOutside
//   the two circles cross each other                        -> Inconclusive
//
// Example with a 100 m radius:
//   reading 30 m from the address, uncertainty 15 m -> at most 45 m away  -> inside
//   reading 250 m away, uncertainty 20 m            -> at least 230 m away -> outside
//   reading 95 m away, uncertainty 20 m             -> between 75 and 115  -> cannot tell
//
// "SupportsInside" means the evidence is compatible with being there. It is
// never proof that a person was physically present, and it is never an
// approval of the delivery. That decision belongs to the app.
#ifndef LCE_PROXIMITY_RELATION_HPP
#define LCE_PROXIMITY_RELATION_HPP

#include <cstdint>

namespace lce {

enum class EvidenceRelation : std::uint8_t { SupportsInside, SupportsOutside, Inconclusive };

// Conditions that must ALL be fine before we are willing to say "inside" or
// "outside". If any of them fails, the answer is "inconclusive" no matter
// what the geometry says. (A "gate" is simply a yes/no checkpoint.)
//
// These are trust checks only. There is deliberately no "accuracy must be
// below X meters" here: a poor accuracy already makes the uncertainty circle
// bigger, so it is taken into account by the geometry itself.
struct RelationGates {
  bool hasCandidate = false;          // we have a reading to judge
  bool ageReliable = false;           // we trust how old the reading is
  bool withinFallbackAge = false;     // it is not older than the app allows
  bool permissionAuthorized = false;  // location permission is granted
  bool strongJumpSuspicion = false;   // must be false: the reading looks like a GPS glitch
  bool blockedByMockPolicy = false;   // must be false: flagged as simulated and the app forbids it
};

// Is this an acceptable delivery radius? (a normal number, above 0, at most 100 km)
bool isValidRadiusMeters(double radiusMeters) noexcept;

// Compares the reading with the delivery circle.
//
//   distanceToDestinationMeters: from the reading to the center of the delivery circle
//   uncertaintyMeters:           how far the phone could really be from the reading
//   radiusMeters:                size of the delivery circle
//
// Exactly on the border counts as inside when "farthest possible == radius",
// and as inconclusive when "closest possible == radius". No rounding margin
// is used, because a margin would make the answer less cautious.
EvidenceRelation classifyRelation(double distanceToDestinationMeters,
                                  double uncertaintyMeters,
                                  double radiusMeters,
                                  const RelationGates& gates) noexcept;

}  // namespace lce

#endif  // LCE_PROXIMITY_RELATION_HPP
