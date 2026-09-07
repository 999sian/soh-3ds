#!/usr/bin/env python3
"""Verify the production profiler's disabled path, sampling and window rollover."""
from pathlib import Path
import os
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / 'platform/3ds/source/gfx_citro3d.cpp').read_text()
begin = source.index('struct Soh3dsProfileCounter {')
end = source.index('// Frame pacing state', begin)
report_begin = source.index('        if (sRenderProfileEnabled) {', end)
report_end = source.index('        // Every 10th sample', report_begin)
profile_code = source[begin:end]
if 'Soh3dsOpenPerformanceLog' not in profile_code:
    # Run the original inline initialization before the helper is introduced,
    # so the new light-capture contract fails against the actual old behavior.
    init_begin = source.index('static FILE* sPerformanceLog = []() -> FILE* {')
    init_begin = source.index('{', init_begin) + 1
    init_end = source.index('        }();', init_begin)
    profile_code += '\nstatic FILE* Soh3dsOpenPerformanceLog() {\n' + source[init_begin:init_end] + '\n}\n'
code = r'''
#include <array>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <sys/stat.h>
#include <unistd.h>
#include "fast/backends/gfx_profile_3ds.h"
#include "ship/utils/logging_3ds.h"
unsigned testLoggingFlags = 0;
extern "C" unsigned Soh3dsLoggingFlags() { return testLoggingFlags; }
uint64_t clockTicks = 100;
unsigned clockReads = 0;
uint64_t svcGetSystemTick() { ++clockReads; return ++clockTicks; }
''' + profile_code + r'''
unsigned sFrames = std::numeric_limits<unsigned>::max();
FILE* sPerformanceLog = nullptr;
void Report() {
''' + source[report_begin:report_end] + r'''
}
int main() {
    // Basic capture must not turn on per-draw clocks. The extra profile flag
    // is inert without capture, and output failure must leave probes off.
    assert(Soh3dsOpenPerformanceLog() == nullptr);
    assert(!sRenderProfileEnabled);
    auto flag = std::fopen("perfprofile.flag", "wb"); assert(flag); std::fclose(flag);
    assert(Soh3dsOpenPerformanceLog() == nullptr);
    assert(!sRenderProfileEnabled);
    std::remove("perfprofile.flag");
    flag = std::fopen("perftrace.flag", "wb"); assert(flag); std::fclose(flag);
    assert(Soh3dsOpenPerformanceLog() == nullptr); // legacy flags cannot opt in
    testLoggingFlags = SOH3DS_LOG_GENERAL;
    auto capture = Soh3dsOpenPerformanceLog(); assert(capture);
    assert(!sRenderProfileEnabled);
    { Soh3dsProfileScope scope(Soh3dsProfileSection::Triangle); }
    Soh3dsProfilePackBatch(true, 3);
    assert(clockReads == 0 && sPackCoverage.totalBatches == 0);
    std::fclose(capture); std::remove("perf.csv");
    flag = std::fopen("perfprofile.flag", "wb"); assert(flag); std::fclose(flag);
    testLoggingFlags |= SOH3DS_LOG_PROFILE;
    capture = Soh3dsOpenPerformanceLog(); assert(capture);
    assert(sRenderProfileEnabled);
    std::fclose(capture); std::remove("perf.csv");
    assert(mkdir("perf.csv", 0700) == 0);
    assert(Soh3dsOpenPerformanceLog() == nullptr);
    assert(!sRenderProfileEnabled);
    assert(rmdir("perf.csv") == 0);
    std::remove("perftrace.flag"); std::remove("perfprofile.flag");
    testLoggingFlags = 0;
    assert(Soh3dsPerformanceLog() == nullptr);
    testLoggingFlags = SOH3DS_LOG_GENERAL | SOH3DS_LOG_PROFILE;
    assert(Soh3dsPerformanceLog() != nullptr && sRenderProfileEnabled);
    testLoggingFlags = SOH3DS_LOG_GENERAL;
    assert(Soh3dsPerformanceLog() != nullptr && !sRenderProfileEnabled);
    testLoggingFlags = 0;
    assert(Soh3dsPerformanceLog() == nullptr && !sRenderProfileEnabled);
    for (unsigned i = 0; i < static_cast<unsigned>(Soh3dsProfileSection::Count); ++i) {
        Soh3dsProfileScope scope(static_cast<Soh3dsProfileSection>(i));
    }
    assert(clockReads == 0);
    Soh3dsProfilePackBatch(true, 12);
    assert(sPackCoverage.totalBatches == 0);
    for (const auto& c : sRenderProfile) assert(c.calls == 0 && c.samples == 0);
    sRenderProfileEnabled = true;
    Soh3dsProfilePackBatch(false, 3);
    Soh3dsProfilePackBatch(true, 12);
    assert(clockReads == 0);
    assert(sPackCoverage.totalBatches == 2 && sPackCoverage.totalVertices == 15);
    assert(sPackCoverage.commonBatches == 1 && sPackCoverage.commonVertices == 12);
    for (unsigned i = 0; i < static_cast<unsigned>(Soh3dsProfileSection::Count); ++i) {
        for (int call = 0; call < 128; ++call) {
            Soh3dsProfileScope scope(static_cast<Soh3dsProfileSection>(i));
        }
        const auto section = static_cast<Soh3dsProfileSection>(i);
        const bool perCall = section == Soh3dsProfileSection::DisplayList ||
                             section == Soh3dsProfileSection::Depth ||
                             section == Soh3dsProfileSection::Interpolate;
        const bool frequent = section == Soh3dsProfileSection::Vertex ||
                              section == Soh3dsProfileSection::Triangle ||
                              section == Soh3dsProfileSection::TriangleKey ||
                              section == Soh3dsProfileSection::TriangleEmit;
        const unsigned expected = perCall ? 128 : frequent ? 2 : 8;
        assert(sRenderProfile[i].calls == 128);
        assert(sRenderProfile[i].samples == expected);
        assert(sRenderProfile[i].ticks == expected);
    }
    const unsigned tri = static_cast<unsigned>(Soh3dsProfileSection::Triangle);
    for (int i = 0; i < 17; ++i) { Soh3dsProfileScope scope(Soh3dsProfileSection::Triangle); }
    auto& c = sRenderProfile[tri];
    c.calls = c.samples = 0;
    c.ticks = 0; // The reporting window preserves ordinal.
    for (int i = 0; i < 47; ++i) { Soh3dsProfileScope scope(Soh3dsProfileSection::Triangle); }
    assert(c.calls == 47 && c.samples == 1 && c.ticks == 1);
    const unsigned reads = clockReads;
    sRenderProfileEnabled = false;
    { Soh3dsProfileScope scope(Soh3dsProfileSection::Triangle); }
    assert(clockReads == reads && c.calls == 47);
    sRenderProfileEnabled = true;
    assert(Soh3dsProfileBegin(1000) == 0);
    Soh3dsProfileEnd(1000, 100);

    // Exercise the production formatter with the largest representable fields:
    // an undersized buffer silently omits the entire record.
    sPerformanceLog = std::tmpfile();
    assert(sPerformanceLog != nullptr);
    for (auto& counter : sRenderProfile) {
        counter.ticks = std::numeric_limits<uint64_t>::max();
        counter.calls = counter.samples = std::numeric_limits<uint32_t>::max();
        counter.ordinal = 17;
    }
    Report();
    std::rewind(sPerformanceLog);
    char record[2048];
    assert(std::fgets(record, sizeof(record), sPerformanceLog) != nullptr);
    assert(std::strchr(record, '\n') != nullptr);
    for (const char* label : {"dl", "draw", "pack", "state", "depth", "interp",
                              "vertex", "triangle", "tristate", "texture", "trikey", "emit"}) {
        char expected[128];
        std::snprintf(expected, sizeof(expected), " %s=%llu/%u/%u", label,
                      static_cast<unsigned long long>(std::numeric_limits<uint64_t>::max() / 268u),
                      std::numeric_limits<uint32_t>::max(), std::numeric_limits<uint32_t>::max());
        assert(std::strstr(record, expected) != nullptr);
    }
    assert(std::fgets(record, sizeof(record), sPerformanceLog) != nullptr);
    assert(std::strstr(record, "units=batches/vertices common=1/12 total=2/15\n") != nullptr);
    assert(sPackCoverage.totalBatches == 0 && sPackCoverage.totalVertices == 0);
    assert(sPackCoverage.commonBatches == 0 && sPackCoverage.commonVertices == 0);
    assert(std::fgets(record, sizeof(record), sPerformanceLog) == nullptr);
    std::fclose(sPerformanceLog);
    for (const auto& counter : sRenderProfile) {
        assert(counter.ticks == 0 && counter.calls == 0 && counter.samples == 0);
        assert(counter.ordinal == 17);
    }
    std::puts("Light capture, explicit detail flag, disabled clocks, sample rates, rollover and reporting pass");
}
'''
with tempfile.TemporaryDirectory(prefix='soh-profile-') as directory:
    p = Path(directory)
    (p / 'test.cpp').write_text(code)
    flags = ['-fsanitize=address,undefined', '-fno-omit-frame-pointer', '-no-pie'] if os.getenv('SANITIZE') else []
    subprocess.run([os.getenv('CXX', 'g++'), '-std=c++20', '-O1', '-D__3DS__', *flags,
                    '-I' + str(root / 'third_party/libultraship/include'), str(p / 'test.cpp'),
                    '-o', str(p / 'test')], check=True)
    subprocess.run([str(p / 'test')], check=True, cwd=p)
