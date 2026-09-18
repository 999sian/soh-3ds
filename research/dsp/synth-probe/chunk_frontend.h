#pragma once
#include "mixer_image.h"
#include "cpu_replay.h"
#include "state_transaction.h"
#include "chunk_runner.h"
namespace ResidentDsp {
// Own this alongside fixed capture/state/replay storage on the audio worker,
// never on its small stack. Cpu adapter restores/applies the complete mixer image
// and calls non-intercepted CPU implementation bodies. No producer rerun occurs.
template<class Transport> class ChunkFrontend {
public:
    enum class Phase {Idle,Recording,Sealed,Dsp,Committed,Cpu,Fault};
    ChunkFrontend(Transport& io,Session<Transport>& session,ChunkCapture& capture,StateTransaction& states,CpuReplay& replay):
        io_(io),session_(session),capture_(capture),states_(states),replay_(replay),runner_(io,session,capture){}
    ChunkFrontend(const ChunkFrontend&)=delete;ChunkFrontend& operator=(const ChunkFrontend&)=delete;
    bool begin(const DspMixerImage& image){
        if(phase_!=Phase::Idle && phase_!=Phase::Committed && phase_!=Phase::Cpu)return false;
        if(phase_==Phase::Committed && (!capture_.nextChunk() || !states_.nextChunk()))return false;
        if(capture_.phase()!=ChunkCapture::Phase::Recording || states_.phase()!=StateTransaction::Phase::Recording || !replay_.begin(&image,sizeof(image)))return false;
        image_=image;initialFilterCount_=image.filter_count;std::memcpy(initialFilter_.data(),image.filter,16);
        phase_=Phase::Recording;return true;
    }
    template<class Cpu> bool record(const CpuCall& call,Cpu& cpu){
        if(phase_==Phase::Cpu){cpu.execute(call,reinterpret_cast<const void*>(call.pointer));return true;}
        if(phase_!=Phase::Recording)return false;
        if(!replay_.append(call)){
            if(!flushToCpu(cpu))return false;
            cpu.execute(call,reinterpret_cast<const void*>(call.pointer));return true;
        }
        if(!lower(call))return flushToCpu(cpu); // current call is already journaled
        return true;
    }
    // OPUS/unsupported implementation entry: flush first, then run it live.
    template<class Cpu> bool flushToCpu(Cpu& cpu){
        if(phase_==Phase::Cpu)return true;
        if(phase_!=Phase::Recording && phase_!=Phase::Sealed)return false;
        if(!replay_.replay(cpu)){phase_=Phase::Fault;return false;}
        if(!capture_.resetBeforeSubmit() || !states_.resetBeforeSubmit()){phase_=Phase::Fault;return false;}
        phase_=Phase::Cpu;return true;
    }
    template<class Cpu> bool seal(Cpu& cpu){
        if(phase_==Phase::Cpu)return true;
        if(phase_!=Phase::Recording)return false;
        if(!capture_.seal() || !states_.seal(capture_))return flushToCpu(cpu);
        if(!replay_.seal())return false;
        phase_=Phase::Sealed;return true;
    }
    bool start(uint64_t timeout){
        if(phase_!=Phase::Sealed || !timeout || session_.state()!=State::Ready)return false;
        // Own replay before the first state/sample shared write or cache call.
        if(!replay_.markDspOwned())return false;
        phase_=Phase::Dsp;
        if(!states_.stage(io_,session_))return fault();
        for(unsigned i=0;i<AudioDmemBytes/2;++i)io_.write(AudioDmemWordBase+i,uint16_t(image_.buf.as_s16[i]));
        for(unsigned i=0;i<8;++i)io_.write(FilterCoefficientWordBase+i,uint16_t(initialFilter_[i]));
        io_.write(FilterCountWord,initialFilterCount_/2);
        if(!io_.flush(AudioDmemWordBase,AudioDmemBytes/2) || !io_.flush(FilterCoefficientWordBase,9) || !runner_.start(timeout))return fault();
        return true;
    }
    template<class Cpu> Phase step(uint64_t now,Cpu& cpu){
        if(phase_!=Phase::Dsp)return phase_;
        auto result=runner_.step(now);
        if(result==ChunkRunner<Transport>::Phase::Fault){fault();return phase_;}
        if(result!=ChunkRunner<Transport>::Phase::Collected)return phase_;
        if(!states_.collect(io_,session_,capture_) || !io_.invalidate(AudioDmemWordBase,AudioDmemBytes/2) || !io_.invalidate(FilterCoefficientWordBase,9)){fault();return phase_;}
        unsigned count=io_.read(FilterCountWord);
        if(count>32767 || count%8){fault();return phase_;}
        for(unsigned i=0;i<AudioDmemBytes/2;++i)image_.buf.as_s16[i]=int16_t(io_.read(AudioDmemWordBase+i));
        for(unsigned i=0;i<8;++i)image_.filter[i]=int16_t(io_.read(FilterCoefficientWordBase+i));
        image_.filter_count=count*2;
        // canApply validates without mutation; apply must then be infallible.
        if(!cpu.canApply(image_) || replay_.phase()!=CpuReplay::Phase::DspOwned){fault();return phase_;}
        if(!states_.commit(capture_)){fault();return phase_;}
        cpu.apply(image_);replay_.acceptDsp();phase_=Phase::Committed;return phase_;
    }
    // Lifecycle owner must actually stop/unload firmware, then reset Session.
    // This gates late DSP writes before replay, not merely a timeout observation.
    template<class Cpu> bool recoverAfterStop(Cpu& cpu){
        if(phase_!=Phase::Fault || session_.state()!=State::Unbound)return false;
        if(replay_.phase()==CpuReplay::Phase::DspOwned && !replay_.stopped())return false;
        if(!replay_.replay(cpu))return false;
        capture_.resetAfterStop();states_.resetAfterStop();if(!runner_.resetAfterStop())return false;
        phase_=Phase::Cpu;return true;
    }
    Phase phase()const{return phase_;}
    bool waiting()const{return phase_==Phase::Dsp && runner_.phase()==ChunkRunner<Transport>::Phase::Waiting;}
private:
    bool fault(){session_.cancel();capture_.fail();states_.fail();phase_=Phase::Fault;return false;}
    // Metadata source reads cannot be deferred through DSP mutations. Unlike
    // ordinary loads they become host-side book/parameter records immediately.
    bool metadataSource(const void* p,unsigned bytes)const{
        auto first=reinterpret_cast<uintptr_t>(p);if(bytes && (!p || first>UINTPTR_MAX-bytes))return false;
        if(states_.overlapsMutable(p,bytes))return false;
        for(unsigned i=0;i<capture_.size();++i){const auto& e=*capture_.at(i);
            if(e.kind==ChunkCapture::Kind::Save && bytes && e.bytes && first<e.host+e.bytes && e.host<first+bytes)return false;
        }
        return true;
    }
    bool lower(const CpuCall& call){
        const auto& a=call.args;auto* host=reinterpret_cast<int16_t*>(call.pointer);
        Command c{};MappedParameters p{};unsigned state=0,loop=0,book=0;bool ok=false;
        switch(call.op){
        case CpuOp::Clear:case CpuOp::Move:{
            DmemTransfer transfer{};bool clear=call.op==CpuOp::Clear;
            if(!lowerDmemTransfer(clear,clear?0:a[0],clear?a[0]:a[1],int32_t(clear?a[1]:a[2]),transfer))return false;
            c={uint16_t(clear?8:7),transfer.bytes,transfer.source,transfer.destination};return capture_.append(c);
        }
        case CpuOp::Load:return capture_.load(host,a[0],a[1]);
        case CpuOp::Save:return capture_.save(a[0],host,a[1]);
        case CpuOp::Book:
            if(!metadataSource(host,a[0]))return false;
            if(a[0])std::memcpy(image_.adpcm_table,host,a[0]);
            return true;
        case CpuOp::Buffer:image_.in=a[1];image_.out=a[2];image_.nbytes=a[3];return true;
        case CpuOp::Loop:image_.adpcm_loop_state=call.pointer;return true;
        case CpuOp::Env1:image_.vol_wet=a[0]<<8;image_.rate_wet=a[1];image_.rate[0]=a[2];image_.rate[1]=a[3];return true;
        case CpuOp::Env2:image_.vol[0]=a[0];image_.vol[1]=a[1];return true;
        case CpuOp::Adpcm:case CpuOp::S8:{
            unsigned flags=a[0];bool twoBit=false;
            if(call.op==CpuOp::Adpcm){twoBit=flags&4;flags&=~4u;}
            if(flags>2 || !states_.bind(StateTransaction::Kind::Adpcm,host,state))return false;
            if(flags==2 && (!metadataSource(reinterpret_cast<const void*>(image_.adpcm_loop_state),32) || !states_.loop(reinterpret_cast<const int16_t*>(image_.adpcm_loop_state),loop)))return false;
            if(call.op==CpuOp::S8)ok=lowerMappedS8(0,image_.nbytes,image_.in,image_.out,flags,state,loop,false,c,p);
            else{if(!states_.book(&image_.adpcm_table[0][0][0],book))return false;
                ok=lowerMappedAdpcm(0,twoBit,image_.nbytes,image_.in,image_.out,flags,state,loop,book,c,p);}
            break;
        }
        case CpuOp::Resample:
            if(!states_.bind(StateTransaction::Kind::Resample,host,state))return false;
            ok=lowerMappedResample(0,image_.nbytes,image_.in,image_.out,a[1],a[0],state,c,p);break;
        case CpuOp::Filter:
            if(a[0]>1){
                if(!metadataSource(host,16))return false;
                std::array<int16_t,8> coefficients{};std::memcpy(coefficients.data(),host,16);
                ok=lowerMappedFilterSetup(0,a[1],coefficients,c,p);
                if(ok){image_.filter_count=uint16_t((a[1]+15)&~15u);std::memcpy(image_.filter,coefficients.data(),16);}
            }else{if(!states_.bind(StateTransaction::Kind::Filter,host,state))return false;ok=lowerMappedFilter(0,a[1],state,a[0],c,p);}break;
        case CpuOp::Interleave:ok=lowerMappedInterleave(0,a[3],a[1],a[2],a[0],c,p);break;
        case CpuOp::Envelope:{
            std::array<uint16_t,5> addresses={uint16_t(a[0]),uint16_t(((a[3]>>24)&255)<<4),uint16_t(((a[3]>>16)&255)<<4),uint16_t(((a[3]>>8)&255)<<4),uint16_t((a[3]&255)<<4)};
            ok=lowerMappedEnvelope(0,a[1],addresses,a[2],{image_.vol[0],image_.vol[1],image_.vol_wet},{image_.rate[0],image_.rate[1],image_.rate_wet},c,p);break;
        }
        case CpuOp::Mix:ok=lowerMappedGain(0,a[0],int16_t(a[1]),a[2],a[3],c,p);break;
        case CpuOp::Add:ok=lowerMappedAdd(0,a[0],a[1],a[2],c,p);break;
        case CpuOp::Duplicate:ok=lowerMappedDuplicate(0,a[0],a[1],a[2],c,p);break;
        case CpuOp::Zoh:ok=lowerMappedZoh(0,image_.nbytes,image_.in,image_.out,a[0],a[1],c,p);break;
        case CpuOp::Interl:ok=lowerMappedInterl(0,a[2],a[0],a[1],c,p);break;
        case CpuOp::HiLo:ok=lowerMappedHiLo(0,a[0],a[1],a[2],c,p);break;
        case CpuOp::Noop3:return true;
        case CpuOp::TableMultiply:ok=lowerMappedTableMultiply(0,a[0],a[1],a[3],a[2],c,p);break;
        }
        return ok && capture_.append(c,&p);
    }
    Transport& io_;Session<Transport>& session_;ChunkCapture& capture_;StateTransaction& states_;CpuReplay& replay_;
    ChunkRunner<Transport> runner_;DspMixerImage image_{};std::array<int16_t,8> initialFilter_{};uint16_t initialFilterCount_=0;Phase phase_=Phase::Idle;
};
}
