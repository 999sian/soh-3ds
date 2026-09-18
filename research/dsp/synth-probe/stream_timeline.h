#pragma once
#include <cstdint>
#include <limits>
namespace ResidentDsp {
// CSND has no public sample cursor. This experimental ring tracks an interval,
// not a point estimate: channel start happened between the command submission
// and its completion observation. svcGetSystemTick uses the base clock on both
// models; CSND's sample period is 4*CSND_TIMER(rate) of those ticks.
// Native owner must stop on Fault, including after a late publication. A CPU
// scheduling stall can violate a reserved write deadline; no software timeline
// proves uninterrupted audio during an unbounded stall. Do not enable in-game
// until physical clock/start uncertainty and underrun behavior are validated.
class StreamTimeline {
public:
    enum class State {Idle,Running,Reserved,Fault,Stopped};
    struct Window {uint64_t first=0,last=0;};
    struct Reservation {unsigned offset=0,first=0,second=0;uint64_t deadline=0;};
    bool start(unsigned capacity,unsigned guard,unsigned timer,unsigned prefilled,uint64_t before,uint64_t after){
        if(state_!=State::Idle && state_!=State::Stopped)return false;
        if(!capacity || !guard || guard>=capacity/2 || !timer || timer>65535 || prefilled>capacity || before>after)return false;
        period_=uint64_t(timer)*4;capacity_=capacity;guard_=guard;before_=before;after_=after;written_=prefilled;
        // More than one guard of startup uncertainty cannot sustain this ring.
        if((after-before)/period_>=guard || prefilled<=guard+(after-before)/period_+1)return fault();
        lastTick_=after;state_=State::Running;return true;
    }
    bool window(uint64_t now,Window& w)const{
        if((state_!=State::Running && state_!=State::Reserved) || now<after_ || now<lastTick_)return false;
        w.first=(now-after_)/period_;
        // Ceiling and a one-frame phase allowance cover a sample fetched at
        // launch. All comparisons use absolute frames, never modulo cursors.
        uint64_t delta=now-before_;
        w.last=delta/period_+(delta%period_!=0)+1;
        return true;
    }
    bool healthy(uint64_t now){
        Window w;if(!window(now,w) || w.last>=written_ || written_-w.last<=guard_)return fault();
        lastTick_=now;return true;
    }
    bool reserve(unsigned frames,uint64_t now,uint64_t writeBudget,Reservation& r){
        if(state_!=State::Running)return false;
        if(!frames || frames>capacity_ || !writeBudget || now>UINT64_MAX-writeBudget)return false;
        Window current,future;
        if(!window(now,current) || !window(now+writeBudget,future))return fault();
        // The first written frame must remain ahead of the fastest possible
        // reader through the copy/flush deadline; the last written frame must
        // be behind the slowest possible reader on its next lap, with guards.
        if(future.last>=written_ || written_-future.last<=guard_)return fault();
        if(written_>UINT64_MAX-frames || current.first>UINT64_MAX-(capacity_-guard_))return fault();
        if(written_+frames>current.first+capacity_-guard_)return false; // bounded backpressure
        r.offset=unsigned(written_%capacity_);r.first=frames<capacity_-r.offset?frames:capacity_-r.offset;
        r.second=frames-r.first;r.deadline=now+writeBudget;
        reserved_=frames;deadline_=r.deadline;lastTick_=now;state_=State::Reserved;return true;
    }
    bool publish(uint64_t now){
        if(state_!=State::Reserved)return false;
        Window w;
        if(now>deadline_ || !window(now,w) || w.last>=written_ || written_-w.last<=guard_)return fault();
        written_+=reserved_;reserved_=0;lastTick_=now;state_=State::Running;return true;
    }
    uint64_t buffered(uint64_t now)const{Window w;return window(now,w) && written_>w.last?written_-w.last:0;}
    void cancel(){fault();}
    // Only the native owner confirming stopped channels can reset ownership.
    void stopped(){state_=State::Stopped;reserved_=0;}
    State state()const{return state_;}
    uint64_t written()const{return written_;}
private:
    bool fault(){state_=State::Fault;return false;}
    State state_=State::Idle;
    unsigned capacity_=0,guard_=0,reserved_=0;
    uint64_t period_=0,before_=0,after_=0,lastTick_=0,written_=0,deadline_=0;
};
}
