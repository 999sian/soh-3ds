#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include "dsp_mixer_hooks.h"
namespace ResidentDsp {
// Original implementation-entry arguments, before DSP lowering/rounding. OPUS
// is deliberately absent: drain this prefix on CPU, then execute OPUS live.
enum class CpuOp:uint8_t {
#define SOH_DSP_CPP_OP(name) name=SOH_DSP_OP_##name,
    SOH_DSP_MIXER_OPERATIONS(SOH_DSP_CPP_OP)
#undef SOH_DSP_CPP_OP
};
struct CpuCall {
    CpuOp op=CpuOp::Noop3;
    std::array<uint32_t,5> args{};
    uintptr_t pointer=0;
};
// Fixed storage, one worker owns all ranges. Mutable histories/loop pointers
// must remain live and unchanged by the producer while recording or DSP-owned.
// Read-only command sources are snapshotted, including partial book updates.
class CpuReplay {
public:
    static constexpr unsigned MaxCalls=384,SourceBytes=12288,BaselineBytes=4096,ScratchBytes=3072;
    enum class Phase:uint8_t {Empty,Recording,Sealed,DspOwned,Stopped,Replaying,Consumed};
    enum class Error:uint8_t {None,Invalid,Capacity};
    CpuReplay()=default;
    CpuReplay(const CpuReplay&)=delete;CpuReplay& operator=(const CpuReplay&)=delete;
    CpuReplay(CpuReplay&&)=delete;CpuReplay& operator=(CpuReplay&&)=delete;
    bool begin(const void* baseline,unsigned bytes){
        if(phase_!=Phase::Empty && phase_!=Phase::Consumed)return false;
        if(!bytes || bytes>BaselineBytes || !range(reinterpret_cast<uintptr_t>(baseline),bytes))return false;
        std::memcpy(baseline_.data(),baseline,bytes);baselineSize_=bytes;size_=used_=0;error_=Error::None;phase_=Phase::Recording;return true;
    }
    // On refusal this call is NOT recorded. Replay the retained prefix, then
    // execute this call and the remaining producer calls through the normal CPU.
    bool append(const CpuCall& call){
        if(phase_!=Phase::Recording)return false;
        if(!validCall(call))return refuse(Error::Invalid);
        unsigned bytes=readBytes(call);
        if(bytes>ScratchBytes || !range(call.pointer,bytes))return refuse(Error::Invalid);
        unsigned writes=writeBytes(call);
        if(!range(call.pointer,writes) || (call.op==CpuOp::Loop && !range(call.pointer,32)))return refuse(Error::Invalid);
        if(size_==MaxCalls || bytes>SourceBytes-used_)return refuse(Error::Capacity);
        auto& entry=entries_[size_++];entry.call=call;entry.sourceOffset=used_;entry.sourceBytes=bytes;
        if(bytes)std::memcpy(sources_.data()+used_,reinterpret_cast<const void*>(call.pointer),bytes);
        used_+=bytes;return true;
    }
    bool seal(){if(phase_!=Phase::Recording)return false;phase_=Phase::Sealed;return true;}
    bool markDspOwned(){if(phase_!=Phase::Sealed)return false;phase_=Phase::DspOwned;return true;}
    // Lifecycle owner calls only after actual firmware stop/unload on failure.
    bool stopped(){if(phase_!=Phase::DspOwned)return false;phase_=Phase::Stopped;return true;}
    bool acceptDsp(){if(phase_!=Phase::DspOwned)return false;phase_=Phase::Consumed;return true;}
    template<class Cpu> bool replay(Cpu& cpu){
        if(phase_!=Phase::Recording && phase_!=Phase::Sealed && phase_!=Phase::Stopped)return false;
        // restore must validate size/state before mutating if it returns false.
        if(!cpu.restore(baseline_.data(),baselineSize_))return false;
        phase_=Phase::Replaying; // no retry after any CPU-visible write
        for(unsigned i=0;i<size_;++i){
            const auto& e=entries_[i];const void* source=reinterpret_cast<const void*>(e.call.pointer);
            if(e.sourceBytes){
                std::memcpy(scratch_.data(),sources_.data()+e.sourceOffset,e.sourceBytes);
                // Earlier CPU saves/history updates are already live. Overlay
                // only bytes they own, preserving the captured source elsewhere.
                for(unsigned j=0;j<i;++j){const auto& prior=entries_[j].call;unsigned n=writeBytes(prior);if(!n)continue;
                    uintptr_t first=std::max(e.call.pointer,prior.pointer),last=std::min(e.call.pointer+e.sourceBytes,prior.pointer+n);
                    if(first<last)std::memcpy(scratch_.data()+(first-e.call.pointer),reinterpret_cast<const void*>(first),last-first);
                }
                source=scratch_.data();
            }
            cpu.execute(e.call,source);
        }
        phase_=Phase::Consumed;return true;
    }
    Phase phase()const{return phase_;}Error error()const{return error_;}
    unsigned size()const{return size_;}unsigned sourceBytes()const{return used_;}
private:
    struct Entry {CpuCall call{};uint16_t sourceOffset=0,sourceBytes=0;};
    static bool range(uintptr_t p,unsigned bytes){return !bytes || (p && p<=UINTPTR_MAX-bytes);}
    static bool validCall(const CpuCall& c){
        if(unsigned(c.op)>unsigned(CpuOp::TableMultiply))return false;
        // Zero marks unused fields. Signed arguments retain their full32-bit
        // representation; Mix's signed16 gain is explicitly narrowed at dispatch.
        constexpr uint8_t widths[][5]={
            {16,32,0,0,0},{16,16,0,0,0},{16,16,0,0,0},{32,0,0,0,0},
            {8,16,16,16,0},{16,16,16,16,0},{16,16,32,0,0},{0,0,0,0,0},
            {8,0,0,0,0},{8,16,0,0,0},{8,16,16,16,0},{16,16,0,0,0},
            {16,16,5,32,32},{16,32,16,16,0},{8,0,0,0,0},{16,16,16,0,0},
            {16,16,16,0,0},{16,16,0,0,0},{16,16,16,0,0},{8,16,0,0,0},
            {8,16,16,0,0},{16,16,16,0,0},{8,16,16,16,0}
        };
        for(unsigned i=0;i<5;++i){unsigned bits=widths[unsigned(c.op)][i];if(bits && bits<32 && c.args[i]>((1u<<bits)-1))return false;}
        return true;
    }
    static unsigned readBytes(const CpuCall& c){
        if(c.op==CpuOp::Load)return c.args[1];
        if(c.op==CpuOp::Book)return c.args[0]<=256?c.args[0]:UINT32_MAX;
        if(c.op==CpuOp::Filter && c.args[0]>1)return 16;
        return 0;
    }
    static unsigned writeBytes(const CpuCall& c){
        if(c.op==CpuOp::Save)return c.args[1]&~15u;
        if(c.op==CpuOp::Adpcm || c.op==CpuOp::S8 || c.op==CpuOp::Resample || (c.op==CpuOp::Filter && c.args[0]<=1))return 32;
        return 0;
    }
    bool refuse(Error error){error_=error;return false;}
    std::array<Entry,MaxCalls> entries_{};
    std::array<uint8_t,SourceBytes> sources_{};
    // Read sources can be int16_t coefficients; preserve their alignment.
    alignas(16) std::array<uint8_t,ScratchBytes> scratch_{};
    std::array<uint8_t,BaselineBytes> baseline_{};
    unsigned baselineSize_=0,size_=0,used_=0;Phase phase_=Phase::Empty;Error error_=Error::None;
};
static_assert(sizeof(CpuReplay)<=35*1024,"CPU replay exceeded fixed storage budget");
}
