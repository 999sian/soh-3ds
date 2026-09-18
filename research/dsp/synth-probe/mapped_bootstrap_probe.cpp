#include <array>
#include <cstdio>
#include <stdexcept>
#include "mapped_bootstrap.h"
using namespace ResidentDsp;
static void require(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
struct Transport {
    std::array<uint16_t,MappedSharedWords> memory{};unsigned writes=0,flushes=0,failFlush=0;bool failRead=false;
    uint16_t read(unsigned a){return memory.at(a);}void write(unsigned a,uint16_t v){++writes;memory.at(a)=v;}
    bool flush(unsigned a,unsigned n){return ++flushes!=failFlush && a<=MappedSharedWords && n<=MappedSharedWords-a;}
    bool invalidate(unsigned a,unsigned n){return !failRead && a<=MappedSharedWords && n<=MappedSharedWords-a;}
    Receive tryReceive(uint16_t&){return Receive::Pending;}
};
int main(){
    constexpr uintptr_t length=MappedSharedWords*2;
    for(uintptr_t base:{uintptr_t(0x1ff40000),uintptr_t(0x1ff60000),uintptr_t(0x1ff80000-length)})require(validMappedRegion(base,base+length-2),"required mapped extent");
    require(!validMappedRegion(0x1ff40002,0x1ff40000+length) && !validMappedRegion(0x1ff3ffe0,0x1ff3ffe0+length-2) && !validMappedRegion(0x1ff80000,0x1ff80000+length-2),"alignment and mapping bounds");
    require(!validMappedRegion(0x1ff40000,0x1ff40000+length) && !validMappedRegion(0x1ff40000,0x1ff40000+length-4) && !validMappedRegion(UINTPTR_MAX,UINTPTR_MAX),"inclusive endpoint and overflow refusal");
    std::array<int16_t,256> table{};for(unsigned i=0;i<256;++i)table[i]=int(i*173)-20000;
    for(unsigned fault=0;fault<6;++fault){
        Transport io;io.memory.fill(0xb35a);io.memory[0]=0;io.memory[0x10]=0x4458;
        Session<Transport> session(io);require(!initializeMappedTables(io,session,table.data()) && io.writes==0,"unbound initialization refuses");
        require(session.attach(0x4458),"attach");auto initial=io.memory;
        if(fault==1)io.failRead=true;
        if(fault==2)io.memory[0x10]=0x4457;
        if(fault==3)io.memory[0]=1;
        if(fault>=4)io.failFlush=fault-3;
        bool ok=initializeMappedTables(io,session,table.data());
        if(!fault){require(ok && session.state()==State::Ready && io.writes==272 && io.flushes==2,"mapped table publication");
            for(unsigned i=0;i<io.memory.size();++i){uint16_t expected=initial[i];if(i>=0x2800 && i<0x2810)expected=uint16_t(1u<<(i-0x2800));if(i>=0x4000 && i<0x4100)expected=uint16_t(table[i-0x4000]);require(io.memory[i]==expected,"only immutable table ranges changed");}
        }else{require(!ok && session.state()==State::Fault,"init failure faults owner");if(fault<4)require(io.writes==0,"preflight fails before writes");require(!initializeMappedTables(io,session,table.data()),"no retry in fault");}
    }
    std::puts("PASS: mapped service-address extent/alignment, exact boot table publication and memory guards, signature/busy/cache/partial-flush failure ownership");
}
