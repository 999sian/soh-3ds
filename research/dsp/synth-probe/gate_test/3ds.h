#pragma once
#include <mutex>
struct LightLock {std::mutex mutex;};
inline void LightLock_Init(LightLock*){}
inline void LightLock_Lock(LightLock* lock){lock->mutex.lock();}
inline void LightLock_Unlock(LightLock* lock){lock->mutex.unlock();}
inline int LightLock_TryLock(LightLock* lock){return lock->mutex.try_lock()?0:1;}
