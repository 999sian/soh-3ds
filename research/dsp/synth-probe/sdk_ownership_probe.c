#include <3ds.h>
#include <assert.h>
#include <stdio.h>
#include "sdk_ownership.h"
Result SohDspSvcSendSyncRequest(Handle);
static u32 command[8];
static Result transportResult;
static u32 serviceResult,accepted;
u32* getThreadCommandBuffer(void){return command;}
Result svcSendSyncRequest(Handle handle){
    assert(handle==123);
    unsigned id=command[0]>>16;
    if(id==0x11 || id==0x12)assert(SohDspObservedComponentState()==SOH_DSP_COMPONENT_UNKNOWN);
    command[1]=serviceResult;command[2]=accepted;
    return transportResult;
}
static void request(unsigned id,Result transport,u32 service,u32 loaded,unsigned state){
    uint32_t before=SohDspObservedOwnershipChanges();
    command[0]=id<<16;transportResult=transport;serviceResult=service;accepted=loaded;
    assert(SohDspSvcSendSyncRequest(123)==transport);
    assert(SohDspObservedComponentState()==state);
    assert(SohDspObservedOwnershipChanges()==before+(id==0x11 || id==0x12));
    assert(command[1]==service && command[2]==loaded);
}
int main(void){
    assert(SohDspObservedComponentState()==SOH_DSP_COMPONENT_UNKNOWN);
    request(0x12,0,0,0,SOH_DSP_COMPONENT_STOPPED);
    request(0x11,0,0,1,SOH_DSP_COMPONENT_LOADED);
    request(0x15,-1,0,0,SOH_DSP_COMPONENT_LOADED);
    request(0x12,-1,0,0,SOH_DSP_COMPONENT_UNKNOWN);
    // No IPC on a subsequent SDK no-op cannot manufacture a new confirmation.
    uint32_t count=SohDspObservedOwnershipChanges();
    assert(SohDspObservedComponentState()==SOH_DSP_COMPONENT_UNKNOWN && SohDspObservedOwnershipChanges()==count);
    request(0x12,0,0,0,SOH_DSP_COMPONENT_STOPPED);
    request(0x11,0,0,0,SOH_DSP_COMPONENT_STOPPED);
    request(0x11,0,1,1,SOH_DSP_COMPONENT_UNKNOWN);
    request(0x11,1,0,1,SOH_DSP_COMPONENT_UNKNOWN);
    request(0x11,0,0,1,SOH_DSP_COMPONENT_LOADED);
    request(0x12,0,1,0,SOH_DSP_COMPONENT_UNKNOWN);
    puts("PASS: actual load/unload IPC observation, in-flight uncertainty, transport/service errors, explicit refusal and untouched unrelated IPC/results");
}
