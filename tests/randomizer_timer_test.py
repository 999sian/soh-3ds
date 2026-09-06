#!/usr/bin/env python3
"""Run the real randomizer timers against a controlled 3DS hardware clock.

The 3DS profiling path must measure monotonic system ticks rather than repeatedly
converting shared-page wall time in each reachability predicate.
"""
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
TIMERS = ROOT / 'third_party/shipwright/soh/soh/Enhancements/debugger'
TEST = r'''
#include "performanceTimer.h"
#include <cassert>
#include <cmath>
#include <cstdint>
#include <iostream>
static uint64_t tick = UINT64_C(1) << 60;
static size_t clockReads = 0;
extern "C" uint64_t svcGetSystemTick() { ++clockReads; return tick; }
static void ExpectMilliseconds(TimerID timer, double expected) {
    const double got = GetPerformanceTimer(timer).count();
    if (std::abs(got - expected) > 0.000001) {
        std::cerr << "timer " << timer << ": expected " << expected << " ms, got " << got << '\n';
        std::exit(1);
    }
}
int main() {
    ResetPerformanceTimers();
    for (int i = 0; i < 1000; ++i) {
        StartPerformanceTimer(PT_LOCATION_LOGIC);
        StopPerformanceTimer(PT_LOCATION_LOGIC);
    }
    if (clockReads != 0) {
        std::cerr << "default fine-grained profiling read the clock " << clockReads << " times\n";
        return 1;
    }
    // Stage timing stays available without enabling hot-loop instrumentation.
    StartPerformanceTimer(PT_ADVANCEMENT_ITEMS);
    tick += 268123480;
    StopPerformanceTimer(PT_ADVANCEMENT_ITEMS);
    ExpectMilliseconds(PT_ADVANCEMENT_ITEMS, 1000.0);
    assert(clockReads == 2);
    SetPerformanceTimerDetailEnabled(true);
    ResetPerformanceTimers();
    StartPerformanceTimer(PT_WHOLE_SEED);
    StartPerformanceTimer(PT_TOD_ACCESS);
    tick += 268123480;
    StopPerformanceTimer(PT_TOD_ACCESS);
    ExpectMilliseconds(PT_TOD_ACCESS, 1000.0);
    StartPerformanceTimer(PT_TOD_ACCESS);
    tick += 134061740;
    StopPerformanceTimer(PT_TOD_ACCESS);
    StopPerformanceTimer(PT_WHOLE_SEED);
    ExpectMilliseconds(PT_TOD_ACCESS, 1500.0);
    ExpectMilliseconds(PT_WHOLE_SEED, 1500.0);
    ResetPerformanceTimer(PT_TOD_ACCESS);
    ExpectMilliseconds(PT_TOD_ACCESS, 0.0);
    ExpectMilliseconds(PT_WHOLE_SEED, 1500.0);
    ResetPerformanceTimers();
    ExpectMilliseconds(PT_WHOLE_SEED, 0.0);
    // A one-tick interval remains precise even at a large system uptime.
    StartPerformanceTimer(PT_LOCATION_LOGIC);
    tick += 1;
    StopPerformanceTimer(PT_LOCATION_LOGIC);
    ExpectMilliseconds(PT_LOCATION_LOGIC, 1000.0 / 268123480.0);
    std::cout << "randomizer hardware timer regression passed\n";
}
'''
with tempfile.TemporaryDirectory(prefix='soh-rando-timer-') as directory:
    directory = Path(directory)
    source = directory / 'test.cpp'
    source.write_text(TEST)
    binary = directory / 'test'
    subprocess.run([os.environ.get('CXX', 'g++'), '-std=c++17', '-O2', '-D__3DS__',
                    '-I', str(TIMERS), str(TIMERS / 'performanceTimer.cpp'), str(source),
                    '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
