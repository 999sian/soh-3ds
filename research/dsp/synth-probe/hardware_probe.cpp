// Standalone, silent arithmetic/transfer benchmark. Does not initialize NDSP.
#include <3ds.h>
#include <cstdio>
#include <cstdint>
#include <initializer_list>
#include <cstring>
#include <sys/stat.h>
#include "firmware_component.h"
extern "C" void Soh3dsMixArm11(const int16_t*,int16_t*,int32_t,uint32_t);
static volatile uint16_t* mem;
static uint16_t sequence;
static int failures;
static FILE* logFile;
static bool cancelled;
static dspHookCookie hookCookie;
static void dspHookCallback(DSP_HookType) { cancelled=true; }
static bool check(Result rc,const char* name) {
    if(R_SUCCEEDED(rc))return true;
    printf("%s: %08lx\n",name,(unsigned long)rc);
    if(logFile){fprintf(logFile,"# ERROR %s %08lx\n",name,(unsigned long)rc);fflush(logFile);}
    ++failures;return false;
}
static bool invalidate(unsigned start,unsigned words) {
    return check(svcInvalidateProcessDataCache(CUR_PROCESS_HANDLE,(u32)(mem+start),words*2),"invalidate");
}
static bool flush(unsigned start,unsigned words) {
    return check(svcFlushProcessDataCache(CUR_PROCESS_HANDLE,(u32)(mem+start),words*2),"flush");
}
static bool waitFor(bool boot) {
    u64 start=svcGetSystemTick();
    do {
        if(cancelled) {++failures;return false;}
        if(!invalidate(0,32))return false;
        if(boot ? mem[0x10]==0x4453 : mem[0]==0 && mem[0x11]==sequence)return true;
    } while(svcGetSystemTick()-start < SYSCLOCK_ARM11/2);
    printf("DSP %s timeout\n",boot?"ready":"job");
    if(logFile){fprintf(logFile,"# ERROR DSP %s timeout\n",boot?"ready":"job");fflush(logFile);}
    ++failures;return false;
}
static bool submit(unsigned n,int16_t gain,unsigned batch=1) {
    if(++sequence==0)++sequence;
    mem[0]=0;mem[1]=n;mem[2]=uint16_t(gain);mem[3]=sequence;mem[4]=batch;
    if(!flush(0,16))return false;
    mem[0]=1;
    return flush(0,16);
}
static bool job(unsigned n,int16_t gain,unsigned expectedStatus=0,unsigned batch=1) {
    if(!submit(n,gain,batch) || !waitFor(false))return false;
    if(mem[0x12]!=expectedStatus){++failures;printf("DSP status mismatch\n");return false;}
    return true;
}
static uint32_t rng=0x82a734;
static int16_t nextSample(){rng=rng*1664525u+1013904223u;return int16_t(rng>>16);}
static int16_t input[512] __attribute__((aligned(32)));
static int16_t initial[512] __attribute__((aligned(32)));
static int16_t actual[512] __attribute__((aligned(32)));
static int16_t expected[512] __attribute__((aligned(32)));
static int16_t reference(int16_t a,int16_t b,int16_t g) {
    int64_t v=g==-32768 ? int32_t(b)-a : (int64_t(b)*32767+int64_t(a)*g+16384)>>15;
    return v < -32768 ? -32768 : v > 32767 ? 32767 : int16_t(v);
}
static bool upload(unsigned n) {
    for(unsigned i=0;i<n;++i){mem[0x1000+i]=uint16_t(input[i]);mem[0x1400+i]=uint16_t(initial[i]);}
    return flush(0x1000,n) && flush(0x1400,n);
}
static bool download(unsigned n) {
    if(!invalidate(0x1400,n))return false;
    for(unsigned i=0;i<n;++i)actual[i]=int16_t(mem[0x1400+i]);
    return true;
}
static bool correctness() {
    // Verify both kernels, every output sample, input preservation, and guards.
    for(unsigned n:{0u,16u,32u,64u,128u,192u,256u,512u,513u,65535u}) {
        for(int gain:{-32768,-32767,-1,0,1,16384,32767}) {
            if(!aptMainLoop() || cancelled)return false;
            for(unsigned i=0;i<512;++i){input[i]=nextSample();initial[i]=nextSample();expected[i]=initial[i];}
            unsigned valid=n<=512?n:0;
            Soh3dsMixArm11(input,expected,gain,valid);
            for(unsigned i=0;i<valid;++i)if(expected[i]!=reference(input[i],initial[i],gain)){++failures;return false;}
            if(!invalidate(0x800,4096))return false;
            for(unsigned i=0x800;i<0x1800;++i)mem[i]=0x59a3;
            if(!flush(0x800,4096) || !upload(512) || !job(n,gain,n>512?1:0) || !invalidate(0x800,4096))return false;
            for(unsigned i=0x800;i<0x1800;++i) {
                uint16_t want=0x59a3;
                if(i>=0x1000 && i<0x1200)want=uint16_t(input[i-0x1000]);
                if(i>=0x1400 && i<0x1600)want=uint16_t(expected[i-0x1400]);
                if(mem[i]!=want){++failures;printf("Mismatch n=%u gain=%d addr=%x\n",n,gain,i);return false;}
            }
        }
    }
    return true;
}
static bool timing(bool newer,unsigned boot) {
    // A measured DSP job includes upload, cache maintenance, dispatch, wait and readback.
    // Resident rows omit sample transfers to expose how much batching might recover.
    for(unsigned n:{16u,128u,256u,512u})for(int gain:{-32768,16384})for(unsigned round=0;round<5;++round) {
        if(!aptMainLoop() || cancelled)return false;
        for(unsigned i=0;i<n;++i){input[i]=nextSample();initial[i]=nextSample();actual[i]=initial[i];}
        u64 cpu=0,transfer=0,resident=0;
        auto cpuTime=[&](){
            memcpy(actual,initial,n*sizeof(int16_t));
            u64 start=svcGetSystemTick();
            for(unsigned j=0;j<128;++j)Soh3dsMixArm11(input,actual,gain,n);
            cpu=svcGetSystemTick()-start;
        };
        auto dspTime=[&](){
            u64 start=svcGetSystemTick();
            for(unsigned j=0;j<128;++j)if(!upload(n) || !job(n,gain) || !download(n))return false;
            transfer=svcGetSystemTick()-start;
            start=svcGetSystemTick();
            for(unsigned j=0;j<128;++j)if(!job(n,gain))return false;
            resident=svcGetSystemTick()-start;
            return true;
        };
        if(round&1){if(!dspTime())return false;cpuTime();}
        else{cpuTime();if(!dspTime())return false;}
        if(logFile){fprintf(logFile,"%s,%u,%u,%d,%u,128,%llu,%llu,%llu\n",newer?"new":"old",boot,n,gain,round,
            (unsigned long long)cpu,(unsigned long long)transfer,(unsigned long long)resident);fflush(logFile);}
        if(round==0)printf("%u samples: ARM %.1f / DSP %.1f us\n",n,
            cpu*1e6/SYSCLOCK_ARM11/128,transfer*1e6/SYSCLOCK_ARM11/128);
    }
    return true;
}
// Synthetic independent CPU workload: measures actual overlap, not game FPS.
static volatile uint32_t workSeed=0x19ab321;
__attribute__((noinline)) static uint32_t cpuWork(unsigned count) {
    uint32_t v=workSeed; // volatile entry read prevents pure-call hoisting/elimination
    for(unsigned i=0;i<count;++i){v^=v<<13;v^=v>>17;v^=v<<5;}
    return v;
}
static bool overlap(bool newer,unsigned boot) {
    FILE* f=fopen("sdmc:/3ds/soh/dsp-synth-overlap.csv",boot?"a":"w");
    if(!f){++failures;return false;}
    if(!boot)fprintf(f,"# probe=v4 resident batches; staging/readback excluded; synthetic CPU work\nmodel,boot,samples,batch,work,round,serial_ticks,overlap_ticks,submit_ticks,work_ticks,finish_ticks\n");
    bool ok=true;
    for(unsigned n:{128u,512u})for(unsigned batch:{1u,2u,8u,32u})for(unsigned work:{256u,4096u,65536u})for(unsigned round=0;round<3;++round) {
        if(!aptMainLoop() || cancelled){ok=false;goto done;}
        for(unsigned i=0;i<n;++i){input[i]=nextSample();initial[i]=nextSample();}
        uint32_t serialHash=0,parallelHash=0;
        u64 serial=0,elapsed=0,submission=0,working=0,finish=0;
        auto runSerial=[&](){
            memcpy(expected,initial,n*2);
            u64 begin=svcGetSystemTick();
            for(unsigned i=0;i<batch;++i)Soh3dsMixArm11(input,expected,16384,n);
            serialHash=cpuWork(work);
            serial=svcGetSystemTick()-begin;
        };
        auto runParallel=[&](){
            if(!upload(n))return false;
            u64 begin=svcGetSystemTick();
            if(!submit(n,16384,batch))return false;
            u64 submitted=svcGetSystemTick();
            parallelHash=cpuWork(work); // no polling while the CPU has useful work
            u64 worked=svcGetSystemTick();
            if(!waitFor(false) || mem[0x12]!=0)return false;
            u64 finished=svcGetSystemTick();
            elapsed=finished-begin;submission=submitted-begin;working=worked-submitted;finish=finished-worked;
            return download(n);
        };
        if(round&1){ok=runParallel();runSerial();}else{runSerial();ok=runParallel();}
        if(!ok || serialHash!=parallelHash || memcmp(actual,expected,n*2)!=0){ok=false;goto done;}
        fprintf(f,"%s,%u,%u,%u,%u,%u,%llu,%llu,%llu,%llu,%llu\n",newer?"new":"old",boot,n,batch,work,round,
            (unsigned long long)serial,(unsigned long long)elapsed,(unsigned long long)submission,(unsigned long long)working,(unsigned long long)finish);
        fflush(f);
    }
 done:
    fprintf(f,"# boot=%u overlap=%s\n",boot,ok?"PASS":"FAIL");fclose(f);
    if(!ok)++failures;
    return ok;
}
#include "csnd_output_probe.h"
int main() {
    gfxInitDefault();consoleInit(GFX_TOP,nullptr);
    bool newer=false;APT_CheckNew3DS(&newer);osSetSpeedupEnable(newer);
    mkdir("sdmc:/3ds",0777);mkdir("sdmc:/3ds/soh",0777);
    logFile=fopen("sdmc:/3ds/soh/dsp-synth-probe.csv","w");
    printf("DSP synth transfer probe v4\n%s 3DS | includes a quiet test tone\n",newer?"New":"Old");
    printf("Keep lid open during this test.\n");
    if(logFile){fprintf(logFile,"# probe=v4 arithmetic+transfer only; not game FPS\nmodel,boot,samples,gain,round,iterations,arm_ticks,dsp_transfer_ticks,dsp_resident_ticks\n");fflush(logFile);}
    else {printf("Cannot open result file\n");++failures;}
    bool initialized=check(dspInit(),"dspInit");
    if(initialized) {
        dspHook(&hookCookie,dspHookCallback);
        for(unsigned boot=0;boot<2 && !failures && !cancelled;++boot) {
            bool loaded=false;
            if(!check(DSP_LoadComponent(firmware_component,sizeof(firmware_component),0xff,0xff,&loaded),"load"))break;
            if(!loaded){printf("DSP component rejected\n");++failures;break;}
            u32 base=0,inputAddress=0;
            if(!check(DSP_ConvertProcessAddressFromDspDram(0,&base),"map") ||
               !check(DSP_ConvertProcessAddressFromDspDram(0x1000,&inputAddress),"map input"))break;
            if(base<0x1ff40000 || base>0x1ff7d000 || inputAddress!=base+0x2000){printf("Unexpected DSP mapping\n");++failures;break;}
            mem=reinterpret_cast<volatile uint16_t*>(base);
            bool ok=waitFor(true) && correctness();
            printf("Boot %u correctness: %s\n",boot,ok?"PASS":"FAIL");
            if(logFile){fprintf(logFile,"# boot=%u correctness=%s\n",boot,ok?"PASS":"FAIL");fflush(logFile);}
            if(ok)ok=timing(newer,boot);
            if(ok)ok=overlap(newer,boot);
            if(ok)ok=csndOutputProof(boot);
            bool unloaded=check(DSP_UnloadComponent(),"unload");
            if(!ok || !unloaded){if(!failures)++failures;break;}
        }
        dspUnhook(&hookCookie);dspExit();
    }
    if(cancelled)++failures;
    if(logFile){fprintf(logFile,"# finished failures=%d cancelled=%d\n",failures,cancelled);fclose(logFile);}
    printf("\n%s. /3ds/soh/dsp-synth-probe.csv\nSTART to exit.\n",failures?"FAILED":"DONE");
    while(aptMainLoop()){hidScanInput();if(hidKeysDown()&KEY_START)break;gfxFlushBuffers();gfxSwapBuffers();gspWaitForVBlank();}
    gfxExit();return failures?1:0;
}
