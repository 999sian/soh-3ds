#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
enum SohDspComponentState {SOH_DSP_COMPONENT_UNKNOWN,SOH_DSP_COMPONENT_STOPPED,SOH_DSP_COMPONENT_LOADED};
// Snapshot is observational, not a lock or permission to reclaim memory.
// The runtime must serialize component ownership operations through its owner.
unsigned SohDspObservedComponentState(void);
uint32_t SohDspObservedOwnershipChanges(void);
#ifdef __cplusplus
}
#endif
