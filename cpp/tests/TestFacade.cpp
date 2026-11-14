#include "third_party/doctest.h"

#include "location_confidence/lce.h"

TEST_CASE("facade: ABI version matches the header") {
  CHECK(lce_abi_version() == LCE_ABI_VERSION);
}
