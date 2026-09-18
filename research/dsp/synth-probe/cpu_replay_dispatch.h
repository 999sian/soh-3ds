#pragma once
#include "cpu_replay.h"
extern "C" {
#include "mixer.h"
}
namespace ResidentDsp {
// This calls the CPU implementation names. Production interception must be
// disabled during replay, or call renamed CPU bodies, to avoid recapture.
inline void executeCpuCall(const CpuCall& c,const void* source){
    const auto& a=c.args;auto* state=reinterpret_cast<int16_t*>(c.pointer);
    switch(c.op){
    case CpuOp::Clear:aClearBufferImpl(a[0],int32_t(a[1]));break;
    case CpuOp::Load:aLoadBufferImpl(source,a[0],a[1]);break;
    case CpuOp::Save:aSaveBufferImpl(a[0],state,a[1]);break;
    case CpuOp::Book:aLoadADPCMImpl(int32_t(a[0]),static_cast<const int16_t*>(source));break;
    case CpuOp::Buffer:aSetBufferImpl(a[0],a[1],a[2],a[3]);break;
    case CpuOp::Interleave:aInterleaveImpl(a[0],a[1],a[2],a[3]);break;
    case CpuOp::Move:aDMEMMoveImpl(a[0],a[1],int32_t(a[2]));break;
    case CpuOp::Loop:aSetLoopImpl(reinterpret_cast<ADPCM_STATE*>(c.pointer));break;
    case CpuOp::Adpcm:aADPCMdecImpl(a[0],state);break;
    case CpuOp::Resample:aResampleImpl(a[0],a[1],state);break;
    case CpuOp::Env1:aEnvSetup1Impl(a[0],a[1],a[2],a[3]);break;
    case CpuOp::Env2:aEnvSetup2Impl(a[0],a[1]);break;
    case CpuOp::Envelope:aEnvMixerImpl(a[0],a[1],a[2]&1,a[2]&2,a[2]&4,a[2]&8,a[2]&16,int32_t(a[3]),a[4]);break;
    case CpuOp::Mix:aMixImpl(a[0],int16_t(a[1]),a[2],a[3]);break;
    case CpuOp::S8:aS8DecImpl(a[0],state);break;
    case CpuOp::Add:aAddMixerImpl(a[0],a[1],a[2]);break;
    case CpuOp::Duplicate:aDuplicateImpl(a[0],a[1],a[2]);break;
    case CpuOp::Zoh:aResampleZohImpl(a[0],a[1]);break;
    case CpuOp::Interl:aInterlImpl(a[0],a[1],a[2]);break;
    case CpuOp::Filter:aFilterImpl(a[0],a[1],a[0]>1?const_cast<int16_t*>(static_cast<const int16_t*>(source)):state);break;
    case CpuOp::HiLo:aHiLoGainImpl(a[0],a[1],a[2]);break;
    case CpuOp::Noop3:aUnkCmd3Impl(a[0],a[1],a[2]);break;
    case CpuOp::TableMultiply:aUnkCmd19Impl(a[0],a[1],a[2],a[3]);break;
    }
}
}
