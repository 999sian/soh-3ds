#pragma once
#include <cstdlib>
#include "producer_gate.h"
#include "sdk_lifecycle_bridge.h"

namespace ResidentDsp {
// Serializes SDK callbacks and backend controls. The producer holds only the
// separate batch gate and then MixerOwner's lock, never this control mutex.
// Lifecycle callbacks/ports must not acquire the game's audio.mutex: producer
// order is audio.mutex -> batch gate -> MixerOwner lock.
// Install hooks before first component load. All control changes must use this
// adapter so an in-flight SDK sleep/wake pair cannot change backend underneath.
// Mutex supports lock/unlock (LightLock adapter natively, std::mutex in tests).
template<class Owner,class Mutex> class SdkLifecycleOwner {
public:
    enum class Phase {Awake,CpuSleeping,CustomSleeping,Parked,Closed};
    SdkLifecycleOwner(Owner& owner,Mutex& mutex,void (*sdkCancel)()):
        owner_(owner),mutex_(mutex),sdkCancel_(sdkCancel),hooks_{this,sleepHook,wakeHook,cancelHook}{}
    SdkLifecycleOwner(const SdkLifecycleOwner&)=delete;
    SdkLifecycleOwner& operator=(const SdkLifecycleOwner&)=delete;
    const SohDspLifecycleHooks* hooks()const{return &hooks_;}
    bool enableCustom(){return change(true);}
    bool disableCustom(){return change(false);}
    template<class Prepare> bool activate(Prepare prepare){
        Guard lock(mutex_);if(phase_!=Phase::Awake)return false;
        SohDspProducerPause(SOH_DSP_PAUSE_CONTROL);
        bool ready=prepare();
        bool ok=ready && (owner_.enableCustom() || owner_.mode()==Owner::Mode::Cpu);
        SohDspProducerResume(SOH_DSP_PAUSE_CONTROL);return ok;
    }
    // AudioPlayer closes/reopens its device when settings change. Keep the
    // producer paused between those calls without replacing installed hooks.
    bool park(){
        Guard lock(mutex_);
        if(phase_==Phase::Parked)return true;
        if(phase_!=Phase::Awake)return false;
        SohDspProducerPause(SOH_DSP_PAUSE_CONTROL);
        if(!owner_.suspend())std::abort();
        phase_=Phase::Parked;return true;
    }
    template<class Configure> bool reopen(Configure configure){
        Guard lock(mutex_);if(phase_!=Phase::Parked)return false;
        configure();bool ok=owner_.resume();
        phase_=Phase::Awake;SohDspProducerResume(SOH_DSP_PAUSE_CONTROL);return ok;
    }
    bool shutdown(){
        Guard lock(mutex_);
        if(phase_==Phase::Closed)return true;
        SohDspProducerPause(SOH_DSP_PAUSE_SHUTDOWN);
        // A sleeping NDSP worker waits on an event; SDK cancel releases that
        // wait before owner.shutdown asks ndspExit to join it.
        if(phase_==Phase::CpuSleeping){if(!sdkCancel_)std::abort();sdkCancel_();}
        if(!owner_.shutdown())std::abort();
        phase_=Phase::Closed;return true;
    }
    Phase phase(){Guard lock(mutex_);return phase_;}
private:
    struct Guard {Mutex& mutex;explicit Guard(Mutex& m):mutex(m){mutex.lock();}~Guard(){mutex.unlock();}};
    bool change(bool custom){
        Guard lock(mutex_);if(phase_!=Phase::Awake)return false;
        SohDspProducerPause(SOH_DSP_PAUSE_CONTROL);
        bool ok=custom?owner_.enableCustom():owner_.disableCustom();
        SohDspProducerResume(SOH_DSP_PAUSE_CONTROL);return ok;
    }
    static bool sleepHook(void* p,bool (*sdk)()){
        auto& self=*static_cast<SdkLifecycleOwner*>(p);Guard lock(self.mutex_);
        if(self.phase_==Phase::Closed || self.phase_==Phase::Parked)return false;
        if(self.phase_!=Phase::Awake)return true;
        SohDspProducerPause(SOH_DSP_PAUSE_LIFECYCLE);
        if(self.owner_.mode()==Owner::Mode::Custom){
            if(!self.owner_.suspend())std::abort();
            self.phase_=Phase::CustomSleeping;return true;
        }
        // CPU/NDSP must retain libctru's own sleep protocol and worker state.
        // Do not call owner.suspend here: that would stop NDSP before the SDK
        // continuation can preserve its component and sleeping worker.
        if(sdk()){self.phase_=Phase::CpuSleeping;return true;}
        SohDspProducerResume(SOH_DSP_PAUSE_LIFECYCLE);return false;
    }
    static void wakeHook(void* p,void (*sdk)()){
        auto& self=*static_cast<SdkLifecycleOwner*>(p);Guard lock(self.mutex_);
        if(self.phase_==Phase::CpuSleeping)sdk();
        else if(self.phase_==Phase::CustomSleeping){
            // A failed restart may safely select CPU or Silent; MixerOwner
            // handles confirmed cleanup. Gate stays closed throughout.
            self.owner_.resume();
        }else return;
        self.phase_=Phase::Awake;SohDspProducerResume(SOH_DSP_PAUSE_LIFECYCLE);
    }
    static void cancelHook(void* p,void (*sdk)()){
        auto& self=*static_cast<SdkLifecycleOwner*>(p);Guard lock(self.mutex_);
        if(self.phase_==Phase::Awake || self.phase_==Phase::Closed || self.phase_==Phase::Parked)return;
        if(self.phase_==Phase::CpuSleeping)sdk();
        SohDspProducerPause(SOH_DSP_PAUSE_SHUTDOWN);
        // SDK cancellation is terminal for this audio lifetime. Do not reload
        // either firmware or reopen the producer on a subsequent late wake.
        if(!self.owner_.shutdown())std::abort();
        self.phase_=Phase::Closed;
    }
    Owner& owner_;Mutex& mutex_;void (*sdkCancel_)();
    const SohDspLifecycleHooks hooks_;Phase phase_=Phase::Awake;
};
}
