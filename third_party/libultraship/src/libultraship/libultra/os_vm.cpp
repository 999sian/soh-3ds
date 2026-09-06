// SoH-3DS: libultra spellings, matching the headers. devkitARM makes
// int32_t `long int`, a different type from s32.
#include "libultraship/libultraship.h"

extern "C" {

uintptr_t osVirtualToPhysical(void* addr) {
    return (uintptr_t)addr;
}

void osMapTLB(s32 a, u32 b, void* c, u32 d, u32 e, u32 f) {
}
}