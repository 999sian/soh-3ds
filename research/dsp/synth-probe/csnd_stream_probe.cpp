#include <cstdio>
#include <cstdlib>
#include <array>
#include <vector>
#include "csnd_stream_3ds.h"
using namespace ResidentDsp;
static void require(bool ok,const char* why){if(!ok){std::fprintf(stderr,"FAIL %s\n",why);std::exit(1);}}
u32 csndChannels=0xc;
static uint64_t tick=0;static bool stall=false,flushFail=false,allocFail=false,initFail=false;
static uint64_t flushDelay=0;
static unsigned allocations=0,frees=0,exits=0,stops=0,commands=0;
static int16_t* storage=nullptr;
static std::array<CSND_ChnInfo,32> info{};
alignas(32) static std::array<u8,32> command{};
static std::vector<unsigned> playing,stopping;
Result csndInit(){return initFail?-1:0;}void csndExit(){++exits;}
void* linearAlloc(unsigned n){if(allocFail)return nullptr;++allocations;storage=static_cast<int16_t*>(std::aligned_alloc(32,n));return storage;}
void linearFree(void* p){++frees;std::free(p);storage=nullptr;}
u32 osConvertVirtToPhys(void* p){return reinterpret_cast<u32>(p);}
uint64_t svcGetSystemTick(){return tick;}
void svcSleepThread(uint64_t ns){tick+=ns*SYSCLOCK_ARM11/1000000000;}
Result svcFlushProcessDataCache(u32,u32 address,unsigned n){require((address&31)==0 && n%32==0,"cache lines");tick+=flushDelay;return flushFail?-1:0;}
u32* csndAddCmd(unsigned){++commands;command.fill(0);return reinterpret_cast<u32*>(command.data()+8);}
Result csndExecCmds(bool wait){require(!wait,"no unbounded libctru waits");
    for(auto c:playing)info[c].active=1;
    for(auto c:stopping)info[c].active=0;
    playing.clear();stopping.clear();
    if(!stall)command[4]=1;
    return 0;
}
void CSND_SetChnRegs(u32 flags,u32,u32,u32 n,u32,u32){require(n==CsndStream::Capacity*2,"ring bytes");playing.push_back(flags&31);}
void CSND_SetPlayStateR(unsigned c,unsigned){++stops;stopping.push_back(c);}
CSND_ChnInfo* csndGetChnInfo(unsigned c){return &info[c];}
int main(){
    std::array<int16_t,8192> data{};for(unsigned i=0;i<data.size();++i)data[i]=int16_t(i*13);
    CsndStream s;require(s.open(32000),"open");require(s.start(data.data(),4096),"start");
    CsndStream other;require(!other.open(32000),"exclusive stream ownership");
    for(unsigned i=0;i<4096;++i)require(storage[i]==data[2*i] && storage[8192+i]==data[2*i+1],"stereo planar");
    require(s.push(data.data(),4096)==CsndStream::Push::Full,"backpressure");
    tick+=uint64_t(512)*CSND_TIMER(32000)*4;
    require(s.push(data.data(),512)==CsndStream::Push::Accepted && s.service(),"stream push");
    require(s.stop() && frees==1 && stops==2,"confirmed stop frees");
    require(s.open(32000) && s.start(data.data(),4096),"reopen");
    stall=true;require(!s.stop() && s.retained() && frees==1,"timed-out stop retains");
    unsigned issued=commands;require(!s.stop() && commands==issued,"same pending marker polled");
    command[4]=1;stall=false;require(s.stop() && frees==2 && stops==4,"late stop acknowledgment frees without duplicate stop");
    require(s.open(32000),"open flush fail");flushFail=true;require(!s.start(data.data(),4096),"flush failure");flushFail=false;
    require(s.stop() && frees==3,"pre-play failure frees after close");
    allocFail=true;require(!s.open(32000) && !s.retained(),"allocation failure closes service");allocFail=false;
    csndChannels=1;require(!s.open(32000) && !s.retained(),"insufficient stereo channels");csndChannels=0xc;
    initFail=true;require(!s.open(32000),"init failure");initFail=false;
    require(s.open(32000) && s.start(data.data(),4096),"start underrun");tick+=SYSCLOCK_ARM11;
    require(!s.service() && s.retained(),"underrun retains until stop");require(s.stop(),"underrun stop");
    require(s.open(32000) && s.start(data.data(),4096),"start late copy");
    tick+=uint64_t(512)*CSND_TIMER(32000)*4;
    // Ordinary preemption inside the write budget must publish. A 2ms stall used to
    // fault against the old 1ms budget even though the reader was nowhere near the
    // written region - that is what produced outputError=Publish on hardware.
    flushDelay=SYSCLOCK_ARM11/500;
    require(s.push(data.data(),512)==CsndStream::Push::Accepted,"a 2ms stall faulted inside the write budget");
    // Beyond the budget the reservation's proof no longer covers the write, so it
    // must still fault and retain ownership for the owner to stop.
    tick+=uint64_t(512)*CSND_TIMER(32000)*4;flushDelay=SYSCLOCK_ARM11/200;
    require(s.push(data.data(),512)==CsndStream::Push::Fault && s.retained(),"late publication faults and retains");
    flushDelay=0;require(s.stop(),"late publication stop");
    require(s.open(32000),"start timeout open");stall=true;
    unsigned outstanding=stops;
    require(!s.start(data.data(),4096) && s.retained(),"start timeout retains");
    // The attempt's command list timed out and may still own its entries, so the
    // abort path must not issue stop commands here - it has to stay faulted and let
    // the owner's stop() drain the same marker.
    require(stops==outstanding,"aborted start issued commands while a list was outstanding");
    issued=commands;require(!s.stop() && commands==issued,"start timeout drains same marker");
    command[4]=1;stall=false;require(s.stop(),"late start then explicit stop");
    // A start that fails before any channel was configured took no ownership the
    // owner could confirm, so a retry must be possible without tearing the stream
    // down. Every other failure case above stops first, which hid this: the only
    // exit from StreamTimeline::Fault is stopped(), and stop() exits CSND and frees
    // the ring, so a single transient flush error otherwise killed the DSP output
    // path for the rest of the process.
    require(s.open(32000),"retry open");
    unsigned released=stops;
    flushFail=true;require(!s.start(data.data(),4096),"retry flush failure");flushFail=false;
    require(stops==released,"aborted start released channels it never configured");
    require(s.start(data.data(),4096),"a failed start locked out every later start");
    tick+=uint64_t(512)*CSND_TIMER(32000)*4;
    require(s.push(data.data(),512)==CsndStream::Push::Accepted,"retried start does not stream");
    require(s.stop(),"retry stop");
    require(allocations==frees && exits==9,"no confirmed-stop allocation leaks");
    std::puts("PASS: native CSND adapter planar stereo, aligned cache publication, backpressure, lifecycle, late stop acknowledgment, allocation/flush/init/underrun failures");
}
