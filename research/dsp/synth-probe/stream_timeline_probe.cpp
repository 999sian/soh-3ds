#include "stream_timeline.h"
#include <cstdio>
#include <cstdlib>
using namespace ResidentDsp;
static void require(bool ok,const char* why){if(!ok){std::fprintf(stderr,"FAIL %s\n",why);std::exit(1);}}
int main(){
    constexpr unsigned timer=2094,period=timer*4,capacity=8192,guard=256;
    StreamTimeline t;StreamTimeline::Reservation r;StreamTimeline::Window w;
    require(!t.start(capacity,guard,0,4096,100,200),"zero timer");
    require(t.start(capacity,guard,timer,4096,100,200),"start");
    require(!t.reserve(8192,200,period,r) && t.state()==StreamTimeline::State::Running,"full ring backpressure");
    unsigned wraps=0;
    for(unsigned i=0;i<10000;++i){
        uint64_t now=200+uint64_t(i+1)*512*period;
        require(t.healthy(now),"healthy stream");
        require(t.reserve(512,now,period*16,r),"reserve");
        require(t.window(now,w),"reader interval");
        // Check both extreme cursors for every byte published, including wraps.
        for(unsigned sample=0;sample<512;++sample){
            auto frame=t.written()+sample;
            require(frame>w.last+guard,"write ahead of active reader");
            require(frame<w.first+capacity-guard,"write before next slow-reader lap");
        }
        if(r.offset+r.first==capacity)++wraps;
        require(r.first+r.second==512,"split size");
        require(t.publish(now+period*8),"publish");
    }
    require(wraps>500,"repeated physical ring wrap");
    require(!t.healthy(UINT64_C(1000000000000)) && t.state()==StreamTimeline::State::Fault,"missed refill faults");
    require(!t.start(capacity,guard,timer,4096,0,0),"no restart without confirmed stop");
    t.stopped();require(t.start(capacity,guard,timer,4096,100,200),"restart after stop");
    require(t.reserve(512,512*period,period,r),"deadline reserve");
    require(!t.publish(514*period) && t.state()==StreamTimeline::State::Fault,"late flush faults");
    t.stopped();require(!t.start(capacity,guard,timer,4096,0,uint64_t(guard)*period),"startup uncertainty bounded");
    t.stopped();require(t.start(capacity,guard,timer,4096,100,200),"restart2");
    require(!t.healthy(199),"backward clock faults");
    t.stopped();require(t.start(capacity,guard,timer,4096,100,200),"restart3");
    require(!t.reserve(1,UINT64_MAX-3,8,r),"deadline overflow rejects");
    std::puts("PASS: conservative CSND cursor bounds, 10000 publications/wraps, backpressure, underrun/deadline/clock faults and confirmed-stop restart");
}
