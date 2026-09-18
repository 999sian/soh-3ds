#pragma once
#include <stddef.h>
#include <stdint.h>

// Standard binary OSFT v2: 64-byte resource header, then the font index.
// Unknown/custom formats return -1 so the caller uses the full importer.
static inline int Soh3dsReadFontIndex(const uint8_t* data, size_t size) {
    if (!data || size < 68 || data[0] > 1) return -1;
    const bool big = data[0] == 1;
    const auto read32 = [big](const uint8_t* p) -> uint32_t {
        if (big) return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
        return p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
    };
    if (read32(data + 4) != 0x4F534654 || read32(data + 8) != 2) return -1;
    const uint32_t index = read32(data + 64);
    return index <= INT32_MAX ? (int)index : -1;
}
