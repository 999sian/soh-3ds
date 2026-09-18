#include "resident_transport_3ds.h"
#include <array>
#include <cstdio>
#include <cstdlib>
static Result waitResult=0,readyResult=0,recvResult=0,clearResult=0;
static bool ready=true;
static unsigned readyCalls=0,recvCalls=0,clearCalls=0;
Result svcCreateEvent(Handle* h,int){*h=1;return 0;}
Result svcCloseHandle(Handle){return 0;}
Result DSP_RegisterInterruptEvents(Handle,int,int){return 0;}
Result svcFlushProcessDataCache(Handle,u32,unsigned){return 0;}
Result svcInvalidateProcessDataCache(Handle,u32,unsigned){return 0;}
Result svcWaitSynchronization(Handle,s64){return waitResult;}
Result svcClearEvent(Handle){++clearCalls;return clearResult;}
Result DSP_RecvDataIsReady(int,bool* value){++readyCalls;*value=ready;return readyResult;}
Result DSP_RecvData(int,uint16_t* value){++recvCalls;*value=42;return recvResult;}
static void require(bool b,const char* message){if(!b){std::fprintf(stderr,"FAIL: %s\n",message);std::exit(1);}}
int main(){
    using ResidentDsp::Receive;
    std::array<uint16_t,ResidentDsp::MappedSharedWords> memory{};ResidentDsp::CtrTransport transport;
    require(transport.open(memory.data()),"open");uint16_t sequence=0;
    require(transport.flush(0x40ff,1) && !transport.flush(0x4100,1) && !transport.invalidate(0x7400,9),"legacy map remains bounded");
    // Kernel timeout uses Info level: positive, so R_SUCCEEDED alone is wrong.
    waitResult=0x09401bfe;
    require(R_SUCCEEDED(waitResult),"real timeout is informational");
    require(transport.tryReceive(sequence)==Receive::Pending,"timeout must remain pending");
    require(readyCalls==0 && recvCalls==0 && clearCalls==0,"timeout must not touch DR0");
    waitResult=-1;require(transport.tryReceive(sequence)==Receive::Error,"wait error");
    waitResult=0;ready=false;require(transport.tryReceive(sequence)==Receive::Error,"spurious event");
    require(recvCalls==0,"empty DR0 must not block");
    ready=true;readyResult=-1;require(transport.tryReceive(sequence)==Receive::Error,"ready error");
    readyResult=0;recvResult=-1;require(transport.tryReceive(sequence)==Receive::Error,"receive error");
    require(clearCalls==0,"failed receive must not clear");
    recvResult=0;clearResult=-1;require(transport.tryReceive(sequence)==Receive::Error,"clear error");
    clearResult=0;require(transport.tryReceive(sequence)==Receive::Received && sequence==42,"completion");
    require(transport.closeAfterStop(),"close");
    require(!transport.flush(0,1) && !transport.invalidate(0,1),"closed map invalid");
    require(!transport.open(memory.data(),32) && !transport.open(memory.data(),ResidentDsp::MappedSharedWords+16),"unsupported map size rejected");
    require(transport.open(memory.data(),ResidentDsp::MappedSharedWords),"explicit mapped extent");
    require(transport.flush(0x7400,9) && transport.invalidate(0x741f,1),"last mapped state/cache line accessible");
    require(!transport.flush(0x7420,1) && !transport.invalidate(0x741f,2) && !transport.flush(UINT32_MAX,1) && !transport.invalidate(1,UINT32_MAX),"mapped endpoint and overflow guards");
    memory[0]=0;memory[0x10]=0x4458;ResidentDsp::Session<ResidentDsp::CtrTransport> session(transport);
    require(session.attach(0x4458),"mapped session attach");std::array<int16_t,256> table{};for(unsigned i=0;i<256;++i)table[i]=int(i*111)-16000;
    require(ResidentDsp::initializeMappedTables(transport,session,table.data()),"mapped bootstrap through native adapter");
    for(unsigned i=0;i<16;++i)require(memory[0x2800+i]==uint16_t(1u<<i),"ADPCM power table");
    for(unsigned i=0;i<256;++i)require(memory[0x4000+i]==uint16_t(table[i]),"resample table");
    session.resetAfterStop();require(transport.closeAfterStop() && transport.open(memory.data()),"reopen legacy after mapped stop");
    require(!transport.flush(0x7400,9) && transport.closeAfterStop(),"legacy reopen does not retain expanded access");
    std::puts("PASS: native transport mocked SVC/IPC timeout, spurious event, errors and completion, explicit legacy/mapped bounds, overflow/reopen guards and mapped bootstrap");
}
