#include "mapped_native_context.h"
#include "csnd_stream_3ds.h"
#ifdef SOH3DS_DSP_STREAM_IO_TRACE
static bool traceIo=false;
void SohDspStreamIoTrace(const char* stage,unsigned word,unsigned count,int32_t result){
    if(!traceIo)return;
    fprintf(logFile,"io_checkpoint=%s word=%04x count=%u rc=%08lx tick=%llu\n",stage,word,count,(unsigned long)result,(unsigned long long)svcGetSystemTick());fflush(logFile);
}
#endif
struct StreamContext {
    Context synth;StreamProbeFixture fixture;CsndStream output;
    std::array<int16_t,8192> priming{};
    std::array<int16_t,1024> stereo{};
    uint64_t renderTicks=0,maxRenderTicks=0,minBuffered=UINT64_MAX;
    unsigned blocks=0;bool streamStarted=false;
    uint64_t lastService=0,maxServiceGap=0;
    bool serviceOutput(){
        uint64_t now=svcGetSystemTick();
        if(lastService)maxServiceGap=std::max(maxServiceGap,now-lastService);
        if(maxServiceGap>SYSCLOCK_ARM11/250)return check(false,"stream service gap exceeded4ms");
        bool serviced=output.service();
        uint64_t after=svcGetSystemTick();
        if(lastService)maxServiceGap=std::max(maxServiceGap,after-lastService);
        lastService=after;
        return check(maxServiceGap<=SYSCLOCK_ARM11/250,"stream service completion exceeded4ms") && serviced;
    }
    bool render(){
        auto& x=synth;auto& f=fixture;
#ifdef SOH3DS_DSP_STREAM_IO_TRACE
        traceIo=blocks==0;
#endif
#ifdef SOH3DS_DSP_STREAM_TRACE
        x.tracing=blocks==0;
#endif
        x.trace("render_begin");
        for(unsigned i=0;i<512;++i){unsigned phase=(blocks*256+i)%128;f.input[i]=int16_t((phase<64?int(phase):128-int(phase))*64-2048);}
        f.continuation=blocks>0;
        if(!SohDspMixerReadImage(&x.initial,sizeof(x.initial)))return false;
        auto r=f.resampler,h=f.filter;
        x.trace("cpu_oracle_begin");
        f.sequence();SohDspMixerReadImage(&x.expected,sizeof(x.expected));
        x.trace("cpu_oracle_end");
        auto expectedR=f.resampler,expectedH=f.filter;auto expectedOutput=f.output;
        f.resampler=r;f.filter=h;f.output.fill(0);
        SohDspMixerWriteImage(&x.initial,sizeof(x.initial));
        SohDspMixerHooks hooks={Context::begin,Context::end,Context::barrier,Context::record};
        if(!SohDspMixerInstallHooks(&hooks,&x))return false;
        x.trace("capture_begin");
        uint64_t began=svcGetSystemTick();SohDspMixerBeginChunk();f.sequence();
        x.trace("capture_end");SohDspMixerEndChunk();
        x.trace("synthesis_end");
        uint64_t elapsed=svcGetSystemTick()-began;renderTicks+=elapsed;maxRenderTicks=std::max(maxRenderTicks,elapsed);
        bool removed=SohDspMixerInstallHooks(nullptr,nullptr);
        SohDspMixerReadImage(&x.actual,sizeof(x.actual));
        if(!check(removed && x.ok && x.cpu.executed==0 && x.frontend.phase()==Frontend::Phase::Committed &&
            !std::memcmp(&x.actual,&x.expected,sizeof(x.actual)) && f.resampler==expectedR && f.filter==expectedH && f.output==expectedOutput,"stream synth PCM/history equality"))return false;
        for(unsigned i=0;i<512;++i)stereo[2*i]=stereo[2*i+1]=f.output[i];
#ifdef SOH3DS_DSP_STREAM_TRACE
        x.tracing=false;
#endif
#ifdef SOH3DS_DSP_STREAM_IO_TRACE
        traceIo=false;
#endif
        ++blocks;return true;
    }
    bool run(){
        if(!boot(synth))return false;
        for(unsigned i=0;i<8;++i){if(!render())return false;std::copy(stereo.begin(),stereo.end(),priming.begin()+i*1024);}
        unsigned peak=0;for(auto sample:priming)peak=std::max(peak,unsigned(std::abs(int(sample))));
        if(!check(peak>=64,"non-silent prefill"))return false;
        fprintf(logFile,"stream_prefill=PASS blocks=%u peak=%u\n",blocks,peak);fflush(logFile);
        bool opened=output.open(32000);
        bool started=opened && output.start(priming.data(),4096);
        streamStarted=started;
        if(!started){
            fprintf(logFile,"stream_start=FAIL rc=%08lx activity_known=%d active_mask=%u\n",(unsigned long)output.lastResult(),output.activityKnown(),output.activeMask());fflush(logFile);return false;
        }
        lastService=svcGetSystemTick();
        uint64_t deadline=lastService+SYSCLOCK_ARM11*4ULL;
        // Keep roughly128ms queued and service every2ms between refills. PCM is
        // genuinely synthesized by DSP, with a direct CPU oracle each block.
        // This probe deliberately includes oracle overhead; no game speed claim.
        while(svcGetSystemTick()<deadline && !cancelled.load()){
            if(!serviceOutput())return check(false,"stream service/underrun");
            uint64_t buffered=output.buffered();minBuffered=std::min(minBuffered,buffered);
            if(buffered<4096){
                if(!render())return false;
                if(output.push(stereo.data(),512)!=CsndStream::Push::Accepted)return check(false,"stream publication");
            }
            if(!serviceOutput())return check(false,"stream post-render service");
            svcSleepThread(2000000);
        }
        return !cancelled.load() && blocks>8 && serviceOutput();
    }
    bool stop(){
        bool audio=output.stop();bool dsp=synth.stop();
        if(streamStarted)fprintf(logFile,"stream_start=PASS activity_known=%d active_mask=%u\n",output.activityKnown(),output.activeMask());
        fprintf(logFile,"stream_blocks=%u min_buffered_known=%d min_buffered=%llu dsp_total_ticks=%llu dsp_max_block_ticks=%llu max_service_gap_ticks=%llu active_mask=%u\n",blocks,minBuffered!=UINT64_MAX,(unsigned long long)(minBuffered==UINT64_MAX?0:minBuffered),(unsigned long long)renderTicks,(unsigned long long)maxRenderTicks,(unsigned long long)maxServiceGap,output.activeMask());fflush(logFile);
        fprintf(logFile,"stream_stop=%s dsp_stop=%s retained=%d\n",audio?"PASS":"FAIL",dsp?"PASS":"FAIL",output.retained());fflush(logFile);
        return audio && dsp;
    }
};
int main(){
    gfxInitDefault();consoleInit(GFX_TOP,nullptr);
    bool sleep=aptIsSleepAllowed(),home=aptIsHomeAllowed();aptSetSleepAllowed(false);aptSetHomeAllowed(false);
    bool newer=false;APT_CheckNew3DS(&newer);osSetSpeedupEnable(newer);
    mkdir("sdmc:/3ds",0777);mkdir("sdmc:/3ds/soh",0777);
    logFile=fopen("sdmc:/3ds/soh/dsp-stream-probe.txt","w");bool ok=logFile!=nullptr;
#ifdef SOH3DS_DSP_MAILBOX_DIAGNOSTIC
    printf("DSP Mailbox v14: notification isolation\n");
#elif defined(SOH3DS_DSP_STREAM_IO_TRACE)
    printf("DSP stream IO v13: first-block checkpoints\n");
#elif defined(SOH3DS_DSP_STREAM_TRACE)
    printf("DSP stream trace v12: first-block checkpoints\n");
#else
    printf("DSP streaming v10: brief quiet tones\n");
#endif
    if(ok){
#ifdef SOH3DS_DSP_MAILBOX_DIAGNOSTIC
        fprintf(logFile,"probe=v14 model=%s\nMailbox completion, no DR0 notification. Not a performance benchmark.\n",newer?"new":"old");fflush(logFile);
#elif defined(SOH3DS_DSP_STREAM_IO_TRACE)
        fprintf(logFile,"probe=v13 model=%s\nFirst-block IO diagnostics before CSND playback. Not a performance benchmark.\n",newer?"new":"old");fflush(logFile);
#elif defined(SOH3DS_DSP_STREAM_TRACE)
        fprintf(logFile,"probe=v12 model=%s\nFirst-block diagnostics before CSND playback. Not a performance benchmark.\n",newer?"new":"old");fflush(logFile);
#else
        fprintf(logFile,"probe=v10 model=%s\nDSP resample/filter PCM with CPU oracle; CSND streaming. Not game performance or audibility verification.\n",newer?"new":"old");fflush(logFile);
#endif
        bool initialized=R_SUCCEEDED(dspInit());ok=initialized;
        if(initialized){
            dspHookCookie cookie;dspHook(&cookie,hook);
            for(unsigned i=0;i<2 && ok;++i){
                auto* x=new(std::nothrow) StreamContext;ok=x!=nullptr;
                if(x){fprintf(logFile,"stream_boot=%u\n",i);fflush(logFile);ok=x->run();bool stopped=x->stop();ok=ok && stopped;if(stopped)delete x;}
            }
            dspUnhook(&cookie);dspExit();
        }
        fprintf(logFile,"finished=%s\n",ok?"PASS":"FAIL");fclose(logFile);
    }
    aptSetHomeAllowed(home);aptSetSleepAllowed(sleep);
    printf("Streaming test %s. START to exit\n",ok?"PASS":"FAIL");
    while(aptMainLoop()){hidScanInput();if(hidKeysDown()&KEY_START)break;gfxFlushBuffers();gfxSwapBuffers();gspWaitForVBlank();}
    gfxExit();return ok?0:1;
}
