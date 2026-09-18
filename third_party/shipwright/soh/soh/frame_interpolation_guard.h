#pragma once

// Keep this header independent of game types: macros.h is included while those
// types are still being declared, by both C and C++ translation units.
#ifndef __cplusplus
#include <stdbool.h>
#endif
#ifdef __cplusplus
extern "C" {
#endif
#ifdef __3DS__
// Set once by the early model query; false on Old 3DS or query failure.
extern bool gSoh3dsFrameInterpolationEnabled;
#endif
void FrameInterpolation_RecordOpenChild(const void* a, int b);
void FrameInterpolation_RecordCloseChild(void);
#ifdef __cplusplus
}
#endif

#if defined(__3DS__) && !defined(SOH_FRAME_INTERPOLATION_IMPLEMENTATION)
// Test the cached model flag at the call site, before evaluating arguments.
#define SOH_FRAME_INTERPOLATION_CALL(function, ...) \
    (gSoh3dsFrameInterpolationEnabled ? (function)(__VA_ARGS__) : (void)0)
#define FrameInterpolation_RecordOpenChild(...) SOH_FRAME_INTERPOLATION_CALL(FrameInterpolation_RecordOpenChild, __VA_ARGS__)
#define FrameInterpolation_RecordCloseChild(...) SOH_FRAME_INTERPOLATION_CALL(FrameInterpolation_RecordCloseChild, __VA_ARGS__)
#endif
