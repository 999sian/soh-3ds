#pragma once
// Native v9 correctness probe. No streaming output or game speed claim.
#include <3ds.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <atomic>
#include <sys/stat.h>
#include "mapped_firmware_component.h"
#include "resident_transport_3ds.h"
#include "chunk_frontend.h"
#include "mapped_probe_cpu.h"
#include "mapped_component_3ds.h"
using namespace ResidentDsp;
static FILE* logFile;
static std::atomic<bool> cancelled{false};
static void hook(DSP_HookType){cancelled.store(true);}
static bool check(bool ok,const char* what){if(!ok){fprintf(logFile,"ERROR %s\n",what);fflush(logFile);}return ok;}
struct Cpu {
    unsigned executed=0;
    bool restore(const void* p,unsigned n){return SohDspMixerWriteImage(static_cast<const DspMixerImage*>(p),n);}
    void execute(const CpuCall& c,const void* p){int prior=SohDspMixerSetCpuBypass(1);++executed;mappedProbeExecute(c,p);SohDspMixerSetCpuBypass(prior);}
    bool canApply(const DspMixerImage&)const{return true;}
    void apply(const DspMixerImage& image){SohDspMixerWriteImage(&image,sizeof(image));}
};
using Frontend=ChunkFrontend<CtrTransport>;
struct Context : MappedProbeFixture {
    CtrTransport io;Session<CtrTransport> session{io};ChunkCapture capture;StateTransaction states;CpuReplay replay;
    Frontend frontend{io,session,capture,states,replay};Cpu cpu;
    bool ok=true,injectFault=false,recovered=false;unsigned waits=0;
#ifdef SOH3DS_DSP_STREAM_TRACE
    bool tracing=false;
    void trace(const char* stage){if(tracing){fprintf(logFile,"checkpoint=%s phase=%u transport=%s rc=%08lx tick=%llu\n",stage,unsigned(frontend.phase()),io.lastStage(),(unsigned long)io.lastResult(),(unsigned long long)svcGetSystemTick());fflush(logFile);}}
#else
    void trace(const char*){}
#endif
    DspMixerImage initial{},expected{},actual{};MappedComponent3ds component{io,session};
    bool stop(){
        bool stopped=component.stop();
        fprintf(logFile,"unload=%08lx stage=%s uncertain=%d\n",(unsigned long)component.lastResult(),component.lastStage(),component.uncertain());fflush(logFile);
        return stopped;
    }
    static void begin(void* p){auto& x=*static_cast<Context*>(p);x.ok=check(SohDspMixerReadImage(&x.actual,sizeof(x.actual)) && x.frontend.begin(x.actual),"begin") && x.ok;}
    static void barrier(void* p){auto& x=*static_cast<Context*>(p);x.ok=check(x.frontend.flushToCpu(x.cpu),"barrier") && x.ok;}
    static int record(void* p,uint8_t op,const uint32_t args[5],uintptr_t host){auto& x=*static_cast<Context*>(p);
        CpuCall c;c.op=CpuOp(op);std::copy(args,args+5,c.args.begin());c.pointer=host;
        // Unexpected contract failure is fatal for this probe; never execute a
        // live suffix over partially owned state and call that a fallback pass.
        if(!x.ok || !x.frontend.record(c,x.cpu)){x.ok=false;return 1;}return 1;
    }
    static void end(void* p){auto& x=*static_cast<Context*>(p);
        x.trace("seal_begin");
        x.ok=check(x.frontend.seal(x.cpu),"seal") && x.ok;
        x.trace("seal_end");
        if(x.ok && x.frontend.phase()==Frontend::Phase::Sealed){
            x.trace("submit_begin");
            uint64_t timeout=SYSCLOCK_ARM11/2;
#ifdef SOH3DS_DSP_STREAM_IO_TRACE
            // Flushed SD checkpoints take measurable time on hardware. This
            // prefill-only diagnostic must not create a 500ms deadline failure.
            if(x.tracing)timeout=SYSCLOCK_ARM11*5ULL;
#endif
            x.ok=check(x.frontend.start(timeout),"start");
            x.trace("submit_end");
            while(x.ok && x.frontend.phase()==Frontend::Phase::Dsp){
                if(cancelled.load()){x.session.cancel();}
                x.trace("step_begin");
                x.frontend.step(svcGetSystemTick(),x.cpu);
                x.trace("step_end");
                if(x.frontend.waiting()){
                    // Cancel only after the runner has submitted a real batch.
                    if(x.injectFault){x.session.cancel();continue;}
                    ++x.waits;auto ticks=x.session.remainingTicks(svcGetSystemTick());
                    s64 ns=(ticks*1000000000ULL+SYSCLOCK_ARM11-1)/SYSCLOCK_ARM11;
                    x.trace("wait_begin");
                    if(x.io.waitForEvent(ns)==Receive::Error)x.session.cancel();
                    x.trace("wait_end");
                }
            }
            if(!x.injectFault)x.ok=check(x.frontend.phase()==Frontend::Phase::Committed,"commit") && x.ok;
        }
        if(x.frontend.phase()==Frontend::Phase::Fault){
            fprintf(logFile,"fault=%u stage=%s rc=%08lx\n",unsigned(x.session.error()),x.io.lastStage(),(unsigned long)x.io.lastResult());
            if(x.stop())x.recovered=check(x.frontend.recoverAfterStop(x.cpu),"recovery after unload");
            x.ok=x.ok && x.injectFault && x.recovered;
        }
    }
    bool compare(unsigned boot,unsigned frame,bool fault=false){
        injectFault=fault;recovered=false;continueHistory=frame>0;
        auto initialD=decoder,initialR=resampler,initialS=s8,initialF=filter;output.fill(0);
        for(unsigned i=0;i<compressed.size();++i)compressed[i]=i*23+frame*7;
        compressed[0]=0x21;compressed[9]=0x32;
        initial.adpcm_loop_state=reinterpret_cast<uintptr_t>(loop.data());
        if(!SohDspMixerWriteImage(&initial,sizeof(initial)))return false;
        u64 start=svcGetSystemTick();sequence();u64 cpuTicks=svcGetSystemTick()-start;
        SohDspMixerReadImage(&expected,sizeof(expected));
        auto d=decoder,r=resampler,s=s8,f=filter,o=output;
        decoder=initialD;resampler=initialR;s8=initialS;filter=initialF;output.fill(0);
        SohDspMixerWriteImage(&initial,sizeof(initial));
        SohDspMixerHooks hooks={begin,end,barrier,record};
        if(!SohDspMixerInstallHooks(&hooks,this))return false;
        start=svcGetSystemTick();SohDspMixerBeginChunk();sequence();
        SohDspMixerReadImage(&actual,sizeof(actual));
        ok=check(!std::memcmp(&actual,&initial,sizeof(actual)) && output==std::array<int16_t,16>{},"deferred bodies") && ok;
        SohDspMixerEndChunk();u64 dspTicks=svcGetSystemTick()-start;
        ok=check(SohDspMixerInstallHooks(nullptr,nullptr),"uninstall") && ok;
        SohDspMixerReadImage(&actual,sizeof(actual));
        ok=check(!std::memcmp(&actual,&expected,sizeof(actual)) && decoder==d && resampler==r && s8==s && filter==f && output==o,"PCM and complete mixer/history equality") && ok;
        ok=check(fault ? (recovered && !component.loaded() && cpu.executed>0 && frontend.phase()==Frontend::Phase::Cpu) : (cpu.executed==0 && frontend.phase()==Frontend::Phase::Committed),"expected DSP commit or stopped CPU recovery") && ok;
        fprintf(logFile,"boot=%u frame=%u recovery=%d result=%s waits=%u cpu_ticks=%llu dsp_total_ticks=%llu context_bytes=%u\n",boot,frame,recovered,ok?"PASS":"FAIL",waits,(unsigned long long)cpuTicks,(unsigned long long)dspTicks,unsigned(sizeof(*this)));fflush(logFile);
        if(ok && !fault)initial=actual; // carry successful DMEM/globals into the next chunk
        return ok;
    }
};
static bool boot(Context& x){
    bool ready=x.component.start(mapped_firmware_component,sizeof(mapped_firmware_component),SohDspMixerResampleTable(),[](){return cancelled.load();});
    fprintf(logFile,"load=%08lx loaded=%d stage=%s uncertain=%d\n",(unsigned long)x.component.lastResult(),x.component.loaded(),x.component.lastStage(),x.component.uncertain());fflush(logFile);
    return check(ready,"mapped component start");
}
