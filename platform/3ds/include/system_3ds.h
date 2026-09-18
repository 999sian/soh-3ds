#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

uint64_t Mk64System3DSGetTick(void);
uint64_t Mk64System3DSTicksPerSecond(void);
// CPU-to-GPU/DSP ownership only; GPU readback still requires invalidation.
bool Soh3dsCleanDataCache(const void* address, size_t size);
bool Soh3dsInvalidateDataCache(const void* address, size_t size);
void Soh3dsCopyWordsArm11(void* dst, const void* src, uint32_t words);
const char* Soh3dsDataCacheMode(void);

#ifdef __cplusplus
}
#endif
