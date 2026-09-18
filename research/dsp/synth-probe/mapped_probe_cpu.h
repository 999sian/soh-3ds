#pragma once
#include <array>
#include "cpu_replay.h"
struct MappedProbeFixture {
    std::array<uint8_t,18> compressed{};
    std::array<int16_t,16> decoder{},resampler{},s8{},filter{},loop{},output{};
    std::array<int16_t,128> book{};
    std::array<int16_t,8> coeff={1000,-3000,4000,12000,-10000,2000,500,700};
    bool continueHistory=false;
    void sequence();
};
void mappedProbeExecute(const ResidentDsp::CpuCall&,const void*);

struct StreamProbeFixture {
    std::array<int16_t,512> input{},output{};
    std::array<int16_t,16> resampler{},filter{};
    std::array<int16_t,8> coefficients={0,0,0,0,0,0,0,32767};
    bool continuation=false;
    void sequence();
};
