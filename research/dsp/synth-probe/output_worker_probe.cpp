#include <cstdio>
#include <cstdlib>
#include <thread>
#include <atomic>
#include <chrono>
#include <vector>
#include <deque>
#include "output_worker_3ds.h"
using namespace ResidentDsp;
static void require(bool ok,const char* what){if(!ok){std::fprintf(stderr,"FAIL %s\n",what);std::exit(1);}}
static std::atomic<uint64_t> ticks{0};static thread_local bool workerThread=false;
static std::atomic<bool> createFail{false},joinTimeout{false},readyTimeout{false};
// Scheduling latency BETWEEN iterations, as opposed to a slow service call.
static std::atomic<bool> slowWake{false};
static std::atomic<unsigned> threadAlloc{0},threadFreed{0};
struct ThreadTag {std::thread thread;std::mutex mutex;std::condition_variable cv;bool done=false;};
void LightLock_Init(LightLock*){}void LightLock_Lock(LightLock* l){l->mutex.lock();}void LightLock_Unlock(LightLock* l){l->mutex.unlock();}
void LightEvent_Init(LightEvent* e,ResetType r){e->reset=r;}
void LightEvent_Clear(LightEvent* e){std::lock_guard<std::mutex> lock(e->mutex);e->signalled=false;}
void LightEvent_Signal(LightEvent* e){std::lock_guard<std::mutex> lock(e->mutex);e->signalled=true;e->cv.notify_all();}
int LightEvent_WaitTimeout(LightEvent* e,s64 ns){
    if(ns==250000000 && readyTimeout)return 1;
    std::unique_lock<std::mutex> lock(e->mutex);
    bool ready=e->cv.wait_for(lock,std::chrono::nanoseconds(ns),[&](){return e->signalled;});
    if(ready && e->reset==RESET_ONESHOT)e->signalled=false;
    if(!ready && workerThread)ticks.fetch_add(uint64_t(ns)*SYSCLOCK_ARM11/1000000000);
    if(ns==2000000 && slowWake && workerThread)ticks.fetch_add(SYSCLOCK_ARM11/100);
    return ready?0:1;
}
Thread threadCreate(void(*entry)(void*),void* p,unsigned stack,int priority,int core,bool detached){
    require(stack==16384 && priority==0x18 && core==0 && !detached,"native thread configuration");
    if(createFail)return nullptr;
    auto* t=new ThreadTag;++threadAlloc;t->thread=std::thread([=](){workerThread=true;entry(p);{std::lock_guard<std::mutex> lock(t->mutex);t->done=true;}t->cv.notify_all();});return t;
}
Result threadJoin(Thread t,u64 ns){
    if(joinTimeout)return 0x3fe; // positive timeout, never success
    std::unique_lock<std::mutex> lock(t->mutex);
    return t->cv.wait_for(lock,std::chrono::nanoseconds(ns),[&](){return t->done;})?0:0x3fe;
}
void threadFree(Thread t){require(t->done,"free only after completed join");t->thread.join();delete t;++threadFreed;}
u64 svcGetSystemTick(){return ticks.load();}void svcSleepThread(u64 ns){ticks.fetch_add(ns*SYSCLOCK_ARM11/1000000000);std::this_thread::yield();}
static std::atomic<bool> failOpen{false},failStart{false},failService{false},failPush{false},fullPush{false},failStop{false},slowService{false},holdStop{false};
static std::mutex dataMutex;static std::condition_variable stopCv;
static std::vector<int16_t> played;
static std::atomic<unsigned> stopCalls{0},serviceCalls{0};
struct Stream {
    enum class Push {Accepted,Full,Fault};
    std::thread::id owner;unsigned ring=0;
    void check()const{require(workerThread && owner==std::this_thread::get_id(),"exclusive worker calls");}
    bool open(unsigned rate){require(workerThread && rate==32000,"worker opens output");owner=std::this_thread::get_id();return !failOpen;}
    bool start(const int16_t* pcm,unsigned n){check();require(n==1024,"priming count");if(failStart)return false;append(pcm,n);return true;}
    Push push(const int16_t* pcm,unsigned n){check();if(failPush)return Push::Fault;if(fullPush)return Push::Full;append(pcm,n);return Push::Accepted;}
    void append(const int16_t* pcm,unsigned n){std::lock_guard<std::mutex> lock(dataMutex);played.insert(played.end(),pcm,pcm+n*2);ring+=n;}
    bool service(){check();++serviceCalls;if(slowService)ticks.fetch_add(SYSCLOCK_ARM11/100);ring=ring>16?ring-16:0;return !failService;}
    uint64_t buffered(){check();return ring;}
    bool stop(){check();++stopCalls;std::unique_lock<std::mutex> lock(dataMutex);stopCv.wait(lock,[](){return !holdStop;});return !failStop;}
};
using Worker=OutputWorker3ds<Stream>;
template<class Predicate> static void waitFor(Predicate p,const char* why){
    auto end=std::chrono::steady_clock::now()+std::chrono::seconds(2);
    while(!p() && std::chrono::steady_clock::now()<end)std::this_thread::sleep_for(std::chrono::milliseconds(1));
    require(p(),why);
}
static void reset(){ticks=0;failOpen=false;failStart=false;failService=false;failPush=false;fullPush=false;failStop=false;slowService=false;holdStop=false;slowWake=false;played.clear();}
int main(){
    // Randomized ring wrap/backpressure against a separate scalar FIFO oracle.
    PcmQueue<37> queue;std::deque<int16_t> reference;std::array<int16_t,80> source{},copy{};uint32_t seed=5;
    for(unsigned i=0;i<10000;++i){seed=seed*1664525+1013904223;unsigned n=1+(seed%40);for(unsigned j=0;j<n*2;++j)source[j]=int16_t(i+j);
        bool room=n<=37-reference.size()/2;require(queue.push(source.data(),n)==room,"queue admission");if(room)reference.insert(reference.end(),source.begin(),source.begin()+n*2);
        unsigned peek=queue.peek(copy.data(),n);require(peek==std::min<size_t>(n,reference.size()/2),"peek size");
        for(unsigned j=0;j<peek*2;++j)require(copy[j]==reference[j],"FIFO samples");
        require(queue.size()==reference.size()/2,"peek retains samples");unsigned consume=peek/2;require(queue.consume(consume),"consume");for(unsigned j=0;j<consume*2;++j)reference.pop_front();
    }
    std::array<int16_t,4096> data{};for(unsigned i=0;i<data.size();++i)data[i]=int16_t(i*17);
    reset();Worker w;require(w.start(32000),"start worker");require(w.enqueue(data.data(),512) && w.buffered()==512,"partial priming remains queued");
    require(w.enqueue(data.data()+1024,512),"finish prime");waitFor([&](){return w.state()==Worker::State::Playing;},"playing");
    require(w.enqueue(data.data()+2048,512) && w.enqueue(data.data()+3072,512),"streaming enqueue");
    waitFor([&](){std::lock_guard<std::mutex> lock(dataMutex);return played.size()==data.size();},"all PCM published");
    require(w.stop() && !w.healthy(),"worker confirmed stop");require(played==std::vector<int16_t>(data.begin(),data.end()),"stereo FIFO order on real worker");
    require(w.start(32000),"reopen");require(w.enqueue(data.data(),512),"pending partial prime");require(w.stop(),"stop priming");
    require(!w.start(32000),"pending samples require explicit recovery/discard");
    require(w.takePending(copy.data(),20)==20,"recover pending prefix");for(unsigned j=0;j<40;++j)require(copy[j]==data[j],"pending PCM preserved");require(w.discardPending(),"discard rest");
    for(unsigned mode=0;mode<7;++mode){
        reset();Worker t;
        if(mode==0)failOpen=true;
        bool started=t.start(32000);require(started==(mode!=0),"open status");
        if(started){
            if(mode==1)failStart=true;
            require(t.enqueue(data.data(),1024),"failure prime");
            if(mode!=1){waitFor([&](){return t.state()==Worker::State::Playing;},"failure playing");
                if(mode==2)failService=true;
                if(mode==3){fullPush=true;require(t.enqueue(data.data()+2048,512),"full publication queue");std::this_thread::sleep_for(std::chrono::milliseconds(5));require(t.healthy(),"full means backpressure");failPush=true;}
                if(mode==4)slowService=true;
                if(mode==5)failStop=true;
                // Mode 6: the gap lands BEFORE the service call, which is the path
                // scheduling latency actually trips on hardware. It must still be the
                // gap that gets reported, or maxGapUs is useless exactly when it fires.
                if(mode==6)slowWake=true;
            }
        }
        if(mode!=0 && mode!=5)waitFor([&](){return !t.healthy();},"fault observed");
        // A deadline fault has to report the gap that caused it, or the hardware
        // run cannot tell whether the 4000us threshold is the right one. The mock
        // service burns 10ms, so the published figure must be at least the
        // threshold it tripped.
        if(mode==4 || mode==6)require(t.maxGapUs()>=4000,"deadline fault did not publish the gap that caused it");
        bool stopped=t.stop();require(stopped==(mode!=5),"confirmed versus retained stop");
        if(mode==5)require(t.state()==Worker::State::Retained && !t.discardPending() && !t.start(32000),"uncertain DMA cannot restart or discard");
        if(mode==1 || mode==3){unsigned pending=t.takePending(data.data(),2048);require(pending==(mode==1?1024u:512u),"unpublished fault PCM retained");}
    }
    reset();createFail=true;Worker unavailable;require(!unavailable.start(32000) && unavailable.stop(),"thread-create failure owns nothing");createFail=false;
    readyTimeout=true;Worker lateStart;require(!lateStart.start(32000),"ready timeout");readyTimeout=false;require(lateStart.stop(),"late-start worker joined");
    reset();Worker delayed;require(delayed.start(32000),"delayed stop setup");holdStop=true;joinTimeout=true;unsigned freed=threadFreed;
    require(!delayed.stop() && threadFreed==freed,"positive join timeout retains handle");
    holdStop=false;stopCv.notify_all();joinTimeout=false;require(delayed.stop() && threadFreed==freed+1,"same worker joined later");
    require(threadAlloc==threadFreed,"no unjoined thread handles");
    std::puts("PASS: PCM FIFO wrap/order, exclusive native-shaped output worker, priming/streaming, backpressure, pending recovery, startup/publication/service/deadline/stop faults and positive join-timeout retention");
}
