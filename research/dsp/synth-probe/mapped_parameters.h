#pragma once
#include <array>
#include "dmem_layout.h"
#include "resident_session.h"
namespace ResidentDsp {
constexpr uint16_t ParameterWordBase=0x6000;
constexpr unsigned ParameterSlots=64,ParameterWords=16;
constexpr uint16_t AdpcmBookWordBase=0x5600,AdpcmStateWordBase=0x6800,AdpcmLoopWordBase=0x6c00;
constexpr unsigned AdpcmBookSlots=16,AdpcmBookWords=128;
constexpr uint16_t FilterStateWordBase=0x7000,FilterCoefficientWordBase=0x7400,FilterCountWord=0x7408;
// Per-command immutable record during an in-flight batch. Addresses are sample
// offsets into audio DMEM, not DSP addresses: input,dryL,dryR,wetL,wetR;
// then envelope flags, volumes L/R/wet, rates L/R/wet, gain, reserved[3].
using MappedParameters=std::array<uint16_t,ParameterWords>;
inline bool lowerSampleAddress(uint16_t address,unsigned samples,uint16_t& offset){
    if(address<AudioDmemStart)return false;
    unsigned byteOffset=(address-AudioDmemStart)&~1u; // BUF_S16 division
    if(byteOffset>AudioDmemBytes || samples>(AudioDmemBytes-byteOffset)/2)return false;
    offset=uint16_t(byteOffset/2);return true;
}
inline bool lowerMappedLoad(uint16_t payloadOffset,uint16_t destination,uint16_t bytes,Command& command){
    if(payloadOffset>PayloadBytes || bytes>PayloadBytes-payloadOffset || !audioDmemExtent(destination,bytes))return false;
    command={24,bytes,payloadOffset,uint16_t(destination-AudioDmemStart)};return true;
}
inline bool lowerMappedHiLo(unsigned slot,uint8_t gain,uint16_t bytes,uint16_t input,
                           Command& command,MappedParameters& parameters){
    if(slot>=ParameterSlots)return false;
    // Production consumes eight samples but subtracts eight from its byte counter.
    unsigned count=(unsigned(bytes)+31)&~31u;if(!count)count=8;
    MappedParameters next{};if(!lowerSampleAddress(input,count,next[0]))return false;next[1]=gain;
    parameters=next;command={22,uint16_t(count),uint16_t(slot),0};return true;
}
inline bool lowerMappedTableMultiply(unsigned slot,uint8_t offset,uint16_t bytes,uint16_t input,uint16_t output,
                                    Command& command,MappedParameters& parameters){
    if(slot>=ParameterSlots || unsigned(input)+offset>65535)return false;
    unsigned count=((unsigned(bytes)+63)&~63u)/2;if(!count)count=32;
    MappedParameters next{};if(!lowerSampleAddress(input+offset,32,next[0]) || !lowerSampleAddress(output,count,next[1]))return false;
    parameters=next;command={23,uint16_t(count),uint16_t(slot),0};return true;
}
inline bool lowerMappedFilterSetup(unsigned slot,uint16_t bytes,const std::array<int16_t,8>& coefficients,
                                   Command& command,MappedParameters& parameters){
    // Production stores ROUND_UP_16 in uint16_t, including wrap to zero.
    unsigned count=uint16_t((unsigned(bytes)+15)&~15u)/2;
    if(slot>=ParameterSlots || count>AudioDmemBytes/2)return false;
    MappedParameters next{};for(unsigned i=0;i<8;++i)next[i]=uint16_t(coefficients[i]);
    parameters=next;command={20,uint16_t(count),uint16_t(slot),0};return true;
}
inline bool lowerMappedFilter(unsigned slot,uint16_t input,unsigned stateSlot,unsigned flags,
                              Command& command,MappedParameters& parameters){
    if(slot>=ParameterSlots || stateSlot>=64 || flags>1)return false;
    MappedParameters next{};if(!lowerSampleAddress(input,0,next[0]))return false;
    next[1]=stateSlot;next[2]=flags;
    parameters=next;command={21,0,uint16_t(slot),0};return true;
}
// Input is an N64 byte address, or an offset into the immutable payload bank.
inline bool lowerMappedS8(unsigned slot,uint16_t bytes,uint16_t input,uint16_t output,
                         unsigned flags,unsigned stateSlot,unsigned loopSlot,bool staged,
                         Command& command,MappedParameters& parameters){
    if(slot>=ParameterSlots || flags>2 || stateSlot>=64 || loopSlot>=64)return false;
    unsigned samples=((unsigned(bytes)+31)&~31u)/2;MappedParameters next{};
    if(!lowerSampleAddress(output,samples+16,next[1]))return false;
    if(staged){if(input>PayloadBytes || samples>PayloadBytes-input)return false;next[0]=input;}
    else{if(!audioDmemExtent(input,samples))return false;next[0]=input-AudioDmemStart;}
    next[2]=stateSlot;next[3]=loopSlot;next[5]=flags;next[6]=staged;
    parameters=next;command={19,uint16_t(samples),uint16_t(slot),0};return true;
}
inline bool lowerMappedDuplicate(unsigned slot,uint16_t repeats,uint16_t input,uint16_t output,
                                 Command& command,MappedParameters& parameters){
    unsigned copies=unsigned(repeats)+1;
    if(slot>=ParameterSlots || copies>AudioDmemBytes/128 || !audioDmemExtent(input,128) || !audioDmemExtent(output,copies*128))return false;
    MappedParameters next{};next[0]=input-AudioDmemStart;next[1]=output-AudioDmemStart;
    parameters=next;command={18,uint16_t(copies),uint16_t(slot),0};return true;
}
inline bool lowerMappedZoh(unsigned slot,uint16_t bytes,uint16_t input,uint16_t output,
                           uint16_t pitch,uint16_t phase,Command& command,MappedParameters& parameters){
    if(slot>=ParameterSlots)return false;
    unsigned count=((unsigned(bytes)+7)&~7u)/2;if(!count)count=4;
    if(count>AudioDmemBytes/2)return false;
    unsigned sourceSamples=((uint32_t(phase)+uint32_t(count-1)*pitch*4)>>17)+1;
    MappedParameters next{};
    if(!lowerSampleAddress(input,sourceSamples,next[0]) || !lowerSampleAddress(output,count,next[1]))return false;
    next[2]=pitch;next[3]=phase;
    parameters=next;command={17,uint16_t(count),uint16_t(slot),0};return true;
}
inline bool lowerMappedAdd(unsigned slot,uint16_t bytes,uint16_t input,uint16_t output,
                           Command& command,MappedParameters& parameters){
    if(slot>=ParameterSlots)return false;
    unsigned samples=(((unsigned(bytes)&~15u)+63)&~63u)/2;
    if(!samples)samples=16; // production do/while executes once for zero bytes
    MappedParameters next{};
    if(!lowerSampleAddress(input,samples,next[0]) || !lowerSampleAddress(output,samples,next[1]))return false;
    parameters=next;command={15,uint16_t(samples),uint16_t(slot),0};return true;
}
inline bool lowerMappedInterl(unsigned slot,uint16_t samples,uint16_t input,uint16_t output,
                              Command& command,MappedParameters& parameters){
    if(slot>=ParameterSlots)return false;
    unsigned actual=(unsigned(samples)+7)&~7u;if(!actual)actual=8;
    MappedParameters next{};
    if(!lowerSampleAddress(input,actual*2,next[0]) || !lowerSampleAddress(output,actual,next[1]))return false;
    parameters=next;command={16,uint16_t(actual),uint16_t(slot),0};return true;
}
inline bool lowerMappedGain(unsigned slot,uint16_t count,int16_t gain,uint16_t input,uint16_t output,
                            Command& command,MappedParameters& parameters){
    if(slot>=ParameterSlots)return false;
    unsigned bytes=(unsigned(count)*16+31)&~31u;
    MappedParameters next{};
    if(!lowerSampleAddress(input,bytes/2,next[0]) || !lowerSampleAddress(output,bytes/2,next[1]))return false;
    next[12]=uint16_t(gain);
    parameters=next;command={9,uint16_t(bytes/2),uint16_t(slot),0};return true;
}
inline bool lowerMappedAdpcm(unsigned slot,bool twoBit,uint16_t bytes,uint16_t input,uint16_t output,
                             unsigned flags,unsigned stateSlot,unsigned loopSlot,unsigned bookSlot,
                             Command& command,MappedParameters& parameters){
    if(slot>=ParameterSlots || flags>2 || stateSlot>=64 || loopSlot>=64 || bookSlot>=AdpcmBookSlots)return false;
    unsigned samples=((unsigned(bytes)+31)&~31u)/2,packed=samples/16*(twoBit?5:9);
    MappedParameters next{};
    if(!audioDmemExtent(input,packed) || !lowerSampleAddress(output,samples+16,next[1]))return false;
    unsigned source=input-AudioDmemStart,destination=next[1]*2,outputBytes=(samples+16)*2;
    if(packed && source<destination+outputBytes && destination<source+packed)return false;
    next[0]=source;next[2]=stateSlot;next[3]=loopSlot;next[4]=bookSlot;next[5]=flags;
    parameters=next;command={uint16_t(twoBit?14:13),uint16_t(samples),uint16_t(slot),0};return true;
}
inline bool lowerStagedAdpcm(unsigned slot,bool twoBit,uint16_t bytes,uint16_t payloadOffset,uint16_t output,
                             unsigned flags,unsigned stateSlot,unsigned loopSlot,unsigned bookSlot,
                             Command& command,MappedParameters& parameters){
    if(slot>=ParameterSlots || flags>2 || stateSlot>=64 || loopSlot>=64 || bookSlot>=AdpcmBookSlots)return false;
    unsigned samples=((unsigned(bytes)+31)&~31u)/2,packed=samples/16*(twoBit?5:9);
    MappedParameters next{};
    if(payloadOffset>PayloadBytes || packed>PayloadBytes-payloadOffset || !lowerSampleAddress(output,samples+16,next[1]))return false;
    next[0]=payloadOffset;next[2]=stateSlot;next[3]=loopSlot;next[4]=bookSlot;next[5]=flags;next[6]=1;
    parameters=next;command={uint16_t(twoBit?14:13),uint16_t(samples),uint16_t(slot),0};return true;
}
inline bool lowerMappedResample(unsigned slot,uint16_t bytes,uint16_t input,uint16_t output,uint16_t pitch,
                                unsigned flags,unsigned stateSlot,Command& command,MappedParameters& parameters){
    if(slot>=ParameterSlots || stateSlot>=64 || flags>2)return false;
    unsigned samples=bytes?((unsigned(bytes)+15)&~15u)/2:8;
    MappedParameters next{};
    if(!lowerSampleAddress(input,0,next[0]) || next[0]<(flags==2?8:4) || !lowerSampleAddress(output,samples,next[1]))return false;
    next[2]=stateSlot;next[3]=pitch;next[4]=flags;
    parameters=next;command={12,uint16_t(samples),uint16_t(slot),0};return true;
}
inline bool lowerMappedInterleave(unsigned slot,uint16_t channelBytes,uint16_t left,uint16_t right,uint16_t destination,
                                  Command& command,MappedParameters& parameters){
    if(slot>=ParameterSlots)return false;
    unsigned samples=((unsigned(channelBytes)+7)&~7u)/2;
    MappedParameters next{};
    if(!lowerSampleAddress(left,samples,next[0]) || !lowerSampleAddress(right,samples,next[1]) ||
       !lowerSampleAddress(destination,samples*2,next[2]))return false;
    parameters=next;command={11,uint16_t(samples),uint16_t(slot),0};return true;
}
inline bool lowerMappedEnvelope(unsigned slot,uint16_t samples,const std::array<uint16_t,5>& addresses,
                                uint16_t flags,const std::array<uint16_t,3>& volumes,
                                const std::array<uint16_t,3>& rates,Command& command,MappedParameters& parameters){
    if(slot>=ParameterSlots || flags>31)return false;
    unsigned actual=samples?(unsigned(samples)+15)&~15u:8;
    MappedParameters next{};
    for(unsigned i=0;i<5;++i)if(!lowerSampleAddress(addresses[i],actual,next[i]))return false;
    next[5]=flags;
    for(unsigned i=0;i<3;++i){next[6+i]=volumes[i];next[9+i]=rates[i];}
    parameters=next;command={10,uint16_t(actual),uint16_t(slot),0};return true;
}
}
