#pragma once
#include <cstdint>
using Result=int32_t;using u32=uintptr_t;using u8=uint8_t;
constexpr uint64_t SYSCLOCK_ARM11=268123480;
constexpr u32 CUR_PROCESS_HANDLE=0xffff8001;
#define R_FAILED(r) ((r)<0)
#define CSND_TIMER(n) (0x3fec3fcu/(n))
#define SOUND_CHANNEL(n) ((n)&31)
constexpr unsigned SOUND_FORMAT_16BIT=1<<12,SOUND_REPEAT=1<<10,SOUND_ENABLE=1<<14;
inline u32 CSND_VOL(float,float p){return p<0?0x8000:0x80000000;}
struct CSND_ChnInfo {uint8_t active;};
extern u32 csndChannels;
Result csndInit();void csndExit();void* linearAlloc(unsigned);void linearFree(void*);
u32 osConvertVirtToPhys(void*);uint64_t svcGetSystemTick();void svcSleepThread(uint64_t);
Result svcFlushProcessDataCache(u32,u32,unsigned);
u32* csndAddCmd(unsigned);Result csndExecCmds(bool);
void CSND_SetChnRegs(u32,u32,u32,u32,u32,u32);void CSND_SetPlayStateR(unsigned,unsigned);
CSND_ChnInfo* csndGetChnInfo(unsigned);
inline void __dmb(){}
