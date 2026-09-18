#include <array>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <sys/mman.h>
#include "mapped_component_3ds.h"
using namespace ResidentDsp;
static void require(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
static bool loaded=false,accept=true;
static Result loadResult=0,unloadResult=0;
static unsigned loads=0,unloads=0,closes=0,failMap=0,flushes=0,failFlush=0;
static unsigned scenario=0,invalidates=0;
static uint64_t ticks=0;
static auto* memory=reinterpret_cast<uint16_t*>(0x1ff40000);
bool dspIsComponentLoaded(){return loaded;}
Result DSP_LoadComponent(const void*,unsigned,uint16_t,uint16_t,bool* out){++loads;loaded=accept;*out=loaded;return loadResult;}
Result DSP_UnloadComponent(){if(!loaded)return 0;++unloads;loaded=false;return unloadResult;}
Result DSP_ConvertProcessAddressFromDspDram(unsigned word,u32* out){*out=reinterpret_cast<u32>(memory+word);return failMap?-1:0;}
uint64_t svcGetSystemTick(){return ticks;}
Result svcSleepThread(s64 ns){ticks+=uint64_t(ns)*SYSCLOCK_ARM11/1000000000;return 0;}
Result svcCreateEvent(Handle* out,int){*out=42;return scenario==10?-1:0;}
Result svcCloseHandle(Handle){++closes;return scenario==15?-1:0;}
Result DSP_RegisterInterruptEvents(Handle event,int,int){return (scenario==11 && event) || (scenario==13 && !event)?-1:0;}
Result svcFlushProcessDataCache(Handle,u32,unsigned){return ++flushes==failFlush?-1:0;}
Result svcInvalidateProcessDataCache(Handle,u32,unsigned){return ++invalidates==2 && scenario==12?-1:0;}
Result svcWaitSynchronization(Handle,s64){return RD_TIMEOUT;}
Result svcClearEvent(Handle){return 0;}
Result DSP_RecvDataIsReady(int,bool* out){*out=false;return 0;}
Result DSP_RecvData(int,uint16_t*){return -1;}
int main(){
    constexpr unsigned span=0x10000;
    require(mmap(memory,span,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED_NOREPLACE,-1,0)==memory,"fixed valid DSP test map");
    std::array<int16_t,256> table{};
    for(unsigned i=0;i<table.size();++i)table[i]=int16_t(i*97);
    for(unsigned fault=0;fault<16;++fault){
        loaded=false;accept=true;loadResult=unloadResult=0;loads=unloads=closes=failMap=flushes=failFlush=0;ticks=0;
        scenario=fault;invalidates=0;
        std::memset(memory,0,span);memory[0x10]=0x4458;
        CtrTransport io;Session<CtrTransport> session(io);MappedComponent3ds component(io,session);
        if(fault==1)loaded=true;
        if(fault==2){loadResult=-1;accept=false;}
        if(fault==3)loadResult=-1;
        if(fault==4)accept=false;
        if(fault==5)failMap=1;
        if(fault==6)memory[0x10]=0;
        if(fault==7)failFlush=2;
        bool ok=component.start(table.data(),sizeof(table),table.data(),[&](){return fault==8;});
        require(ok==(fault==0 || fault==9 || fault>=13),"startup classification");
        if(ok){
            require(session.state()==State::Ready && loads==1 && component.loaded(),"mapped session ready");
            require(!component.start(table.data(),sizeof(table),table.data(),[](){return false;}) && loads==1,"no duplicate start");
            for(unsigned i=0;i<table.size();++i)require(memory[0x4000+i]==uint16_t(table[i]),"resample table upload");
        }
        if(fault==9)unloadResult=-1;
        if(fault==14)unloadResult=RD_TIMEOUT;
        bool stopped=component.stop();
        if(fault==1 || fault==2 || fault==9 || fault>=13){
            require(!stopped && component.uncertain(),"uncertain ownership retained");
            unsigned prior=unloads;
            require(!component.stop() && unloads==prior,"SDK no-op retry cannot confirm stop");
            require(!component.start(table.data(),sizeof(table),table.data(),[](){return false;}),"uncertain restart refused");
            if(fault==9 || fault==14)require(io.completionEvent()==42 && closes==0 && session.state()==State::Ready,"retain event/session after failed unload");
            if(fault==13 || fault==15)require(component.lastResult()==-1 && session.state()==State::Ready,"failed close diagnostic and no session reset");
        }else{
            require(stopped && !component.loaded() && session.state()==State::Unbound && !io.completionEvent(),"confirmed cleanup");
            if(fault!=4)require(unloads==1,"partial start requires unload");
            if(fault==0){
                require(component.start(table.data(),sizeof(table),table.data(),[](){return false;}),"same owner fresh component lifetime");
                // Models unexpected SDK flag clearing by another lifecycle.
                loaded=false;
                require(!component.stop() && component.uncertain() && unloads==1,"external unload is not confirmation");
            }
        }
    }
    munmap(memory,span);
    std::puts("PASS: native mapped component boot/tables/restart, existing component refusal, cancellation/deadline, partial load/map/table/event/attach failures, uncertain load/unload/close, positive unload timeout and external SDK flag retention");
}
