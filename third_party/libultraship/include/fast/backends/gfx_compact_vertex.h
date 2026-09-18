#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace Fast {
// Version 1 retains every byte colour input, including uniforms that can change
// within a batch. Position/UV arithmetic remains identical to the float stream.
struct CompactVertexLayout {
    uint8_t version = 1;
    uint8_t numInputs = 0;
    uint8_t textureMask = 0;
    bool alpha = false;
    bool fog = false;

    size_t StrideFloats() const {
        return 4 + ((textureMask & 1) ? 2 : 0) + ((textureMask & 2) ? 2 : 0) +
               (fog ? 4 : 0) + numInputs * (alpha ? 4 : 3);
    }
};

struct CompactVertex {
    float position[4];
    float texcoord[2][2];
    uint8_t input[2][4];
    uint8_t fog[4];
};
static_assert(sizeof(CompactVertex) == 44, "Compact stream version 1 ABI");

// The allocation is float[], never an array of CompactVertex objects. memcpy
// preserves object lifetime/aliasing rules on both ARM11 and desktop compilers.
inline CompactVertex ReadCompactVertex(const void* storage, size_t vertex) {
    CompactVertex result;
    std::memcpy(&result, static_cast<const unsigned char*>(storage) + vertex * sizeof(result), sizeof(result));
    return result;
}
inline void WriteCompactVertex(void* storage, size_t vertex, const CompactVertex& record) {
    std::memcpy(static_cast<unsigned char*>(storage) + vertex * sizeof(record), &record, sizeof(record));
}
inline void ExpandCompactVertex(float* destination, const CompactVertex& record, const CompactVertexLayout& layout) {
    std::memcpy(destination, record.position, sizeof(record.position));
    destination += 4;
    for (unsigned texture = 0; texture < 2; ++texture) {
        if (layout.textureMask & (1u << texture)) {
            *destination++ = record.texcoord[texture][0];
            *destination++ = record.texcoord[texture][1];
        }
    }
    constexpr float kInv255 = 1.0f / 255.0f;
    if (layout.fog) {
        for (unsigned channel = 0; channel < 4; ++channel) *destination++ = record.fog[channel] * kInv255;
    }
    for (unsigned input = 0; input < layout.numInputs; ++input) {
        for (unsigned channel = 0; channel < (layout.alpha ? 4u : 3u); ++channel) {
            *destination++ = record.input[input][channel] * kInv255;
        }
    }
}
inline void ExpandCompactVerticesInPlace(float* storage, size_t count, const CompactVertexLayout& layout) {
    const size_t stride = layout.StrideFloats();
    // Small float layouts shrink. Expanding those backwards would overwrite
    // unread records. Take a local record before either direction writes.
    for (size_t step = 0; step < count; ++step) {
        const size_t vertex = stride * sizeof(float) < sizeof(CompactVertex) ? step : count - 1 - step;
        const CompactVertex record = ReadCompactVertex(storage, vertex);
        ExpandCompactVertex(storage + vertex * stride, record, layout);
    }
}
} // namespace Fast
