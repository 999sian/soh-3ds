#pragma once
#include <cstdint>
using Result=int32_t;using Handle=uint32_t;using u32=uintptr_t;using s64=int64_t;
#define R_SUCCEEDED(r) ((r)>=0)
#define R_FAILED(r) ((r)<0)
#define R_DESCRIPTION(r) ((r)&0x3ff)
constexpr int RD_TIMEOUT=0x3fe,RESET_STICKY=1;
constexpr Handle CUR_PROCESS_HANDLE=0xffff8001;
Result svcCreateEvent(Handle*,int);Result svcCloseHandle(Handle);
Result DSP_RegisterInterruptEvents(Handle,int,int);
Result svcFlushProcessDataCache(Handle,u32,unsigned);
Result svcInvalidateProcessDataCache(Handle,u32,unsigned);
Result svcWaitSynchronization(Handle,s64);Result svcClearEvent(Handle);
Result DSP_RecvDataIsReady(int,bool*);Result DSP_RecvData(int,uint16_t*);

void svcSleepThread(s64);
