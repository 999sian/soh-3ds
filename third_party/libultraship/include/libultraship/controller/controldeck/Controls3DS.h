#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// Matches a libultraship SDL button-mapping ID to the stable physical-button
// index used by the native 3DS controls bridge (A, B, X, Y, L, R, ZL, ZR,
// D-pad U/D/L/R, Start, Select). Kept independent of SDL headers so the
// persistence/query policy can be regression-tested on the host.
bool Soh3dsControls_MappingIdMatches(int physical, const char* mappingId);

#ifdef __cplusplus
}
#endif
