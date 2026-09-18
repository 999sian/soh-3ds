#pragma once
#include <stdint.h>
#include <stdbool.h>
typedef int32_t Result;
typedef uint32_t Handle;
typedef uint32_t u32;
u32* getThreadCommandBuffer(void);
Result svcSendSyncRequest(Handle);
