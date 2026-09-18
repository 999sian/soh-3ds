#pragma once
#include <cstdint>
namespace ResidentDsp {
constexpr uint32_t AudioDmemStart=0x3c0;
constexpr uint32_t AudioDmemBytes=0xc00;
constexpr uint16_t AudioDmemWordBase=0x5000;
constexpr uint16_t PayloadWordBase=0x4100;
constexpr unsigned PayloadBytes=(AudioDmemWordBase-PayloadWordBase)*2;
// Exact byte extents after lowering the production command's rounding rules.
// Submit only after copying these fields into the DSP transfer parameters.
struct DmemTransfer {uint16_t source,destination,bytes,operation;};
inline bool audioDmemExtent(uint32_t address,uint32_t bytes){
    return address>=AudioDmemStart && address-AudioDmemStart<=AudioDmemBytes &&
        bytes<=AudioDmemBytes-(address-AudioDmemStart);
}
inline bool lowerDmemTransfer(bool clear,uint16_t source,uint16_t destination,int bytes,DmemTransfer& result){
    if(bytes<0 || uint32_t(bytes)>AudioDmemBytes)return false;
    uint32_t rounded=(uint32_t(bytes)+15)&~15u;
    if(!audioDmemExtent(destination,rounded) || (!clear && !audioDmemExtent(source,rounded)))return false;
    result={uint16_t(clear?0:source-AudioDmemStart),uint16_t(destination-AudioDmemStart),uint16_t(rounded),uint16_t(clear)};
    return true;
}
}
