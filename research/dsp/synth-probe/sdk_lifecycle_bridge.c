#include "sdk_lifecycle_bridge.h"
#include <stdatomic.h>
#include <stddef.h>

// prepare_sdk_lifecycle.py renames only these definitions/references in the
// installed SDK's dsp.o. Every DSP service operation and SDK state stays in
// that original object; no replacement firmware/SDK implementation is copied.
extern bool SohSdkDspSleep(void);
extern void SohSdkDspWakeup(void);
extern void SohSdkDspCancel(void);
static _Atomic(const SohDspLifecycleHooks*) installed;

bool SohDspLifecycleInstall(const SohDspLifecycleHooks* hooks){
    if(!hooks || !hooks->sleep || !hooks->wake || !hooks->cancel)return false;
    const SohDspLifecycleHooks* empty=NULL;
    return atomic_compare_exchange_strong_explicit(&installed,&empty,hooks,
        memory_order_release,memory_order_relaxed);
}
bool aptDspSleep(void){
    const SohDspLifecycleHooks* hooks=atomic_load_explicit(&installed,memory_order_acquire);
    return hooks?hooks->sleep(hooks->context,SohSdkDspSleep):SohSdkDspSleep();
}
void aptDspWakeup(void){
    const SohDspLifecycleHooks* hooks=atomic_load_explicit(&installed,memory_order_acquire);
    if(hooks)hooks->wake(hooks->context,SohSdkDspWakeup);else SohSdkDspWakeup();
}
void aptDspCancel(void){
    const SohDspLifecycleHooks* hooks=atomic_load_explicit(&installed,memory_order_acquire);
    if(hooks)hooks->cancel(hooks->context,SohSdkDspCancel);else SohSdkDspCancel();
}
