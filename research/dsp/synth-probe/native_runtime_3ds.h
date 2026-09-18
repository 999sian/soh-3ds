#pragma once
#include <3ds.h>
#include <cstring>
#include "mapped_component_3ds.h"
#include "mixer_owner.h"
#include "output_worker_3ds.h"
#include "csnd_stream_3ds.h"
#include "sdk_lifecycle_owner.h"
#include "sdk_ownership.h"
#include "dsp_mixer_hooks.h"

void SohDspMixerExecuteCpu(const ResidentDsp::CpuCall&,const void*);
extern "C" void SohSdkDspCancel(void);
namespace ResidentDsp {
// NdspPort supplies start(rate), confirmed stopWorker(), release(), buffered(),
// play(pcm,frames). It owns no lifecycle locks, never acquires audio.mutex, and
// must not free waves until release(). This object, firmware and port live to
// process exit because both SDK and mixer callback tables retain their context.
template<class NdspPort> class NativeSynthRuntime {
    struct Mutex {
        LightLock value;
        Mutex(){LightLock_Init(&value);}
        void lock(){LightLock_Lock(&value);}void unlock(){LightLock_Unlock(&value);}
    };
    struct Cpu {
        unsigned replays=0;
        bool read(DspMixerImage& image){return SohDspMixerReadImage(&image,sizeof(image));}
        bool restore(const void* p,unsigned bytes){return SohDspMixerWriteImage(static_cast<const DspMixerImage*>(p),bytes);}
        void execute(const CpuCall& call,const void* source){
            int previous=SohDspMixerSetCpuBypass(1);++replays;
            SohDspMixerExecuteCpu(call,source);SohDspMixerSetCpuBypass(previous);
        }
        bool canApply(const DspMixerImage&)const{return true;}
        void apply(const DspMixerImage& image){SohDspMixerWriteImage(&image,sizeof(image));}
    };
    struct Life {
        NativeSynthRuntime& runtime;
        void lock(){runtime.ownerMutex_.lock();}void unlock(){runtime.ownerMutex_.unlock();}
        bool startDsp(){
            // A sleep/device reopen intentionally discards old unpublished
            // audio. Runtime fault fallback instead drains it in startNdsp.
            return runtime.output_.discardPending() && runtime.component_.start(runtime.firmware_,runtime.firmwareBytes_,SohDspMixerResampleTable(),[](){return false;});
        }
        bool stopDsp(){return runtime.component_.stop();}
        bool startOutput(){return runtime.output_.start(runtime.rate_);}
        bool stopOutput(){return runtime.output_.stop();}
        bool startNdsp(){
            if(!runtime.port_.start(runtime.rate_))return false;
            unsigned frames;
            while((frames=runtime.output_.takePending(runtime.pending_.data(),512)))runtime.port_.play(runtime.pending_.data(),frames);
            return true;
        }
        bool stopNdsp(){
            if(!runtime.port_.stopWorker())return false;
            if(dspIsComponentLoaded() && !runtime.ndspStop_.stop([](){return DSP_UnloadComponent()==0;}))return false;
            // Observes actual SDK IPC, including inlined unload on CPU sleep.
            // A cleared SDK flag alone cannot justify releasing wave memory.
            if(runtime.ndspStop_.uncertain() || SohDspObservedComponentState()!=SOH_DSP_COMPONENT_STOPPED)return false;
            runtime.port_.release();return true;
        }
        bool cancelled()const{return false;} // lifecycle waits outside full batch gate
        bool outputHealthy()const{return runtime.output_.healthy();}
        uint64_t now()const{return svcGetSystemTick();}
        bool wait(uint64_t ticks){
            s64 ns=(ticks*1000000000ULL+SYSCLOCK_ARM11-1)/SYSCLOCK_ARM11;
            return runtime.io_.waitForEvent(ns)!=Receive::Error;
        }
        [[noreturn]] void fatal(const char* reason){
            svcOutputDebugString(reason,std::strlen(reason));svcBreak(USERBREAK_PANIC);std::_Exit(1);
        }
    };
    using Owner=MixerOwner<CtrTransport,Cpu,Life>;
public:
    NativeSynthRuntime(NdspPort& port,const void* firmware,unsigned bytes):
        port_(port),firmware_(firmware),firmwareBytes_(bytes){}
    NativeSynthRuntime(const NativeSynthRuntime&)=delete;
    NativeSynthRuntime& operator=(const NativeSynthRuntime&)=delete;
    bool initialize(unsigned rate,unsigned desiredBuffered){
        if(rate<8000 || rate>48000 || !desiredBuffered)return false;
        if(prepared_){
            if(lifecycle_.phase()==SdkLifecycleOwner<Owner,Mutex>::Phase::Awake)
                return owner_.mode()==Owner::Mode::Cpu || owner_.mode()==Owner::Mode::Custom;
            return lifecycle_.reopen([&](){rate_=rate;desired_=desiredBuffered;});
        }
        // First initialization precedes the game producer and any DSP component.
        rate_=rate;desired_=desiredBuffered;
        return lifecycle_.activate([&](){
            if(!SohDspLifecycleInstall(lifecycle_.hooks()))return false;
            prepared_=true;
            if(dspInit()!=0 || SohDspObservedComponentState()!=SOH_DSP_COMPONENT_STOPPED)
                life_.fatal("DSP runtime service ownership unconfirmed");
        // Keep this DSP reference to process exit. ndspExit can then only drop
        // its own reference; our explicit unload confirms ordinary switching.
            const SohDspMixerHooks hooks={begin,end,barrier,record};
            if(!SohDspMixerInstallHooks(&hooks,this))life_.fatal("DSP runtime mixer hook installation failed");
            return true;
        });
    }
    // Called by AudioPlayer under the full producer gate.
    int buffered(){
        auto mode=owner_.mode();
        if(mode==Owner::Mode::Custom)return int(output_.buffered());
        if(mode==Owner::Mode::Cpu)return port_.buffered();
        return int(desired_); // prevent a silent backend from driving a refill spin
    }
    void play(const int16_t* pcm,unsigned frames){
        if(!pcm || !frames)return;
        auto mode=owner_.mode();
        if(mode==Owner::Mode::Custom){
            if(output_.enqueue(pcm,frames))return;
            // This producer already owns the batch gate. Control callbacks are
            // excluded; do not re-enter the coordinator's gate-taking method.
            owner_.disableCustom();mode=owner_.mode();
        }
        if(mode==Owner::Mode::Cpu)port_.play(pcm,frames);
    }
    void close(){if(prepared_ && !lifecycle_.park())lifecycle_.shutdown();}
    void shutdown(){if(prepared_)lifecycle_.shutdown();}
    auto stats(){return owner_.stats();}
    auto mode(){return owner_.mode();}
    unsigned recoveries(){return owner_.recoveries();}
    unsigned outputError()const{return unsigned(output_.error());}
    unsigned outputStartFail()const{return output_.startFail();}
    uint64_t outputStartElapsedTicks()const{return output_.startElapsedTicks();}
    unsigned outputActiveMask()const{return output_.activeMask();}
    unsigned outputActivityKnown()const{return output_.activityKnown()?1u:0u;}
    unsigned outputStartState()const{return output_.startState();}
    unsigned outputMaxGapUs()const{return output_.maxGapUs();}
private:
    static void begin(void* p){static_cast<NativeSynthRuntime*>(p)->owner_.begin();}
    static void end(void* p){static_cast<NativeSynthRuntime*>(p)->owner_.end();}
    static void barrier(void* p){static_cast<NativeSynthRuntime*>(p)->owner_.barrier();}
    static int record(void* p,uint8_t op,const uint32_t args[5],uintptr_t pointer){return static_cast<NativeSynthRuntime*>(p)->owner_.record(op,args,pointer);}
    NdspPort& port_;const void* firmware_;unsigned firmwareBytes_;
    unsigned rate_=32000,desired_=2480;bool prepared_=false;
    Mutex ownerMutex_,controlMutex_;
    CtrTransport io_;Session<CtrTransport> session_{io_};MappedComponent3ds component_{io_,session_};
    ChunkCapture capture_;StateTransaction states_;CpuReplay replay_;
    ChunkFrontend<CtrTransport> frontend_{io_,session_,capture_,states_,replay_};
    Cpu cpu_;OutputWorker3ds<CsndStream> output_;ComponentStopGuard ndspStop_;
    std::array<int16_t,1024> pending_{};
    Life life_{*this};Owner owner_{session_,frontend_,cpu_,life_,SYSCLOCK_ARM11/2};
    SdkLifecycleOwner<Owner,Mutex> lifecycle_{owner_,controlMutex_,SohSdkDspCancel};
};
}
