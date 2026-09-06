// Dual-sink logging for 3DS tests.
//
// printf() goes to the on-screen console (consoleInit), which is what you want
// on hardware but is invisible to an emulator's stdout. consoleDebugInit with
// debugDevice_SVC routes stderr through svcOutputDebugString, which Azahar and
// Citra print to the host console — that is what makes automated, headless
// emulator testing possible.
//
// LOG() writes to both, so one binary serves hardware and CI.

#pragma once

#include <3ds.h>
#include <stdarg.h>
#include <stdio.h>

static inline void ctr_log_init(void) {
    consoleInit(GFX_TOP, NULL);
    consoleDebugInit(debugDevice_SVC); // stderr -> svcOutputDebugString
}

__attribute__((format(printf, 1, 2))) static inline void ctr_log(const char* fmt, ...) {
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    fputs(buf, stdout); // on-screen console
    fputs(buf, stderr); // emulator host log
    fflush(stderr);
}

#define LOG(...) ctr_log(__VA_ARGS__)
