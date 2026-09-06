#include "performanceTimer.h"

#ifdef __3DS__
#include <cstdint>

extern "C" uint64_t svcGetSystemTick(void);
// Reachability calls these timers for every entrance and check. Wall-clock
// reads on 3DS copy the shared time page and perform multiple 64-bit divisions;
// accumulate the hardware counter instead, converting only when reporting.
static std::array<uint64_t, PT_MAX> totalTicks = {};
static std::array<uint64_t, PT_MAX> timeStarted = {};
static bool detailedTimersEnabled = false;

void SetPerformanceTimerDetailEnabled(bool enabled) {
    detailedTimersEnabled = enabled;
}

static bool TimerEnabled(TimerID timer) {
    // A default seed evaluates these scopes millions of times. Even the
    // hardware tick SVC distorts generation time at that frequency.
    return detailedTimersEnabled || timer < PT_EVENT_ACCESS || timer > PT_LOCATION_LOGIC;
}

void StartPerformanceTimer(TimerID timer) {
    if (!TimerEnabled(timer)) return;
    timeStarted[timer] = svcGetSystemTick();
}

void StopPerformanceTimer(TimerID timer) {
    if (!TimerEnabled(timer)) return;
    totalTicks[timer] += svcGetSystemTick() - timeStarted[timer];
}

std::chrono::duration<double, std::milli> GetPerformanceTimer(TimerID timer) {
    // SYSCLOCK_ARM11 is fixed even when New 3DS CPU clock boost is enabled.
    return std::chrono::duration<double, std::milli>(static_cast<double>(totalTicks[timer]) * 1000.0 / 268123480.0);
}

void ResetPerformanceTimer(TimerID timer) {
    totalTicks[timer] = 0;
}

void ResetPerformanceTimers() {
    totalTicks = {};
}
#else
static std::array<std::chrono::duration<double, std::milli>, PT_MAX> totalTimes = {};
static std::array<std::chrono::high_resolution_clock::time_point, PT_MAX> timeStarted = {};

void StartPerformanceTimer(TimerID timer) {
    timeStarted[timer] = std::chrono::high_resolution_clock::now();
}

void StopPerformanceTimer(TimerID timer) {
    totalTimes[timer] += (std::chrono::high_resolution_clock::now() - timeStarted[timer]);
}

std::chrono::duration<double, std::milli> GetPerformanceTimer(TimerID timer) {
    return totalTimes[timer];
}

void ResetPerformanceTimer(TimerID timer) {
    totalTimes[timer] = {};
}

void ResetPerformanceTimers() {
    totalTimes = {};
}
#endif
