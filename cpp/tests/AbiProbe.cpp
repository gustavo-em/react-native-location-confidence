// Phase 0.5 host probe: proves the core builds and links standalone,
// without React Native or any platform SDK.
#include "location_confidence/lce.h"

#include <cstdint>
#include <cstdio>

int main() {
  const std::uint32_t version = lce_abi_version();
  std::printf("lce_abi_version=%u\n", static_cast<unsigned>(version));
  return version == LCE_ABI_VERSION ? 0 : 1;
}
