#include <3ds.h>
#include <stdatomic.h>
#include "sdk_ownership.h"
static atomic_uint observedState=SOH_DSP_COMPONENT_UNKNOWN;
static atomic_uint ownershipChanges;
// Only the extracted SDK DSP object calls this symbol. Renaming its undefined
// svcSendSyncRequest reference catches inlined loads/unloads in dspInit/Exit
// and APT sleep/wake as well as public DSP_Load/UnloadComponent calls.
Result SohDspSvcSendSyncRequest(Handle handle){
    u32* command=getThreadCommandBuffer();
    unsigned id=command[0]>>16;
    bool ownership=id==0x11 || id==0x12;
    if(ownership)atomic_store_explicit(&observedState,SOH_DSP_COMPONENT_UNKNOWN,memory_order_release);
    Result result=svcSendSyncRequest(handle);
    if(ownership){
        unsigned state=SOH_DSP_COMPONENT_UNKNOWN;
        if(result==0 && command[1]==0)
            state=id==0x11 && (command[2]&0xff)?SOH_DSP_COMPONENT_LOADED:SOH_DSP_COMPONENT_STOPPED;
        atomic_store_explicit(&observedState,state,memory_order_release);
        atomic_fetch_add_explicit(&ownershipChanges,1,memory_order_release);
    }
    return result;
}
unsigned SohDspObservedComponentState(void){return atomic_load_explicit(&observedState,memory_order_acquire);}
uint32_t SohDspObservedOwnershipChanges(void){return atomic_load_explicit(&ownershipChanges,memory_order_acquire);}
