#!/usr/bin/env python3
"""Exercise production hook registration/dispatch/removal without the game types.

The complete hook implementation is extracted unchanged from GameInteractor.h.
The regular test checks all four dispatch routes in 3DS, desktop and absent
weak-provider builds. --benchmark reports host dispatch timings, never FPS.
--source permits the same benchmark to run against a saved pre-change header.
"""

import argparse
import os
from pathlib import Path
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[1]
HEADER = ROOT / "third_party/shipwright/soh/soh/Enhancements/game-interactor/GameInteractor.h"
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--source", type=Path, default=HEADER)
parser.add_argument("--benchmark", action="store_true")
args = parser.parse_args()
production = args.source.read_text()
support = production[production.index("typedef uint32_t HOOK_ID;"):
                     production.index("#define REGISTER_VB_SHOULD(")]
hooks = production[production.index("    // Game Hooks\n"):
                   production.index("    void RemoveAllQueuedHooks()")]

fixture = r'''
#include <algorithm>
#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <map>
#include <new>
#include <source_location>
#include <unordered_map>
#include <vector>
#include "ship/utils/logging_3ds.h"

static size_t allocations = 0;
[[gnu::noinline]] void* operator new(size_t size) {
    if (void* memory = std::malloc(size ? size : 1)) { ++allocations; return memory; }
    throw std::bad_alloc();
}
[[gnu::noinline]] void operator delete(void* memory) noexcept { std::free(memory); }
[[gnu::noinline]] void operator delete(void* memory, size_t) noexcept { std::free(memory); }

std::atomic<unsigned> loggingFlags{0};
unsigned flagReads = 0;
#ifndef OMIT_FLAGS
extern "C" unsigned Soh3dsLoggingFlags() {
    ++flagReads;
    return loggingFlags.load(std::memory_order_relaxed);
}
#endif
''' + support + r'''
class GameInteractor {
public:
''' + hooks + r'''
};
struct TestHook {
    using fn = std::function<void(int)>;
    using filter = std::function<bool(int)>;
};
using Registered = GameInteractor::RegisteredGameHooks<TestHook>;
using Pending = GameInteractor::HooksToUnregister<TestHook>;
GameInteractor gi;
enum Route { Normal, ID, Pointer, Filter };
uint64_t delivered = 0;

static void Reset() {
    Registered::functions.clear();
    Registered::functionsForID.clear();
    Registered::functionsForPtr.clear();
    Registered::functionsForFilter.clear();
    Registered::hookData.clear();
    Pending::hooks.clear();
    Pending::hooksForID.clear();
    Pending::hooksForPtr.clear();
    Pending::hooksForFilter.clear();
    gi.nextHookId = 1;
    delivered = 0;
    loggingFlags = 0;
    flagReads = 0;
}
static HOOK_ID Register(Route route, TestHook::fn callback = [](int value) { delivered += value; }) {
    switch (route) {
        case Normal: return gi.RegisterGameHook<TestHook>(callback);
        case ID: return gi.RegisterGameHookForID<TestHook>(17, callback);
        case Pointer: return gi.RegisterGameHookForPtr<TestHook>(uintptr_t{0x1234}, callback);
        case Filter: return gi.RegisterGameHookForFilter<TestHook>([](int value) { return value > 0; }, callback);
    }
    std::abort();
}
static void Dispatch(Route route, int value = 1) {
    switch (route) {
        case Normal: gi.ExecuteHooks<TestHook>(value); break;
        case ID: gi.ExecuteHooksForID<TestHook>(17, value); break;
        case Pointer: gi.ExecuteHooksForPtr<TestHook>(uintptr_t{0x1234}, value); break;
        case Filter: gi.ExecuteHooksForFilter<TestHook>(value); break;
    }
}
static void Unregister(Route route, HOOK_ID hook) {
    switch (route) {
        case Normal: gi.UnregisterGameHook<TestHook>(hook); break;
        case ID: gi.UnregisterGameHookForID<TestHook>(hook); break;
        case Pointer: gi.UnregisterGameHookForPtr<TestHook>(hook); break;
        case Filter: gi.UnregisterGameHookForFilter<TestHook>(hook); break;
    }
}
static bool CountsEnabled(unsigned flags) {
#ifdef TEST_DESKTOP
    (void)flags;
    return true;
#elif defined(OMIT_FLAGS)
    (void)flags;
    return false;
#else
    return (flags & (SOH3DS_LOG_GENERAL | SOH3DS_LOG_PROFILE)) == (SOH3DS_LOG_GENERAL | SOH3DS_LOG_PROFILE);
#endif
}
static uint64_t CountSum() {
    uint64_t result = 0;
    for (const auto& [id, info] : Registered::hookData) result += info.calls;
    return result;
}

static void TestRoute(Route route) {
    Reset();
    const auto first = Register(route);
    Register(route);
    Register(route);
    Register(route);
    uint64_t expectedCalls = 0;
    for (unsigned flags : {0u, unsigned(SOH3DS_LOG_GENERAL), unsigned(SOH3DS_LOG_PROFILE),
                          unsigned(SOH3DS_LOG_GENERAL | SOH3DS_LOG_FRAMES | SOH3DS_LOG_TEXTURES),
                          unsigned(SOH3DS_LOG_GENERAL | SOH3DS_LOG_PROFILE), 0u}) {
        loggingFlags = flags;
        const auto oldDelivered = delivered;
        const auto oldAllocations = allocations;
        const auto oldReads = flagReads;
        Dispatch(route, 2);
        assert(delivered == oldDelivered + 8); // callbacks unaffected by profiling
        if (CountsEnabled(flags)) expectedCalls += 4;
        assert(CountSum() == expectedCalls);
        assert(allocations == oldAllocations); // counting cannot allocate existing metadata
#if !defined(TEST_DESKTOP) && !defined(OMIT_FLAGS)
        assert(flagReads == oldReads + 1); // one cached flag read per dispatch, not per hook
#else
        assert(flagReads == oldReads);
#endif
    }
    if (route == Filter) {
        loggingFlags = SOH3DS_LOG_GENERAL | SOH3DS_LOG_PROFILE;
        const auto oldDelivered = delivered;
        const auto oldCount = CountSum();
        Dispatch(route, -1);
        assert(delivered == oldDelivered && CountSum() == oldCount);
    }

    // Removing metadata deliberately must not make the disabled hot path
    // reconstruct map nodes. Desktop/profile-on retain their original counting.
    loggingFlags = 0;
    Registered::hookData.clear();
    const auto oldAllocations = allocations;
    Dispatch(route);
    if (!CountsEnabled(0)) {
        assert(Registered::hookData.empty() && allocations == oldAllocations);
    } else {
        assert(Registered::hookData.size() == 4 && CountSum() == 4);
    }
    Unregister(route, first);
    gi.ProcessUnregisteredHooks<TestHook>();
    const auto beforeRemovalDispatch = delivered;
    Dispatch(route);
    assert(delivered == beforeRemovalDispatch + 3);
    assert(Registered::hookData.count(first) == 0); // removed counters never reappear

    // Self-removal is queued by a callback; the current call can be counted,
    // and the next dispatch removes it before iteration on every route.
    Reset();
    loggingFlags = SOH3DS_LOG_GENERAL | SOH3DS_LOG_PROFILE;
    HOOK_ID self = 0;
    self = Register(route, [&](int) { ++delivered; Unregister(route, self); });
    Dispatch(route);
    assert(delivered == 1);
    Dispatch(route);
    assert(delivered == 1 && Registered::hookData.count(self) == 0);
    const auto readsBeforeEmpty = flagReads;
    Dispatch(route);
    assert(flagReads == readsBeforeEmpty); // empty hook groups need no flag read
}

static void TestDispatchSnapshot() {
    Reset();
    unsigned expected = 0;
    for (unsigned initial : {0u, unsigned(SOH3DS_LOG_GENERAL | SOH3DS_LOG_PROFILE)}) {
        Reset();
        loggingFlags = initial;
        const auto other = initial ? 0u : unsigned(SOH3DS_LOG_GENERAL | SOH3DS_LOG_PROFILE);
        Register(Normal, [=](int) { ++delivered; loggingFlags = other; });
        Register(Normal);
        Dispatch(Normal);
        expected = CountsEnabled(initial) ? 2 : 0;
        assert(delivered == 2 && CountSum() == expected);
        Dispatch(Normal);
        expected += CountsEnabled(other) ? 2 : 0;
        assert(delivered == 4 && CountSum() == expected);
    }
}

static void Benchmark() {
    constexpr int iterations = 50000;
    constexpr int perRoute = 32;
    constexpr int repeats = 5;
    for (unsigned flags : {0u, unsigned(SOH3DS_LOG_GENERAL), unsigned(SOH3DS_LOG_GENERAL | SOH3DS_LOG_PROFILE)}) {
        Reset();
        for (auto route : {Normal, ID, Pointer, Filter}) {
            for (int i = 0; i < perRoute; ++i) Register(route);
        }
        loggingFlags = flags;
        std::vector<double> elapsed;
        elapsed.reserve(repeats);
        const auto firstAllocations = allocations;
        for (int repeat = 0; repeat < repeats; ++repeat) {
            auto begin = std::chrono::steady_clock::now();
            for (int i = 0; i < iterations; ++i) {
                Dispatch(Normal); Dispatch(ID); Dispatch(Pointer); Dispatch(Filter);
            }
            elapsed.push_back(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - begin).count());
        }
        std::sort(elapsed.begin(), elapsed.end());
        std::printf("{\"flags\":%u,\"callbacks_per_sample\":%d,\"median_ms\":%.3f,\"min_ms\":%.3f,\"max_ms\":%.3f,"
                    "\"total_callbacks\":%llu,\"counter_updates\":%llu,\"dispatch_allocations\":%zu,\"flag_reads\":%u}\n",
                    flags, iterations * perRoute * 4, elapsed[repeats / 2], elapsed.front(), elapsed.back(),
                    static_cast<unsigned long long>(delivered), static_cast<unsigned long long>(CountSum()),
                    allocations - firstAllocations, flagReads);
    }
}
int main(int argc, char**) {
    if (argc > 1) { Benchmark(); return 0; }
    for (auto route : {Normal, ID, Pointer, Filter}) TestRoute(route);
    TestDispatchSnapshot();
    std::puts("hook dispatch profiling, callbacks, filtering and removal passed");
}
'''

with tempfile.TemporaryDirectory(prefix="soh-hook-profiling-") as temporary:
    temporary = Path(temporary)
    source = temporary / "test.cpp"
    source.write_text(fixture)
    variants = [("3ds", ["-D__3DS__"])]
    if not args.benchmark:
        variants += [("desktop", ["-DTEST_DESKTOP"]), ("weak-absent", ["-D__3DS__", "-DOMIT_FLAGS"])]
    for name, flags in variants:
        binary = temporary / name
        subprocess.run([
            os.environ.get("CXX", "g++"), "-std=c++20", "-O2", "-Wall", "-Wextra", "-Werror", *flags,
            "-I", str(ROOT / "third_party/libultraship/include"), str(source), "-o", str(binary),
        ], check=True)
        subprocess.run([str(binary), *(["benchmark"] if args.benchmark else [])], check=True)
