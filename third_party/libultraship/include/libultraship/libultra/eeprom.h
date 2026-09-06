#pragma once

// SoH-3DS: libultra spellings, so this header agrees with types.h.
// devkitARM makes int32_t `long int`, which is a different type from s32.
#include "types.h"

#include "message.h"

#define EEPROM_TYPE_4K 0x01
#define EEPROM_TYPE_16K 0x02

#ifdef __cplusplus
extern "C" {
#endif

s32 osEepromProbe(OSMesgQueue*);
s32 osEepromLongRead(OSMesgQueue*, uint8_t, uint8_t*, s32);
s32 osEepromLongWrite(OSMesgQueue*, uint8_t, uint8_t*, s32);

#ifdef __cplusplus
}
#endif
