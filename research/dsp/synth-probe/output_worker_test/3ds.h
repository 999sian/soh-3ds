#pragma once
#include <cstdint>
#include <mutex>
#include <condition_variable>
using Result=int32_t;using s64=int64_t;using u64=uint64_t;
constexpr uint64_t SYSCLOCK_ARM11=268123480;
#define CSND_TIMER(n) (0x3fec3fcu/(n))
enum ResetType {RESET_ONESHOT,RESET_STICKY};
struct LightLock {std::mutex mutex;};
struct LightEvent {std::mutex mutex;std::condition_variable cv;bool signalled=false;ResetType reset=RESET_ONESHOT;};
struct ThreadTag;using Thread=ThreadTag*;
void LightLock_Init(LightLock*);void LightLock_Lock(LightLock*);void LightLock_Unlock(LightLock*);
void LightEvent_Init(LightEvent*,ResetType);void LightEvent_Clear(LightEvent*);void LightEvent_Signal(LightEvent*);int LightEvent_WaitTimeout(LightEvent*,s64);
Thread threadCreate(void(*)(void*),void*,unsigned,int,int,bool);Result threadJoin(Thread,u64);void threadFree(Thread);
u64 svcGetSystemTick();void svcSleepThread(u64);
