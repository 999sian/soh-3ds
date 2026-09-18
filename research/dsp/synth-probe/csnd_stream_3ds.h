#pragma once
#include <3ds.h>
#include <cstring>
#include "stream_timeline.h"
namespace ResidentDsp {
// Experimental single-worker CSND stereo ring, compatible with custom DSP
// firmware. No NDSP calls. Requires sole ownership of ALL libctru CSND API
// calls/channels, including callers outside this class. Owner serializes methods,
// including open/stop across instances, and drives service() at
// least every4ms, and stops this object before freeing it or enabling NDSP.
// Clock-derived ownership requires physical validation; this is not enabled in
// the game. A missed deadline faults and requires explicit confirmed stop.
class CsndStream {
public:
    static constexpr unsigned Capacity=8192,Guard=256;
    enum class Push {Accepted,Full,Fault};
    // Diagnostic only: distinguishes the five distinct start() failure points so
    // one run identifies the cause instead of the aggregate Error::Start code.
    enum class StartFail {None,Guard,Flush,Execute,Timeline,Inactive,Unhealthy};
    CsndStream()=default;CsndStream(const CsndStream&)=delete;CsndStream& operator=(const CsndStream&)=delete;
    // No destructor teardown: service timeouts must retain DMA-visible memory.
    bool open(unsigned rate){
        if(owner_ || initialized_ || pcm_ || rate<8000 || rate>48000)return false;
        timer_=CSND_TIMER(rate);
        if(!timer_ || timer_>65535)return false;
        result_=csndInit();if(R_FAILED(result_))return false;initialized_=true;owner_=this;activityKnown_=false;activeMask_=0;
        u32 mask=csndChannels;
        if(__builtin_popcount(mask)<2){stop();return false;}
        left_=__builtin_ctz(mask);mask&=mask-1;right_=__builtin_ctz(mask);
        pcm_=static_cast<int16_t*>(linearAlloc(Capacity*4));
        if(!pcm_){stop();return false;}
        std::memset(pcm_,0,Capacity*4);return true;
    }
    bool start(const int16_t* interleaved,unsigned frames){
        startFail_=StartFail::None;startElapsed_=0;
        // Diagnostic: StreamTimeline::start rejects unless the state is Idle or
        // Stopped, and with constant capacity/guard/timer/prefilled that guard
        // is the only way a StartFail::Timeline can occur. Recording the state
        // on entry distinguishes "left in Fault by an earlier failure" from
        // "left in Running because a previous session was never stopped", which
        // call for different fixes. Captured before any mutation.
        startState_=(unsigned)timeline_.state();
        if(!initialized_ || !pcm_ || running_ || pending_ || !interleaved || frames<=Guard*2 || frames>Capacity-Guard){
            startFail_=StartFail::Guard;return false;
        }
        copy(interleaved,0,frames);if(!flush(0,Capacity))return abortStart(StartFail::Flush);
        auto* marker=commandMarker();
        u32 flags=SOUND_FORMAT_16BIT|SOUND_REPEAT|SOUND_ENABLE|(timer_<<16);
        u32 leftPhys=osConvertVirtToPhys(pcm_),rightPhys=osConvertVirtToPhys(pcm_+Capacity);
        CSND_SetChnRegs(flags|SOUND_CHANNEL(left_),leftPhys,leftPhys,Capacity*2,CSND_VOL(1,-1),0);
        CSND_SetChnRegs(flags|SOUND_CHANNEL(right_),rightPhys,rightPhys,Capacity*2,CSND_VOL(1,1),0);
        uint64_t before=svcGetSystemTick();running_=true;
        if(!execute(marker))return abortStart(StartFail::Execute);
        uint64_t after=svcGetSystemTick();
        // Publish the start uncertainty on this path too: StreamTimeline::start's only
        // internal failure is its startup-uncertainty guard against (after-before), so
        // without this a StartFail::Timeline reports startUs=0 and says nothing about
        // the span that tripped it.
        if(!timeline_.start(Capacity,Guard,timer_,frames,before,after)){
            startElapsed_=after-before;
            return abortStart(StartFail::Timeline);
        }
        if(!active()){startElapsed_=svcGetSystemTick()-before;return abortStart(StartFail::Inactive);}
        lastInfo_=svcGetSystemTick();startElapsed_=lastInfo_-before;
        if(!timeline_.healthy(lastInfo_))return abortStart(StartFail::Unhealthy);
        return true;
    }
    Push push(const int16_t* interleaved,unsigned frames){
        if(!running_ || pending_ || !interleaved || !frames)return Push::Fault;
        StreamTimeline::Reservation r;
        // Copy/flush allowance sized for SCHEDULING jitter, not for the work: the
        // transfer itself measured ~208us on hardware, but this thread shares two
        // cores with a game thread whose ticks run 20-30ms, so a 1ms window was
        // blown by ordinary preemption and publish() then faulted permanently
        // (outputError=Publish). Widening it does not weaken the invariant -
        // reserve() proves safety across the whole window, so a longer budget is
        // STRICTER there (reserve's future-window check demands more headroom, and
        // faults on a low ring sooner - it is NOT the bounded-backpressure branch,
        // which uses the current window, so this cannot yield Push::Full sooner and
        // cannot livelock; it moves a terminal fault threshold from about 9ms of
        // buffered audio to about 12ms, against a 32ms prime),
        // and publish() still re-checks the pessimistic reader bound against the
        // written region and its guard. Matches the worker's own 4ms service
        // deadline; the guard is 8ms, so the budget stays inside it.
        if(!timeline_.reserve(frames,svcGetSystemTick(),SYSCLOCK_ARM11/250,r))
            return timeline_.state()==StreamTimeline::State::Running?Push::Full:Push::Fault;
        copy(interleaved,r.offset,r.first);
        if(r.second)copy(interleaved+r.first*2,0,r.second);
        if(!flush(r.offset,r.first) || (r.second && !flush(0,r.second))){fail();return Push::Fault;}
        if(!timeline_.publish(svcGetSystemTick()))return Push::Fault;
        return Push::Accepted;
    }
    bool service(){
        if(!running_ || pending_ || !timeline_.healthy(svcGetSystemTick()))return false;
        uint64_t now=svcGetSystemTick();
        if(now-lastInfo_>=SYSCLOCK_ARM11/10){
            if(!active())return fail();
            lastInfo_=svcGetSystemTick();
        }
        return timeline_.healthy(svcGetSystemTick());
    }
    uint64_t buffered()const{return timeline_.buffered(svcGetSystemTick());}
    bool stop(){
        if(!initialized_)return !pcm_;
        // Never reuse shared command entries while a previous timed-out list
        // may still own them. A subsequent call only polls that same marker.
        if(pending_ && !complete())return false;
        if(running_){
            auto* marker=commandMarker();
            CSND_SetPlayStateR(left_,0);CSND_SetPlayStateR(right_,0);
            stopQueued_=true;
            if(!execute(marker))return false;
            running_=false;stopQueued_=false;
        }else if(stopQueued_)return false;
        timeline_.stopped();
        // Explicit stop commands were acknowledged before releasing channels.
        csndExit();initialized_=false;owner_=nullptr;
        if(pcm_){linearFree(pcm_);pcm_=nullptr;}
        return true;
    }
    Result lastResult()const{return result_;}
    bool activityKnown()const{return activityKnown_;}
    unsigned activeMask()const{return activeMask_;}
    bool retained()const{return initialized_ || pcm_;}
    StartFail startFail()const{return startFail_;}
    uint64_t startElapsedTicks()const{return startElapsed_;}
    unsigned startState()const{return startState_;}
private:
    bool fail(){timeline_.cancel();return false;}
    // A start that failed handed the caller nothing to confirm stopped, yet the only
    // exit from StreamTimeline::Fault is stopped() and the only caller of that is
    // stop(), which exits CSND and frees the ring. Leaving the timeline faulted
    // therefore made every later start() fail its first guard for the rest of the
    // process - one transient flush error killed the output path. Release whatever
    // channels this attempt configured, confirm it, and hand the timeline back as
    // Stopped. While a command list is still outstanding its entries may still be
    // owned, so that case must stay faulted for the owner's stop() to drain.
    bool abortStart(StartFail reason){
        startFail_=reason;
        if(pending_)return fail();
        if(running_){
            auto* marker=commandMarker();
            CSND_SetPlayStateR(left_,0);CSND_SetPlayStateR(right_,0);
            stopQueued_=true;
            if(!execute(marker))return fail();
            running_=false;stopQueued_=false;
        }
        timeline_.stopped();
        return false;
    }
    void copy(const int16_t* src,unsigned offset,unsigned n){
        for(unsigned i=0;i<n;++i){pcm_[offset+i]=src[2*i];pcm_[Capacity+offset+i]=src[2*i+1];}
    }
    bool flush(unsigned offset,unsigned n){
        // Flush complete cache lines. Ring halves and wrap boundaries align32.
        unsigned begin=offset&~15u,end=(offset+n+15)&~15u;
        for(unsigned c=0;c<2;++c){
            result_=svcFlushProcessDataCache(CUR_PROCESS_HANDLE,(u32)(pcm_+c*Capacity+begin),(end-begin)*2);
            if(R_FAILED(result_))return false;
        }return true;
    }
    volatile u8* commandMarker(){return reinterpret_cast<volatile u8*>(csndAddCmd(0x300))-4;}
    bool complete(){
        uint64_t start=svcGetSystemTick();
        while(pending_ && !*pending_){
            __dmb();if(svcGetSystemTick()-start>=SYSCLOCK_ARM11/100)return fail();
            svcSleepThread(100000);
        }
        __dmb();pending_=nullptr;
        if(stopQueued_){running_=false;stopQueued_=false;}
        return true;
    }
    bool execute(volatile u8* marker){
        pending_=marker;result_=csndExecCmds(false);
        // An uncertain IPC result keeps ownership until the marker confirms it.
        if(R_FAILED(result_))return fail();
        return complete();
    }
    bool active(){
        if(pending_)return false;
        if(!execute(commandMarker()))return false;
        activeMask_=(csndGetChnInfo(left_)->active?1u:0u)|(csndGetChnInfo(right_)->active?2u:0u);
        activityKnown_=true;return activeMask_==3;
    }
    inline static CsndStream* owner_=nullptr;
    StreamTimeline timeline_;
    int16_t* pcm_=nullptr;
    volatile u8* pending_=nullptr;
    unsigned timer_=0,left_=0,right_=0;
    bool initialized_=false,running_=false,stopQueued_=false;
    bool activityKnown_=false;
    unsigned activeMask_=0;
    uint64_t lastInfo_=0;
    Result result_=0;
    StartFail startFail_=StartFail::None;
    uint64_t startElapsed_=0;
    unsigned startState_=0;
};
}
