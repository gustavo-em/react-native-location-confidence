/*
 * lce.h - the front door of the Location Confidence Engine ("lce").
 *
 * The engine is written in C++. The platform code that uses it is not:
 * Android uses Kotlin (through a small JNI file) and iOS uses Swift.
 * Both of them can call plain C functions easily, so the engine exposes a
 * small set of C functions here and hides all of its C++ behind them.
 *
 * This file is a HEADER: it only announces which functions exist and what
 * they take and return. The code that runs is in cpp/src/CFacade.cpp.
 *
 * Rule for this file: plain C only. No C++ and no Objective-C, otherwise
 * Swift can no longer read it.
 *
 * Today only one function exists (the version check). The real functions
 * (store readings, evaluate proximity) are added in a later phase.
 */
#ifndef LOCATION_CONFIDENCE_LCE_H
#define LOCATION_CONFIDENCE_LCE_H

#include <stdint.h>

/* Tells a C++ compiler "these functions use C naming rules". Without this,
 * C++ would rename them internally and C or Swift code could not find them.
 * A C compiler skips these lines. */
#ifdef __cplusplus
extern "C" {
#endif

/* Version of this front door. Increase it whenever a function here changes
 * what it takes, what it returns or what it means. */
#define LCE_ABI_VERSION 1u

/* Returns the version above, as it was when the engine was compiled.
 *
 * Why it exists: the JavaScript package and the compiled engine are built
 * separately. If an app ends up with a new JavaScript and an old engine,
 * comparing versions lets us fail with a clear error instead of
 * misbehaving. It is also the simplest possible call to prove that
 * JavaScript can reach the engine. It cannot fail. */
uint32_t lce_abi_version(void);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* LOCATION_CONFIDENCE_LCE_H */
