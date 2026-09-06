#pragma once

#include <stdint.h>

#include "SettingsBridge3DS.h"

#ifdef __cplusplus
extern "C" {
#endif

enum Soh3dsPhysicalButton {
    SOH3DS_PHYS_A = 1u << 0,
    SOH3DS_PHYS_B = 1u << 1,
    SOH3DS_PHYS_X = 1u << 2,
    SOH3DS_PHYS_Y = 1u << 3,
    SOH3DS_PHYS_L = 1u << 4,
    SOH3DS_PHYS_R = 1u << 5,
    SOH3DS_PHYS_ZL = 1u << 6,
    SOH3DS_PHYS_ZR = 1u << 7,
    SOH3DS_PHYS_DUP = 1u << 8,
    SOH3DS_PHYS_DDOWN = 1u << 9,
    SOH3DS_PHYS_DLEFT = 1u << 10,
    SOH3DS_PHYS_DRIGHT = 1u << 11,
    SOH3DS_PHYS_START = 1u << 12,
    SOH3DS_PHYS_SELECT = 1u << 13,
};

int Soh3dsControls_RowCount(void);
int Soh3dsControls_Row(int row, Soh3dsSettingsRow* out);
void Soh3dsControls_Adjust(int row, int dir);

// Called by libultraship after its normal controller read. Current/manual is
// an exact pass-through. Managed layouts replace the N64 digital button field
// from the raw 3DS buttons while leaving both analogue sticks untouched.
void Soh3dsControls_TransformPad(uint32_t physicalButtons, uint16_t* buttons);

#ifdef __cplusplus
}
#endif
