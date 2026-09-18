#pragma once
#include <array>
#include <cstdint>

// Only colour-independent plans live here. OoT's subtract folds and split
// interpolations still resolve from the current constants and varying input.
struct CompiledChannel3DS {
    uint8_t function = 0;
    std::array<uint8_t, 3> source = {};
    bool valid = false;
};
