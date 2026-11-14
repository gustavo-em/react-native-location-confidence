// The code behind the front door declared in lce.h.
//
// Each function here will be a thin translator: it receives plain C values
// from Kotlin/Swift, calls the C++ modules of the engine (reading checker,
// history, score...) and returns plain C values. No business rule lives in
// this file.
#include "location_confidence/lce.h"

// `extern "C"` must match the declaration in lce.h so that both sides agree
// on the function's name.
extern "C" uint32_t lce_abi_version(void) {
  return LCE_ABI_VERSION;
}
