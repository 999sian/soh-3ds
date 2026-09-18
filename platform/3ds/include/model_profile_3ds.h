#pragma once
#include <stdint.h>

// Heap setup precedes APT initialization: use the granted application budget,
// not a hardware query, to leave enough ordinary heap on constrained launches.
constexpr uint32_t Soh3dsLinearHeapBytes(uint64_t applicationBytes) {
    return (applicationBytes > 96ULL * 1024 * 1024 ? 16U : 9U) * 1024 * 1024;
}

constexpr bool Soh3dsUseOldProfile(bool modelQuerySucceeded, bool isNewModel) {
    return !modelQuerySucceeded || !isNewModel;
}
