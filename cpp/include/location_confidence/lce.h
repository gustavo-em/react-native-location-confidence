/*
 * Location Confidence Engine - C facade.
 *
 * This header is the only entry point into the shared core for both
 * platforms (JNI on Android, direct C import from Swift on iOS).
 * It must stay plain C: no C++ includes, no Objective-C.
 *
 * Phase 0.5 (integration spike): only the ABI version probe exists.
 */
#ifndef LOCATION_CONFIDENCE_LCE_H
#define LOCATION_CONFIDENCE_LCE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Bumped whenever the layout or semantics of this facade change. */
#define LCE_ABI_VERSION 1u

/* Returns LCE_ABI_VERSION as compiled into the core. Never fails. */
uint32_t lce_abi_version(void);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* LOCATION_CONFIDENCE_LCE_H */
