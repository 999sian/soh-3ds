#pragma once
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif
// Install once before the first DSP component is loaded, with no SDK lifecycle
// transition in flight. The hook table is immutable thereafter. Hooks/context
// remain alive until process exit, including fatal/uncertain shutdown. Each
// handler serializes with producer/control operations. A handler may invoke
// its SDK continuation once for CPU/NDSP mode; custom mode must implement the
// whole transition, including the producer gate. Sleep chooses the path for its
// matching wake/cancel; a mode change must not split that pair. Never re-enter
// these wrappers.
typedef struct SohDspLifecycleHooks {
    void* context;
    bool (*sleep)(void*,bool (*sdkSleep)(void));
    void (*wake)(void*,void (*sdkWake)(void));
    void (*cancel)(void*,void (*sdkCancel)(void));
} SohDspLifecycleHooks;
bool SohDspLifecycleInstall(const SohDspLifecycleHooks* hooks);
// Native libctru entry points, also callable by the dedicated lifecycle probe.
bool aptDspSleep(void);
void aptDspWakeup(void);
void aptDspCancel(void);
#ifdef __cplusplus
}
#endif
