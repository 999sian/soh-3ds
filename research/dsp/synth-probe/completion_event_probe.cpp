#include <cstdio>
#include <stdexcept>
#include <fstream>
#include <type_traits>
#include "teakra/teakra.h"
#include "typed_firmware.h"
#include "resident_session.h"
struct Transport {
    Teakra::Teakra& dsp;bool event=false,flushFailure=false,invalidateFailure=false;
    unsigned notifications=0,receives=0;bool publicationValid=true;
    explicit Transport(Teakra::Teakra& d):dsp(d){
        dsp.SetRecvDataHandler(0,[this](){event=true;++notifications;publicationValid=publicationValid && dsp.DataRead(0)==0 && dsp.DataRead(0x11)==dsp.PeekRecvData(0);});
    }
    uint16_t read(unsigned address){return dsp.DataRead(address);}
    void write(unsigned address,uint16_t value){dsp.DataWrite(address,value);}
    bool flush(unsigned,unsigned){return !flushFailure;}
    bool invalidate(unsigned,unsigned){return !invalidateFailure;}
    ResidentDsp::Receive tryReceive(uint16_t& value){
        if(!event)return ResidentDsp::Receive::Pending;
        if(!dsp.RecvDataIsReady(0))return ResidentDsp::Receive::Error;
        value=dsp.RecvData(0);event=false;++receives;return ResidentDsp::Receive::Received;
    }
};
static_assert(!std::is_copy_constructible_v<ResidentDsp::Session<Transport>>);
static_assert(!std::is_move_constructible_v<ResidentDsp::Session<Transport>>);
int main(int argc,char** argv){
    auto require=[](bool ok,const char* why){if(!ok)throw std::runtime_error(why);};
    auto program=makeTypedFirmware(true,true,true);Teakra::Teakra dsp({});Transport io(dsp);ResidentDsp::Session<Transport> session(io);
    auto boot=[&](){
        dsp.Reset();io.event=false;
        for(unsigned i=0;i<program.words.size();++i)dsp.ProgramWrite(i,program.words[i]);
        for(unsigned i=0;i<0x4100;++i)dsp.DataWrite(i,0);
        dsp.Run(700);require(dsp.RecvDataIsReady(2),"loader reply");dsp.RecvData(2);
        require(session.attach(0x4457),"attach");
    };
    boot();using namespace ResidentDsp;
    Command copy{4,16,0,0};
    for(unsigned i=0;i<16;++i)dsp.DataWrite(0x3010+i,0x3510+i);
    require(session.submit(&copy,1,0,1000),"submit");
    require(session.poll(1)==State::Running,"nonblocking poll");
    require(!session.submit(&copy,1,1,1000),"reject concurrent submission");
    dsp.Run(20000);require(io.notifications==1 && io.publicationValid,"publish before interrupt");
    require(session.poll(20)==State::Done && io.receives==1,"interrupt completion");
    for(unsigned i=0;i<16;++i)require(dsp.DataRead(0x400+i)==0x3510+i,"copy output");
    require(!session.submit(&copy,1,20,1000),"retain outputs until acknowledgement");
    require(session.acknowledge(),"acknowledge");
    dsp.Run(20000);require(io.notifications==1,"no repeated idle interrupt");
    Command noop{0,0,0,0};bool wrapped=false;
    for(unsigned i=0;i<65536;++i){
        uint16_t previous=session.submittedSequence();
        require(session.submit(&noop,1,100+i,1000),"sequence stress submit");
        require(session.submittedSequence()!=0,"reserved zero sequence");
        if(previous==65535){require(session.submittedSequence()==1,"sequence wraps to one");wrapped=true;}
        dsp.Run(1000);require(session.poll(101+i)==State::Done && session.acknowledge(),"sequence stress completion");
    }
    require(wrapped && io.publicationValid,"sequence rollover observed");
    Command invalid{65535,16,0,0};
    require(session.submit(&invalid,1,30,1000),"invalid queue submit");dsp.Run(20000);
    require(session.poll(40)==State::Fault && session.error()==Failure::Rejected && session.dspStatus()==3,"DSP rejection");
    require(!session.acknowledge() && !session.submit(&copy,1,40,1000),"fault requires lifecycle recovery");
    dsp.SendData(2,0x8000);dsp.Run(700);require(dsp.RecvDataIsReady(2) && dsp.RecvData(2)==0,"unload reply");
    session.resetAfterStop();boot();
    require(session.submit(&copy,1,50,10),"timeout submit");
    require(session.remainingTicks(55)==5 && session.remainingTicks(60)==0,"remaining deadline");
    require(session.poll(60)==State::Fault && session.error()==Failure::Timeout,"timeout without spinning");
    // Late completion cannot silently reopen a timed-out session.
    dsp.Run(20000);require(session.poll(70)==State::Fault,"late completion held");
    dsp.SendData(2,0x8000);dsp.Run(700);dsp.RecvData(2);session.resetAfterStop();boot();
    require(session.submit(&copy,1,75,10),"stale event submit");io.event=true;
    require(session.poll(76)==State::Fault && session.error()==Failure::Transfer,"stale event without DR0 rejected");
    dsp.Run(20000);dsp.SendData(2,0x8000);dsp.Run(700);dsp.RecvData(2);session.resetAfterStop();boot();
    io.flushFailure=true;require(!session.submit(&copy,1,80,10) && session.error()==Failure::Transfer,"publication failure");
    if(argc>1){std::ofstream f;f.exceptions(std::ios::failbit|std::ios::badbit);f.open(std::string(argv[1])+"/completion-firmware-words.bin",std::ios::binary);for(auto w:program.words){f.put(char(w));f.put(char(w>>8));}}
    puts("PASS: DSP completion interrupt after publication, nonblocking session, 65536 sequence-rollover jobs, ownership acknowledgement, error notification, timeout, late completion and reload recovery");
}
