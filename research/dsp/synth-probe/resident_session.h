#pragma once
#include <cstddef>
#include <cstdint>

namespace ResidentDsp {
struct Command {uint16_t type,count,argument,flags;};
enum class Receive {Pending,Received,Error};
enum class State {Unbound,Ready,Running,Done,Fault};
enum class Failure {None,Transfer,Protocol,Timeout,Rejected};
// Single-owner host-side state machine. Transport owns mapped RAM and the
// completion event. It must consume DR0 before returning Received. No polling
// loop, allocation, sample readback or automatic CPU replay occurs here.
// Caller retains payload/output/state ownership through Done, or until the DSP
// is stopped after Fault. A failed batch may already have modified audio state.
template<class Transport> class Session {
public:
    explicit Session(Transport& transport):io(transport){}
    Session(const Session&)=delete;
    Session& operator=(const Session&)=delete;
    Session(Session&&)=delete;
    Session& operator=(Session&&)=delete;
    bool attach(uint16_t signature){
        if(current!=State::Unbound)return false;
        if(!io.invalidate(0,32))return fail(Failure::Transfer);
        if(io.read(0x10)!=signature || io.read(0)!=0)return fail(Failure::Protocol);
        current=State::Ready;failure=Failure::None;status=0;return true;
    }
    bool submit(const Command* commands,size_t count,uint64_t now,uint64_t timeout){
        if(current!=State::Ready || !commands || count==0 || count>32 || timeout==0)return false;
        // Even a failed publication can be partially visible. Enter Running
        // before touching shared data; any transport failure requires stop.
        current=State::Running;started=now;limit=timeout;
        for(size_t i=0;i<count;++i){
            io.write(0x100+i*4,commands[i].type);io.write(0x101+i*4,commands[i].count);
            io.write(0x102+i*4,commands[i].argument);io.write(0x103+i*4,commands[i].flags);
        }
        if(!io.flush(0x100,count*4))return fail(Failure::Transfer);
        if(++sequence==0)++sequence;
        io.write(0,0);io.write(3,sequence);io.write(7,uint16_t(count));
        if(!io.flush(0,16))return fail(Failure::Transfer);
        io.write(0,1);
        if(!io.flush(0,16))return fail(Failure::Transfer);
        return true;
    }
    State poll(uint64_t now){
        if(current!=State::Running)return current;
        uint16_t received=0;
        switch(io.tryReceive(received)){
        case Receive::Error:fail(Failure::Transfer);return current;
        case Receive::Pending:
            if(now-started>=limit)fail(Failure::Timeout);
            return current;
        case Receive::Received:break;
        }
        if(!io.invalidate(0,32)){fail(Failure::Transfer);return current;}
        if(received!=sequence || io.read(0)!=0 || io.read(0x11)!=sequence){fail(Failure::Protocol);return current;}
        status=io.read(0x12);
        if(status!=0){fail(Failure::Rejected);return current;}
        current=State::Done;return current;
    }
    // Acknowledge only after collecting outputs or transferring their ownership
    // to the next resident batch. Prevents accidental overwrite of unread data.
    bool acknowledge(){if(current!=State::Done)return false;current=State::Ready;return true;}
    void cancel(){if(current!=State::Unbound)fail(Failure::Protocol);}
    // Lifecycle owner must stop/unload firmware and drain/reset its event first.
    // This does not make partially written sample/state buffers safe to replay.
    void resetAfterStop(){current=State::Unbound;failure=Failure::None;status=0;}
    State state()const{return current;}
    Failure error()const{return failure;}
    uint16_t dspStatus()const{return status;}
    uint16_t submittedSequence()const{return sequence;}
    uint64_t remainingTicks(uint64_t now)const{
        if(current!=State::Running)return 0;
        uint64_t elapsed=now-started;return elapsed>=limit?0:limit-elapsed;
    }
private:
    bool fail(Failure why){failure=why;current=State::Fault;return false;}
    Transport& io;
    State current=State::Unbound;
    Failure failure=Failure::None;
    uint16_t sequence=0,status=0;
    uint64_t started=0,limit=0;
};
}
