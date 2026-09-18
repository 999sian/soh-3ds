// Standalone resident synthesis-chain proof; production NDSP remains untouched.
#include <3ds.h>
#include <cstdio>
#include <cstdint>
#include <atomic>
#include <cstring>
#include <sys/stat.h>
#include "firmware_component.h"
#ifdef SOH_DSP_EVENT_PROBE
#include "resident_transport_3ds.h"
#endif
#ifdef SOH_DSP_EXTENDED_PROBE
#include "chain_fixture.h"
#ifdef SOH_DSP_MULTIBLOCK_PROBE
#ifdef SOH_DSP_EVENT_PROBE
static constexpr unsigned readySignature=0x4457,version=8,commandCount=5;
static const char* resultPath="sdmc:/3ds/soh/dsp-event-output-probe.txt";
#else
static constexpr unsigned readySignature=0x4456,version=7,commandCount=5;
static const char* resultPath="sdmc:/3ds/soh/dsp-chain-output-probe.txt";
#endif
#else
static constexpr unsigned readySignature=0x4455,version=6,commandCount=5;
static const char* resultPath="sdmc:/3ds/soh/dsp-chain-probe.txt";
#endif
#else
#include "pipeline_fixture.h"
static constexpr unsigned readySignature=0x4454,version=5,commandCount=4;
static const char* resultPath="sdmc:/3ds/soh/dsp-pipeline-probe.txt";
#endif
static constexpr unsigned frameCount=sizeof(pipelineFrames)/sizeof(pipelineFrames[0]);
static volatile uint16_t* memory;
static FILE* logFile;
static std::atomic<bool> cancelled{false};
static uint16_t sequence;
#ifdef SOH_DSP_EVENT_PROBE
static ResidentDsp::CtrTransport eventTransport;
static ResidentDsp::Session<ResidentDsp::CtrTransport> eventSession(eventTransport);
#endif
static void hook(DSP_HookType){cancelled.store(true,std::memory_order_relaxed);}
static bool check(Result result,const char* operation){
    if(R_SUCCEEDED(result))return true;
    fprintf(logFile,"ERROR %s %08lx\n",operation,(unsigned long)result);fflush(logFile);return false;
}
static bool flush(unsigned base,unsigned words){return check(svcFlushProcessDataCache(CUR_PROCESS_HANDLE,(u32)(memory+base),words*2),"flush");}
static bool invalidate(unsigned base,unsigned words){return check(svcInvalidateProcessDataCache(CUR_PROCESS_HANDLE,(u32)(memory+base),words*2),"invalidate");}
static bool wait(bool boot=false){
#ifdef SOH_DSP_EVENT_PROBE
    if(!boot){
        if(cancelled.load(std::memory_order_relaxed)){eventSession.cancel();return false;}
        auto state=eventSession.poll(svcGetSystemTick());
        if(state==ResidentDsp::State::Running){
            // Sticky completion event allows sleep followed by one poll/drain.
            // The session independently checks the half-second tick deadline.
            uint64_t remaining=eventSession.remainingTicks(svcGetSystemTick());
            s64 timeoutNs=(remaining*1000000000ULL+SYSCLOCK_ARM11-1)/SYSCLOCK_ARM11;
            if(eventTransport.waitForEvent(timeoutNs)==ResidentDsp::Receive::Error){
                check(eventTransport.lastResult(),"completion event wait");eventSession.cancel();return false;
            }
            state=eventSession.poll(svcGetSystemTick());
        }
        if(state!=ResidentDsp::State::Done){
            fprintf(logFile,"ERROR completion state=%u reason=%u status=%u stage=%s rc=%08lx\n",unsigned(state),unsigned(eventSession.error()),eventSession.dspStatus(),eventTransport.lastStage(),(unsigned long)eventTransport.lastResult());fflush(logFile);return false;
        }
        return eventSession.acknowledge();
    }
#endif
    u64 start=svcGetSystemTick();
    do{
        if(cancelled.load(std::memory_order_relaxed) || !invalidate(0,32))return false;
        if(boot ? memory[0x10]==readySignature : memory[0]==0 && memory[0x11]==sequence)return true;
    }while(svcGetSystemTick()-start<SYSCLOCK_ARM11/2);
    fprintf(logFile,"ERROR %s timeout\n",boot?"boot":"job");fflush(logFile);return false;
}
static bool submit(unsigned queueCount){
#ifdef SOH_DSP_EVENT_PROBE
    ResidentDsp::Command commands[32];
    if(queueCount>32)return false;
    if(queueCount){
        for(unsigned i=0;i<queueCount;++i)commands[i]={memory[0x100+i*4],memory[0x101+i*4],memory[0x102+i*4],memory[0x103+i*4]};
    }else commands[0]={memory[5],memory[1],memory[2],memory[6]};
    bool ok=eventSession.submit(commands,queueCount?queueCount:1,svcGetSystemTick(),SYSCLOCK_ARM11/2);
    if(!ok){fprintf(logFile,"ERROR async submit reason=%u rc=%08lx\n",unsigned(eventSession.error()),(unsigned long)eventTransport.lastResult());fflush(logFile);}
    sequence=eventSession.submittedSequence();return ok;
#else
    if(++sequence==0)++sequence;
    memory[0]=0;memory[3]=sequence;memory[7]=queueCount;
    if(!flush(0,16))return false;
    memory[0]=1;return flush(0,16);
#endif
}
static bool complete(unsigned status=0){return wait() && memory[0x12]==status;}
#ifdef SOH_DSP_MULTIBLOCK_PROBE
// Reuse the CSND output feasibility check against the typed gain command. This
// runs only after all chain checks, so its buffers can reuse resident scratch.
extern "C" void Soh3dsMixArm11(const int16_t*,int16_t*,int32_t,uint32_t);
static int failures;
alignas(32) static int16_t input[512],initial[512],actual[512],expected[512];
static int16_t reference(int16_t a,int16_t b,int16_t gain){
    int64_t v=gain==-32768?int32_t(b)-a:(int64_t(b)*32767+int64_t(a)*gain+16384)>>15;
    return v < -32768 ? -32768 : v > 32767 ? 32767 : int16_t(v);
}
static bool upload(unsigned count){
    for(unsigned i=0;i<count;++i){memory[0x1000+i]=uint16_t(input[i]);memory[0x1400+i]=uint16_t(initial[i]);}
    return flush(0x1000,count) && flush(0x1400,count);
}
static bool download(unsigned count){
    if(!invalidate(0x1400,count))return false;
    for(unsigned i=0;i<count;++i)actual[i]=int16_t(memory[0x1400+i]);
    return true;
}
static bool job(unsigned count,int16_t gain){
    memory[5]=0;memory[1]=count;memory[2]=uint16_t(gain);memory[6]=0;
    return submit(0) && complete();
}
#include "csnd_output_probe.h"
#endif
static bool initialize(){
    // Only touch shared RAM while no request is in flight; discard cache lines
    // first so stale data from a previous boot cannot overwrite firmware state.
    if(!invalidate(0x100,0x4000))return false;
    for(unsigned i=0x100;i<0x4100;++i)memory[i]=0;
    for(unsigned i=0;i<128;++i)memory[0x2000+i]=pipelineBook[i];
    for(unsigned i=0;i<256;++i)memory[0x4000+i]=pipelineTable[i];
    for(unsigned i=0;i<16;++i)memory[0x2800+i]=uint16_t(1u<<i);
    return flush(0x100,0x4000);
}
static volatile uint32_t seed=0x123ab;
__attribute__((noinline)) static uint32_t cpuWork(unsigned count){
    uint32_t x=seed;for(unsigned i=0;i<count;++i){x^=x<<13;x^=x>>17;x^=x<<5;}return x;
}
static bool runFrames(unsigned boot,unsigned mode){
    if(!initialize())return false;
    u64 total=0,submission=0,finishing=0,transfer=0;
    for(unsigned frame=0;frame<frameCount;++frame){
        if(!aptMainLoop() || cancelled.load(std::memory_order_relaxed))return false;
        const auto& expected=pipelineFrames[frame];
        unsigned flags=frame?0:1;
#ifdef SOH_DSP_EXTENDED_PROBE
        const uint16_t queue[]={2,16,0,uint16_t(flags),4,16,0,0,1,16,0x8000,uint16_t(flags),5,16,uint16_t(frame%32),0,6,16,0,uint16_t(flags)};
#else
        const uint16_t queue[]={2,16,0,uint16_t(flags),4,16,0,0,1,16,0x8000,uint16_t(flags),0,16,0x4000,0};
#endif
        u64 stage=svcGetSystemTick();
        for(unsigned i=0;i<5;++i)memory[0x800+i]=expected.packed[i];
        for(unsigned i=0;i<commandCount*4;++i)memory[0x100+i]=queue[i];
        if(!flush(0x800,16) || !flush(0x100,commandCount*4))return false;
#ifdef SOH_DSP_EXTENDED_PROBE
        for(unsigned i=0;i<3;++i){memory[0x3b00+i]=expected.vols[i];memory[0x3b03+i]=expected.rates[i];}
        for(unsigned i=0;i<8;++i)memory[0x3c10+i]=expected.coefficients[i];
        if(!flush(0x3b00,16) || !flush(0x3c10,16))return false;
#endif
        transfer+=svcGetSystemTick()-stage;
        u64 start=svcGetSystemTick();
        if(mode==0){
            for(unsigned command=0;command<commandCount;++command){
                memory[5]=queue[command*4];memory[1]=queue[command*4+1];
                memory[2]=queue[command*4+2];memory[6]=queue[command*4+3];
                if(!submit(0) || !complete())return false;
            }
        }else{
            if(!submit(commandCount))return false;
            u64 submitted=svcGetSystemTick();submission+=submitted-start;
            uint32_t work=mode==2?cpuWork(65536):0;
            u64 finishedWork=svcGetSystemTick();
            if(!complete())return false;
            finishing+=svcGetSystemTick()-finishedWork;
            // Verify independent CPU work without including its replay in timing.
            if(mode==2){total+=svcGetSystemTick()-start;if(work!=cpuWork(65536))return false;}
        }
        if(mode!=2)total+=svcGetSystemTick()-start;
        stage=svcGetSystemTick();
        if(!invalidate(0x1400,16) || !invalidate(0x3800,16) || !invalidate(0x3a00,16))return false;
#ifdef SOH_DSP_EXTENDED_PROBE
        if(!invalidate(0x1800,16) || !invalidate(0x1c00,16) || !invalidate(0x2c00,16) || !invalidate(0x3c00,32))return false;
        const uint16_t outputBases[]={0x1400,0x1800,0x1c00,0x2c00};
        for(unsigned channel=0;channel<4;++channel)for(unsigned i=0;i<16;++i)if(memory[outputBases[channel]+i]!=expected.pcm[channel][i]){
            fprintf(logFile,"ERROR PCM boot%u mode%u frame%u channel%u sample%u\n",boot,mode,frame,channel,i);fflush(logFile);return false;
        }
        for(unsigned i=0;i<16;++i)if(memory[0x3c00+i]!=expected.filter[i])return false;
        for(unsigned i=0;i<8;++i)if(memory[0x3c10+i]!=expected.filteredCoefficients[i])return false;
#endif
        for(unsigned i=0;i<16;++i){
            bool bad=memory[0x3800+i]!=expected.adpcm[i] || memory[0x3a00+i]!=expected.resample[i];
#ifndef SOH_DSP_EXTENDED_PROBE
            bad=bad || memory[0x1400+i]!=expected.pcm[i];
#endif
            if(bad){
                fprintf(logFile,"ERROR PCM/state boot%u mode%u frame%u sample%u\n",boot,mode,frame,i);fflush(logFile);return false;
            }
        }
        transfer+=svcGetSystemTick()-stage;
    }
    fprintf(logFile,"boot=%u mode=%u frames=%u result=PASS elapsed_ticks=%llu submit_ticks=%llu finish_ticks=%llu transfer_check_ticks=%llu\n",boot,mode,frameCount,(unsigned long long)total,(unsigned long long)submission,(unsigned long long)finishing,(unsigned long long)transfer);fflush(logFile);
    printf("Boot %u mode %u: %u frames PASS\n",boot,mode,frameCount);return true;
}
int main(){
    gfxInitDefault();consoleInit(GFX_TOP,nullptr);
    // APT invokes DSP sleep hooks from its event thread before unloading the
    // component. Keep this bounded standalone probe out of that transition;
    // production suspend/resume requires a serialized backend state machine.
    bool sleepAllowed=aptIsSleepAllowed(),homeAllowed=aptIsHomeAllowed();
    aptSetSleepAllowed(false);aptSetHomeAllowed(false);
    bool newer=false;APT_CheckNew3DS(&newer);osSetSpeedupEnable(newer);
    mkdir("sdmc:/3ds",0777);mkdir("sdmc:/3ds/soh",0777);
    logFile=fopen(resultPath,"w");
    printf("DSP pipeline probe v%u | %s 3DS\nSleep/HOME paused until test finishes.\n",version,newer?"New":"Old");
#ifdef SOH_DSP_MULTIBLOCK_PROBE
    printf("Includes a brief quiet test tone.\n");
#else
    printf("Silent correctness test.\n");
#endif
    bool ok=logFile!=nullptr;
    if(ok){
        fprintf(logFile,"probe=v%u model=%s\ncommands=%u\nmode0=separate mode1=queue mode2=queue+syntheticCPU\nTimings exclude staging/readback; no game FPS claim.\n",version,newer?"new":"old",commandCount);fflush(logFile);
#ifdef SOH_DSP_EVENT_PROBE
        fprintf(logFile,"completion=DR0-event with sleeping waits\n");fflush(logFile);
#endif
        bool initialized=check(dspInit(),"dspInit");ok=initialized;
        if(initialized){
            dspHookCookie cookie;dspHook(&cookie,hook);
            for(unsigned boot=0;boot<2 && ok;++boot){
                bool loaded=false;ok=check(DSP_LoadComponent(firmware_component,sizeof(firmware_component),0xff,0xff,&loaded),"load") && loaded;
                if(!ok)break;
                u32 base=0,last=0;
                ok=check(DSP_ConvertProcessAddressFromDspDram(0,&base),"map") && check(DSP_ConvertProcessAddressFromDspDram(0x4100,&last),"map end");
                ok=ok && base>=0x1ff40000 && base<=0x1ff77e00 && last==base+0x8200;
                if(ok){memory=reinterpret_cast<volatile uint16_t*>(base);ok=wait(true);}
#ifdef SOH_DSP_EVENT_PROBE
                if(ok){
                    ok=eventTransport.open(memory) && eventSession.attach(readySignature);
                    if(!ok){fprintf(logFile,"ERROR event attach reason=%u rc=%08lx\n",unsigned(eventSession.error()),(unsigned long)eventTransport.lastResult());fflush(logFile);}
                }
#endif
                for(unsigned mode=0;mode<3 && ok;++mode)ok=runFrames(boot,mode);
#ifdef SOH_DSP_MULTIBLOCK_PROBE
                if(ok && boot==1)ok=csndOutputProof(0);
#endif
                bool unloaded=check(DSP_UnloadComponent(),"unload");ok=ok && unloaded;
#ifdef SOH_DSP_EVENT_PROBE
                if(unloaded){eventSession.resetAfterStop();bool closed=eventTransport.closeAfterStop();ok=ok && closed;}
#endif
            }
            dspUnhook(&cookie);dspExit();
        }
        fprintf(logFile,"finished=%s cancelled=%d\n",ok?"PASS":"FAIL",cancelled.load(std::memory_order_relaxed));fclose(logFile);
    }
    aptSetHomeAllowed(homeAllowed);aptSetSleepAllowed(sleepAllowed);
    printf("\n%s. START to exit.\n",ok?"DONE":"FAILED");
    while(aptMainLoop()){hidScanInput();if(hidKeysDown()&KEY_START)break;gfxFlushBuffers();gfxSwapBuffers();gspWaitForVBlank();}
    gfxExit();return ok?0:1;
}
