#pragma once
#include <stdint.h>
// Internal CPU mixer image. The bridge must verify sizeof/offsetof against rspa;
// it is not an on-disk or DSP ABI. One audio worker owns it and all host pointers.
typedef struct DspMixerImage {
    uint16_t in,out,nbytes;
    uint16_t vol[2],rate[2],vol_wet,rate_wet;
    uintptr_t adpcm_loop_state;
    int16_t adpcm_table[8][2][8];
    uint16_t filter_count;
    int16_t filter[8];
    union {int16_t as_s16[1536];uint8_t as_u8[3072];} buf;
} DspMixerImage;
