#pragma once
#include <3ds.h>
#include <array>
#include <algorithm>
#include <cstring>
namespace ResidentDsp {
// Called only under the runtime owner/full-batch gate. Worker stop and wave
// release are separate so the owner can confirm the actual DSP unload first.
class NativeNdspPort {
public:
    bool start(unsigned rate){
        if(uncertain_)return false;
        if(worker_)return true;
        Result result=ndspInit();if(R_FAILED(result))return false;
        // Some SDK revisions return stale success after threadCreate failure.
        // Require a real NDSP worker frame before trusting its join lifecycle.
        uint64_t began=svcGetSystemTick();
        while(!ndspGetFrameCount() && uint64_t(svcGetSystemTick())-began<SYSCLOCK_ARM11/4)svcSleepThread(1000000);
        if(!ndspGetFrameCount()){uncertain_=true;return false;}
        worker_=true;
        for(auto& buffer:buffers_){
            buffer.samples=static_cast<int16_t*>(linearAlloc(Frames*4));
            if(!buffer.samples)return false;
            std::memset(buffer.samples,0,Frames*4);buffer.wave={};
        }
        ndspSetOutputMode(NDSP_OUTPUT_STEREO);ndspChnReset(0);ndspChnWaveBufClear(0);
        ndspChnSetInterp(0,NDSP_INTERP_LINEAR);ndspChnSetRate(0,float(rate));
        ndspChnSetFormat(0,NDSP_FORMAT_STEREO_PCM16);
        float mix[12]={1,1};ndspChnSetMix(0,mix);ndspChnSetPaused(0,false);return true;
    }
    bool stopWorker(){
        if(uncertain_)return false;
        if(worker_){ndspChnSetPaused(0,true);ndspChnWaveBufClear(0);ndspExit();worker_=false;}
        return true;
    }
    void release(){
        for(auto& buffer:buffers_){if(buffer.samples)linearFree(buffer.samples);buffer.samples=nullptr;buffer.wave={};}
    }
    int buffered(){
        if(!worker_)return 0;
        u16 sequence=ndspChnGetWaveBufSeq(0);u32 position=ndspChnGetSamplePos(0);unsigned frames=0;
        for(const auto& b:buffers_){
            if(b.wave.status==NDSP_WBUF_QUEUED)frames+=b.wave.nsamples;
            else if(b.wave.status==NDSP_WBUF_PLAYING)
                frames+=b.wave.sequence_id==sequence && position<b.wave.nsamples?b.wave.nsamples-position:b.wave.nsamples;
        }
        return int(frames);
    }
    void play(const int16_t* pcm,unsigned frames){
        if(!worker_ || !pcm)return;
        while(frames){
            auto b=std::find_if(buffers_.begin(),buffers_.end(),[](const Buffer& b){return b.wave.status==NDSP_WBUF_FREE || b.wave.status==NDSP_WBUF_DONE;});
            if(b==buffers_.end()){++dropped_;return;}
            unsigned n=std::min(frames,Frames);std::memcpy(b->samples,pcm,n*4);
            if(svcFlushProcessDataCache(CUR_PROCESS_HANDLE,reinterpret_cast<u32>(b->samples),n*4)!=0){++dropped_;return;}
            b->wave={};b->wave.data_vaddr=b->samples;b->wave.nsamples=n;
            ndspChnWaveBufAdd(0,&b->wave);pcm+=n*2;frames-=n;
        }
    }
    unsigned dropped()const{return dropped_;}
private:
    static constexpr unsigned Frames=2048;
    struct Buffer {ndspWaveBuf wave{};int16_t* samples=nullptr;};
    std::array<Buffer,8> buffers_{};bool worker_=false,uncertain_=false;unsigned dropped_=0;
};
}
