#pragma once
#include <cstdlib>
#include "chunk_frontend.h"
namespace ResidentDsp {
// Single producer callback owner. The lifecycle lock spans the entire synth
// chunk, including CPU barriers/replay. Control/sleep callers take that same
// lock. A separate output worker exclusively owns CSND and acknowledges stop.
// Lifecycle contract:
// lock/unlock; startDsp (load/map/attach/bootstrap); stopDsp (confirmed unload
// and event closure); startOutput/stopOutput (worker start/confirmed join+DMA
// stop); startNdsp/stopNdsp; now; wait(ticks) (sleep, no spin); cancelled and
// outputHealthy (thread-safe observations); fatal(reason) (must not return).
// Lifecycle callbacks must not re-enter this owner (including getters). The
// output worker must never acquire this lock: stopOutput joins it while holding
// the lock. Use a separate queue mutex/event for output-worker requests.
// Start failures may partially own resources, so stop is required even then.
// Construct with the existing CPU/NDSP backend active; install hooks separately
// on its producer while idle. This class never installs/replaces C hooks during
// a callback. CPU adapter provides read(image), restore, execute, canApply/apply.
template<class Transport,class CpuAdapter,class Lifecycle> class MixerOwner {
public:
    using Frontend=ChunkFrontend<Transport>;
    enum class Mode {Cpu,Custom,Silent,Suspended,Closed,Poisoned};
    struct Stats {unsigned dspChunks=0,cpuChunks=0,capturedCpuChunks=0;};
    MixerOwner(Session<Transport>& session,Frontend& frontend,CpuAdapter& cpu,Lifecycle& lifecycle,uint64_t timeout):
        session_(session),frontend_(frontend),cpu_(cpu),life_(lifecycle),timeout_(timeout){}
    MixerOwner(const MixerOwner&)=delete;MixerOwner& operator=(const MixerOwner&)=delete;
    // Control methods may run on another thread; all shared state is protected
    // by the lifecycle lock. No getter reads mode without that lock.
    bool enableCustom(){
        Guard lock(life_);if(active_ || mode_==Mode::Suspended || mode_==Mode::Closed || mode_==Mode::Poisoned)return false;
        if(mode_==Mode::Custom)return true;
        if(mode_==Mode::Cpu && !life_.stopNdsp())poison("NDSP stop unconfirmed");
        mode_=Mode::Silent;
        if(!timeout_ || !life_.startDsp() || session_.state()!=State::Ready || !life_.startOutput()){
            stopCustom();startCpu();return false;
        }
        mode_=Mode::Custom;return true;
    }
    bool disableCustom(){
        Guard lock(life_);if(active_ || mode_==Mode::Suspended || mode_==Mode::Closed || mode_==Mode::Poisoned)return false;
        if(mode_==Mode::Custom)stopCustom();
        return mode_==Mode::Cpu || startCpu();
    }
    bool suspend(){
        Guard lock(life_);if(active_ || mode_==Mode::Closed || mode_==Mode::Poisoned)return false;
        if(mode_==Mode::Suspended)return true;
        resumeCustom_=mode_==Mode::Custom;
        if(mode_==Mode::Custom)stopCustom();
        else if(mode_==Mode::Cpu && !life_.stopNdsp())poison("NDSP suspend unconfirmed");
        mode_=Mode::Suspended;return true;
    }
    bool resume(){
        Guard lock(life_);if(active_ || mode_!=Mode::Suspended)return false;
        mode_=Mode::Silent;
        if(resumeCustom_){
            if(life_.startDsp() && session_.state()==State::Ready && life_.startOutput()){mode_=Mode::Custom;return true;}
            stopCustom();
        }
        return startCpu();
    }
    // Stop/join the producer before uninstalling the C hooks. Unlike disabling
    // custom mode, final shutdown must not restart NDSP during teardown.
    bool shutdown(){
        Guard lock(life_);if(active_ || mode_==Mode::Poisoned)return false;
        if(mode_==Mode::Closed)return true;
        if(mode_==Mode::Custom)stopCustom();
        else if(mode_==Mode::Cpu && !life_.stopNdsp())poison("NDSP shutdown unconfirmed");
        mode_=Mode::Closed;return true;
    }
    // Called by the existing producer's C mixer hooks.
    void begin(){
        life_.lock();
        if(active_ || mode_==Mode::Suspended || mode_==Mode::Closed || mode_==Mode::Poisoned)poison("producer entered unavailable backend");
        active_=true;capturing_=false;
        if(mode_==Mode::Custom && (!life_.outputHealthy() || life_.cancelled())){stopCustom();startCpu();}
        if(mode_==Mode::Custom){
            if(!cpu_.read(baseline_) || !frontend_.begin(baseline_))poison("mixer capture begin failed");
            capturing_=true;
        }
    }
    int record(uint8_t op,const uint32_t args[5],uintptr_t pointer){
        if(!active_)poison("mixer command outside chunk");
        if(!capturing_)return 0; // existing production CPU body runs live
        CpuCall call;call.op=CpuOp(op);std::copy(args,args+5,call.args.begin());call.pointer=pointer;
        if(!frontend_.record(call,cpu_))poison("mixer command ordering lost");
        return 1;
    }
    void barrier(){
        if(!active_)poison("mixer barrier outside chunk");
        if(capturing_ && !frontend_.flushToCpu(cpu_))poison("mixer barrier replay failed");
    }
    void end(){
        if(!active_)poison("mixer end outside chunk");
        if(capturing_){
            if(!frontend_.seal(cpu_))poison("mixer seal failed");
            if(frontend_.phase()==Frontend::Phase::Sealed){
                if(!frontend_.start(timeout_) && frontend_.phase()!=Frontend::Phase::Fault)poison("mixer start rejected");
                while(frontend_.phase()==Frontend::Phase::Dsp){
                    if(life_.cancelled() || !life_.outputHealthy())session_.cancel();
                    frontend_.step(life_.now(),cpu_);
                    if(frontend_.waiting()){
                        uint64_t remaining=session_.remainingTicks(life_.now());
                        // Deadline may expire between step and this observation.
                        // Repoll to report Timeout; never issue a zero-time wait.
                        if(remaining && !life_.wait(remaining))session_.cancel();
                    }
                }
            }
            if(frontend_.phase()==Frontend::Phase::Fault){
                // Failed work may still write DSP scratch. CPU replay is illegal
                // until actual unload has completed, even on transfer failure.
                if(!life_.stopDsp())poison("DSP stop unconfirmed; replay forbidden");
                session_.resetAfterStop();
                if(!frontend_.recoverAfterStop(cpu_))poison("stopped mixer replay failed");
                if(!life_.stopOutput())poison("output stop unconfirmed; NDSP forbidden");
                mode_=Mode::Silent;startCpu();++recoveries_;
            }else if(frontend_.phase()!=Frontend::Phase::Committed && frontend_.phase()!=Frontend::Phase::Cpu){
                poison("mixer result not committed");
            }
        }
        // A custom backend can still replay a whole chunk on the CPU (for
        // example an unsupported operation or capture capacity exhaustion).
        // Count only committed DSP work as offloaded, never backend selection.
        if(capturing_ && frontend_.phase()==Frontend::Phase::Committed)++stats_.dspChunks;
        else {++stats_.cpuChunks;if(capturing_)++stats_.capturedCpuChunks;}
        capturing_=false;active_=false;life_.unlock();
    }
    Mode mode(){Guard lock(life_);return mode_;}
    unsigned recoveries(){Guard lock(life_);return recoveries_;}
    Stats stats(){Guard lock(life_);return stats_;}
private:
    struct Guard {Lifecycle& life;explicit Guard(Lifecycle& l):life(l){life.lock();}~Guard(){life.unlock();}};
    [[noreturn]] void poison(const char* reason){mode_=Mode::Poisoned;life_.fatal(reason);std::abort();}
    void stopCustom(){
        if(!life_.stopDsp())poison("DSP stop unconfirmed");
        session_.resetAfterStop();
        if(!life_.stopOutput())poison("output stop unconfirmed");
        mode_=Mode::Silent;
    }
    bool startCpu(){
        bool ready=life_.startNdsp();
        if(!ready && !life_.stopNdsp())poison("failed NDSP initialization cleanup unconfirmed");
        mode_=ready?Mode::Cpu:Mode::Silent;return ready;
    }
    Session<Transport>& session_;Frontend& frontend_;CpuAdapter& cpu_;Lifecycle& life_;
    uint64_t timeout_;DspMixerImage baseline_{};
    Mode mode_=Mode::Cpu;bool active_=false,capturing_=false,resumeCustom_=false;
    unsigned recoveries_=0;
    Stats stats_{};
};
}
