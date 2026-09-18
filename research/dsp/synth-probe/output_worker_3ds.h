#pragma once
#include <3ds.h>
#include <atomic>
#include "pcm_queue.h"
namespace ResidentDsp {
// Dedicated CSND owner. Control start/stop are serialized by the synth owner;
// this worker never acquires that lifecycle lock. Producer calls enqueue and
// buffered under their own short queue lock. All Stream APIs run on the worker.
// Caller must retain this whole object while stop() is false, even if the thread
// exited with uncertain DMA ownership. Never call start/stop from this worker.
template<class Stream> class OutputWorker3ds {
public:
    static constexpr unsigned QueueFrames=2048,PrimeFrames=1024,PushFrames=512;
    enum class State {Stopped,Starting,Priming,Playing,Stopping,Retained};
    enum class Error {None,Thread,Open,Start,Service,Publish,Deadline,Stop};
    OutputWorker3ds(){LightLock_Init(&queueLock_);LightEvent_Init(&wake_,RESET_ONESHOT);LightEvent_Init(&ready_,RESET_STICKY);}
    OutputWorker3ds(const OutputWorker3ds&)=delete;OutputWorker3ds& operator=(const OutputWorker3ds&)=delete;
    bool start(unsigned rate,int core=0){
        if(thread_ || state_.load()!=State::Stopped || rate<8000 || rate>48000)return false;
        {QueueLock lock(queueLock_);if(queue_.size())return false;ringFrames_=0;ringTick_=0;}
        rate_=rate;period_=uint64_t(CSND_TIMER(rate))*4;lastService_=maxGap_=0;maxGapUs_.store(0,std::memory_order_relaxed);
        stopRequested_.store(false);error_.store(Error::None);state_.store(State::Starting);
        LightEvent_Clear(&ready_);LightEvent_Clear(&wake_);
        thread_=threadCreate(entry,this,16*1024,0x18,core,false);
        if(!thread_){error_.store(Error::Thread);state_.store(State::Stopped);return false;}
        if(LightEvent_WaitTimeout(&ready_,250000000)!=0){stopRequested_.store(true);LightEvent_Signal(&wake_);return false;}
        return healthy();
    }
    bool enqueue(const int16_t* pcm,unsigned frames){
        QueueLock lock(queueLock_);
        if(!healthy() || !queue_.push(pcm,frames))return false;
        LightEvent_Signal(&wake_);return true;
    }
    unsigned buffered(){
        QueueLock lock(queueLock_);
        if(!healthy())return 0;
        uint64_t now=svcGetSystemTick(),elapsed=now>=ringTick_?(now-ringTick_)/period_:ringFrames_;
        unsigned ring=elapsed<ringFrames_?ringFrames_-unsigned(elapsed):0;
        return queue_.size()+ring;
    }
    bool healthy()const{
        State s=state_.load(std::memory_order_acquire);
        return !stopRequested_.load(std::memory_order_acquire) && (s==State::Priming || s==State::Playing);
    }
    bool stop(){
        stopRequested_.store(true,std::memory_order_release);LightEvent_Signal(&wake_);
        if(thread_){
            // Kernel timeout may be positive Info, so R_SUCCEEDED is insufficient.
            if(threadJoin(thread_,250000000)!=0)return false;
            threadFree(thread_);thread_=nullptr;
        }
        return state_.load(std::memory_order_acquire)==State::Stopped;
    }
    // Only after confirmed stop/join: recover unpublished queued PCM for a CPU
    // output fallback, or explicitly discard it for a sleep/seek reset. A failed
    // Stream call may have partially played this prefix; exact-once audible
    // recovery cannot be inferred from retained queue ownership.
    unsigned takePending(int16_t* pcm,unsigned limit){
        if(thread_ || state_.load()!=State::Stopped)return 0;
        QueueLock lock(queueLock_);unsigned n=queue_.peek(pcm,limit);queue_.consume(n);return n;
    }
    bool discardPending(){
        if(thread_ || state_.load()!=State::Stopped)return false;
        QueueLock lock(queueLock_);queue_.clear();return true;
    }
    State state()const{return state_.load(std::memory_order_acquire);}
    Error error()const{return error_.load(std::memory_order_acquire);}
    // Diagnostic passthrough for the CSND start failure point.
    unsigned startFail()const{return unsigned(stream_.startFail());}
    uint64_t startElapsedTicks()const{return stream_.startElapsedTicks();}
    unsigned activeMask()const{return stream_.activeMask();}
    bool activityKnown()const{return stream_.activityKnown();}
    unsigned startState()const{return stream_.startState();}
    // Worst observed service gap, in microseconds. The 4ms deadline below is a
    // scheduling proxy in front of the stream's own underrun check, and choosing
    // its threshold needs the gap the real device actually sees - measured here,
    // previously discarded. Published as a 32-bit atomic so the reader cannot tear.
    unsigned maxGapUs()const{return maxGapUs_.load(std::memory_order_relaxed);}
private:
    struct QueueLock {LightLock& lock;explicit QueueLock(LightLock& l):lock(l){LightLock_Lock(&lock);}~QueueLock(){LightLock_Unlock(&lock);}};
    static void entry(void* p){static_cast<OutputWorker3ds*>(p)->run();}
    void snapshot(){QueueLock lock(queueLock_);ringFrames_=unsigned(stream_.buffered());ringTick_=svcGetSystemTick();}
    bool service(){
        uint64_t before=svcGetSystemTick();
        // Fold the gap into the high-water mark BEFORE bailing out. This pre-call
        // check is the one scheduling latency between iterations actually trips, and
        // returning without recording left maxGapUs holding a value that is <=4000us
        // by construction - printing exactly the ambiguity it was added to resolve.
        if(lastService_ && before-lastService_>SYSCLOCK_ARM11/250){
            maxGap_=std::max(maxGap_,before-lastService_);
            maxGapUs_.store(unsigned(maxGap_*1000000ull/SYSCLOCK_ARM11),std::memory_order_relaxed);
            error_.store(Error::Deadline);return false;
        }
        bool ok=stream_.service();uint64_t after=svcGetSystemTick();
        if(lastService_){
            maxGap_=std::max(maxGap_,after-lastService_);
            maxGapUs_.store(unsigned(maxGap_*1000000ull/SYSCLOCK_ARM11),std::memory_order_relaxed);
        }
        lastService_=after;
        if(maxGap_>SYSCLOCK_ARM11/250){error_.store(Error::Deadline);return false;}
        if(!ok){error_.store(Error::Service);return false;}
        snapshot();return true;
    }
    void finish(){
        state_.store(State::Stopping,std::memory_order_release);
        bool stopped=stream_.stop();
        // Same Stream object/marker retries; never recycle uncertain commands.
        for(unsigned i=0;!stopped && i<3;++i){svcSleepThread(2000000);stopped=stream_.stop();}
        if(!stopped)error_.store(Error::Stop);
        state_.store(stopped?State::Stopped:State::Retained,std::memory_order_release);
        LightEvent_Signal(&ready_);
    }
    void run(){
        if(!stream_.open(rate_)){error_.store(Error::Open);finish();return;}
        state_.store(State::Priming,std::memory_order_release);LightEvent_Signal(&ready_);
        bool playing=false;
        while(!stopRequested_.load(std::memory_order_acquire)){
            if(playing && !service())break;
            unsigned n;
            {QueueLock lock(queueLock_);n=queue_.peek(staging_.data(),playing?PushFrames:PrimeFrames);}
            bool published=false;
            if(!playing && n==PrimeFrames){
                if(!stream_.start(staging_.data(),n)){error_.store(Error::Start);break;}
                playing=true;published=true;lastService_=svcGetSystemTick();
            }else if(playing && n){
                auto result=stream_.push(staging_.data(),n);
                if(result==Stream::Push::Fault){error_.store(Error::Publish);break;}
                published=result==Stream::Push::Accepted;
            }
            if(published){
                QueueLock lock(queueLock_);
                queue_.consume(n);ringFrames_=unsigned(stream_.buffered());ringTick_=svcGetSystemTick();
                state_.store(State::Playing,std::memory_order_release);
            }
            if(playing && !service())break;
            LightEvent_WaitTimeout(&wake_,2000000);
        }
        finish();
    }
    Stream stream_;
    PcmQueue<QueueFrames> queue_;
    std::array<int16_t,PrimeFrames*2> staging_{};
    LightLock queueLock_;LightEvent wake_,ready_;Thread thread_=nullptr;
    std::atomic<State> state_{State::Stopped};std::atomic<Error> error_{Error::None};
    std::atomic<bool> stopRequested_{false};
    unsigned rate_=0,ringFrames_=0;uint64_t period_=1,ringTick_=0,lastService_=0,maxGap_=0;
    std::atomic<unsigned> maxGapUs_{0};
};
}
