// SoH-3DS: libultra spellings, matching the headers. devkitARM makes
// int32_t `long int`, a different type from s32.
#include "libultraship/libultraship.h"

extern "C" {

void osWritebackDCacheAll() {
}

void osInvalICache(void* p, s32 x) {
}

void osWritebackDCache(void* p, s32 x) {
}

void osInvalDCache(void* p, s32 l) {
}
}