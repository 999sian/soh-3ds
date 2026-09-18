#include "system_3ds.h"

#include <3ds.h>
#include <atomic>
#include <limits>

namespace {
enum class CacheMode { Unprobed, Direct, Fallback };
std::atomic<CacheMode> sCacheMode{CacheMode::Unprobed};
}

extern "C" bool Soh3dsCleanDataCache(const void* address, size_t size) {
    if (!address || !size) return true;
    if (size > std::numeric_limits<u32>::max()) return false;
    if (sCacheMode.load(std::memory_order_relaxed) != CacheMode::Fallback) {
        // Store cleans without invalidating CPU lines or blocking on GSP IPC.
        // Some launch environments deny this SVC; remember the service fallback.
        const Result result = svcStoreProcessDataCache(CUR_PROCESS_HANDLE,
            static_cast<u32>(reinterpret_cast<uintptr_t>(address)), static_cast<u32>(size));
        if (R_SUCCEEDED(result)) {
            auto expected = CacheMode::Unprobed;
            sCacheMode.compare_exchange_strong(expected, CacheMode::Direct, std::memory_order_relaxed);
            return true;
        }
        sCacheMode.store(CacheMode::Fallback, std::memory_order_relaxed);
    }
    return R_SUCCEEDED(GSPGPU_FlushDataCache(address, static_cast<u32>(size)));
}

extern "C" bool Soh3dsInvalidateDataCache(const void* address, size_t size) {
    if (!address || !size) return true;
    if (size > std::numeric_limits<u32>::max()) return false;
    if (sCacheMode.load(std::memory_order_relaxed) != CacheMode::Fallback) {
        const Result result = svcInvalidateProcessDataCache(CUR_PROCESS_HANDLE,
            static_cast<u32>(reinterpret_cast<uintptr_t>(address)), static_cast<u32>(size));
        if (R_SUCCEEDED(result)) {
            auto expected = CacheMode::Unprobed;
            sCacheMode.compare_exchange_strong(expected, CacheMode::Direct, std::memory_order_relaxed);
            return true;
        }
        sCacheMode.store(CacheMode::Fallback, std::memory_order_relaxed);
    }
    return R_SUCCEEDED(GSPGPU_InvalidateDataCache(address, static_cast<u32>(size)));
}

extern "C" const char* Soh3dsDataCacheMode(void) {
    switch (sCacheMode.load(std::memory_order_relaxed)) {
        case CacheMode::Direct: return "direct-svc-store";
        case CacheMode::Fallback: return "gsp-fallback";
        default: return "unprobed";
    }
}

extern "C" uint64_t Mk64System3DSGetTick(void) {
    return svcGetSystemTick();
}

extern "C" uint64_t Mk64System3DSTicksPerSecond(void) {
    return SYSCLOCK_ARM11;
}
