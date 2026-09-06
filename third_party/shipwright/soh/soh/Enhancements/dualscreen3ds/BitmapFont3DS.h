#ifndef SOH_BITMAP_FONT_3DS_H
#define SOH_BITMAP_FONT_3DS_H
#include <stdint.h>

// libctru's existing console font has eight MSB-first rows per glyph. Expand
// into I8 for Fast3D (one 64-bit tile row per glyph row), trimming only empty side columns. Every lit font pixel
// remains one LCD pixel; no downsampling or antialiased outline is involved.
static inline uint8_t BsFont_Expand(const uint8_t rows[8], uint8_t image[64]) {
    uint8_t occupied = 0;
    for (int y = 0; y < 8; ++y) occupied |= rows[y];
    int left = 0, right = 7;
    if (occupied != 0) {
        while (!(occupied & (0x80 >> left))) ++left;
        while (!(occupied & (0x80 >> right))) --right;
    }
    for (int y = 0; y < 8; ++y) {
        uint8_t row = (uint8_t)(rows[y] << left);
        for (int x = 0; x < 8; ++x) {
            image[y * 8 + x] = (row & (0x80 >> x)) ? 255 : 0;
        }
    }
    return occupied ? (uint8_t)(right - left + 2) : 4;
}
#endif
