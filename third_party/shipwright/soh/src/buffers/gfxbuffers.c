#include "z64.h"

#ifdef __3DS__
// SoH-3DS: on N64, RSP microcode wrote RDP FIFO commands here. In SoH, Fast3D
// interprets display lists directly from workBuffer; nothing reads or writes this.
u64 gGfxSPTaskOutputBuffer[1];
#else
// 0x18000 bytes
u64 gGfxSPTaskOutputBuffer[0x3000];
#endif

// 0xC00 bytes
u8 gGfxSPTaskYieldBuffer[OS_YIELD_DATA_SIZE];

// 0x400 bytes
u8 gGfxSPTaskStack[0x400];

// 0x12410 bytes each; 0x24820 bytes total
GfxPool gGfxPools[2];
