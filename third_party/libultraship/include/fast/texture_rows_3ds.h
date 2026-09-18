#pragma once
#include <cstdint>

namespace Fast {
// ARM11 lacks integer division. Texture strides normally need only a shift.
inline uint32_t TextureRows3DS(uint32_t bytes, uint32_t stride) {
    if (stride != 0 && (stride & (stride - 1)) == 0) return bytes >> __builtin_ctz(stride);
    return bytes / (stride == 0 ? 1 : stride);
}
}
