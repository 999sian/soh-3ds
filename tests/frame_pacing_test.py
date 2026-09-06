#!/usr/bin/env python3
"""Exercise production pacing with distinct GPU and two-screen LCD events.

100 clock units represent a 60 Hz LCD interval. GPU completion publishes a
swap; each LCD consumes it at its own next blank. FrameBegin can finish
between blanks, as on the device. This checks scheduling and submission
safety, not physical rendering, interrupt races, or audio playback.

PACING_SOURCE may name an earlier renderer snapshot for a regression red run.
"""
import os
from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
source = Path(os.environ.get('PACING_SOURCE', ROOT / 'platform/3ds/source/gfx_citro3d.cpp')).read_text()
globals_start = source.index('constexpr uint32_t kMaxPacingDebtVblanks')
globals_code = source[globals_start:source.index('namespace Fast {', globals_start)]
end_start = source.index('void GfxRenderingAPICitro3D::EndFrame()')
end_code = source[end_start:source.index('    // SoH-3DS DIAGNOSTIC:', end_start)] + '\n}\n'
start = source.index('    {\n        const uint64_t now = svcGetSystemTick();',
                     source.index('void GfxRenderingAPICitro3D::StartFrame()'))
start_end = source.index('    if (trace) {', source.index('    mImpl->frameStartTick = svcGetSystemTick();', start))
start_code = source[start:start_end]
wake_start = source.index('extern "C" void Soh3dsGraphicsWake()')
wake_code = source[wake_start:source.index('\n}', wake_start) + 2]
rate_code = re.search(r'    C3D_FrameRate\([^;]+;', source).group(0)
reset_swap = ('sLastFrameEndVblank[0] = sLastFrameEndVblank[1] = 0;'
              if 'sLastFrameEndVblank' in globals_code else 'sSwapReadyVblank = 0;')

code = r'''
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <limits>

constexpr uint64_t Never = std::numeric_limits<uint64_t>::max();
uint64_t clockUnits, gpuReady;
std::array<uint64_t, 2> nextBlank;
std::array<uint32_t, 2> lcdCounter, pendingSwap;
uint32_t lastSubmitted;
unsigned gpuCost, unsafeSubmissions, consumedFrames, maxLcdGap;
std::array<unsigned, 8> gapCounts;
uint64_t previousConsume;
int targetFps, counterPeriod;
bool gpuBottomTransfer, nextBottomTransfer, failNextBegin;
unsigned failures;

void check(bool condition, const char* label) {
    if (!condition) { std::fprintf(stderr, "FAIL: %s\n", label); ++failures; }
}
void advance(uint64_t target) {
    while (true) {
        const uint64_t next = std::min({gpuReady, nextBlank[0], nextBlank[1]});
        if (next > target) break;
        clockUnits = next;
        // Tie convention: completed GPU work publishes before a same-time blank.
        if (gpuReady == next) {
            pendingSwap[0] = lastSubmitted;
            if (gpuBottomTransfer) pendingSwap[1] = lastSubmitted;
            gpuReady = Never;
        }
        for (unsigned screen = 0; screen < 2; ++screen) {
            if (nextBlank[screen] != next) continue;
            if ((nextBlank[screen] / 100) % counterPeriod == 0) ++lcdCounter[screen];
            if (pendingSwap[screen]) {
                if (screen == 0) {
                    if (previousConsume) {
                        const unsigned gap = (clockUnits - previousConsume) / 100;
                        maxLcdGap = std::max(maxLcdGap, gap);
                        ++gapCounts[std::min<unsigned>(gap, gapCounts.size() - 1)];
                    }
                    previousConsume = clockUnits;
                    ++consumedFrames;
                }
                pendingSwap[screen] = 0;
            }
            nextBlank[screen] += 100;
        }
    }
    clockUnits = target;
}
uint32_t C3D_FrameCounter(unsigned screen) { return lcdCounter[screen]; }
void C3D_FrameRate(float rate) { counterPeriod = rate < 60.0f ? 2 : 1; }
uint64_t svcGetSystemTick() { return clockUnits; }
int Soh3dsTargetFps() { return targetFps; }
void gspWaitForAnyEvent() { advance(std::min({gpuReady, nextBlank[0], nextBlank[1]})); }
bool C3D_FrameBegin(int) {
    if (failNextBegin) { failNextBegin = false; return false; }
    if (gpuReady != Never) advance(gpuReady);
    return true;
}
bool gspIsPresentPending(unsigned screen) { return pendingSwap[screen] != 0; }
void C3D_FrameEnd(int) {
    if (pendingSwap[0] || pendingSwap[1]) ++unsafeSubmissions;
    ++lastSubmitted;
    gpuReady = clockUnits + gpuCost;
    gpuBottomTransfer = nextBottomTransfer;
    nextBottomTransfer = false;
}
void C3D_FrameSplit(int) {}
void C3D_FrameDrawOn(void* target) { if (target != nullptr) nextBottomTransfer = true; }
void C3D_RenderTargetClear(void*, int, int, int) {}
constexpr int GX_CMDLIST_FLUSH = 1, C3D_CLEAR_ALL = 1;
''' + globals_code + r'''
struct State {
    bool frameActive = false, bottomDrawnLastFrame = false, bottomDrawnThisFrame = false;
    bool externalLinearBuffersDirty = false;
    void* bottomTarget = reinterpret_cast<void*>(1);
    uint64_t busyTickAccumulator = 0, waitTickAccumulator = 0, frameStartTick = 0;
    uint64_t frameEndTick = 0, loopTickAccumulator = 0;
    uint64_t sampleFrameSplitCount = 0, linearHeapFlushFrameCount = 0;
};
struct GfxRenderingAPICitro3D {
    State state;
    State* mImpl = &state;
    void StartFrame();
    void EndFrame();
    void PresentSceneToTopTarget() {}
    void FlushPackedVertices() {}
};
void GfxRenderingAPICitro3D::StartFrame() {
''' + start_code + r'''
    mImpl->frameActive = true;
}
''' + wake_code + '\n' + end_code + r'''

void reset(int target, bool useIntermediatePresentation = true, unsigned bottomPhase = 100,
           uint32_t initialCounter = 100) {
    clockUnits = 10000;
    nextBlank = {10100, 10000 + bottomPhase};
    gpuReady = Never;
    targetFps = target;
''' + rate_code + r'''
    lcdCounter.fill(initialCounter / counterPeriod);
    pendingSwap.fill(0);
    lastSubmitted = unsafeSubmissions = consumedFrames = maxLcdGap = 0;
    previousConsume = 0;
    gapCounts.fill(0);
    gpuBottomTransfer = nextBottomTransfer = failNextBegin = false;
    gpuCost = 18;
    sPaceVblank = sPacePeriod = sFramesDropped = 0;
''' + reset_swap + r'''
}

bool present(GfxRenderingAPICitro3D& renderer, unsigned cpuCost, bool bottom = false) {
    renderer.StartFrame();
    if (!renderer.state.frameActive) return false;
    renderer.state.bottomDrawnThisFrame = bottom;
    if (bottom) C3D_FrameDrawOn(renderer.state.bottomTarget);
    advance(clockUnits + cpuCost);
    renderer.EndFrame();
    // These assignments lie after the diagnostic code excluded from EndFrame.
    renderer.state.frameActive = false;
    renderer.state.frameEndTick = clockUnits;
    return true;
}

struct Result { double tps, lcdFps, submitFps; unsigned skips, gap1, gap2, gap3, maxGap; };
Result runScenario(const char* name, int target, unsigned game, unsigned cpu, unsigned gpu,
                   bool variable = false, bool intermediate = true, bool wrap = false) {
    reset(target, intermediate, 100, wrap ? UINT32_MAX - 3 : 100);
    gpuCost = gpu;
    GfxRenderingAPICitro3D renderer;
    uint64_t measuredStart = 0;
    unsigned measuredSubmits = 0, measuredConsumes = 0, measuredDrops = 0;
    for (unsigned tick = 0; tick < 1040; ++tick) {
        if (tick == 40) {
            measuredStart = clockUnits;
            measuredSubmits = lastSubmitted;
            measuredConsumes = consumedFrames;
            measuredDrops = sFramesDropped;
            maxLcdGap = 0;
            gapCounts.fill(0);
        }
        advance(clockUnits + game);
        // Production's 20 Hz logic schedules 3, alternating 1/2, or 1 render.
        const unsigned count = target == 60 ? 3 : target == 30 ? 1 + tick % 2 : 1;
        for (unsigned frame = 0; frame < count; ++frame) {
            if (frame + 1 < count && Soh3dsFrameBehind()) {
                Soh3dsFrameDropped();
                continue;
            }
            check(present(renderer, variable && tick % 7 == 0 ? cpu * 2 : cpu), name);
        }
    }
    const double seconds = (clockUnits - measuredStart) / 6000.0;
    Result result{1000.0 / seconds, (consumedFrames - measuredConsumes) / seconds,
                  (lastSubmitted - measuredSubmits) / seconds, sFramesDropped - measuredDrops,
                  gapCounts[1], gapCounts[2], gapCounts[3], maxLcdGap};
    std::printf("%s: %.3f TPS, %.3f LCD FPS, %.3f submitted FPS, %u skips, "
                "gaps 1/2/3=%u/%u/%u, max=%u, unsafe submissions=%u\n", name,
                result.tps, result.lcdFps, result.submitFps, result.skips,
                result.gap1, result.gap2, result.gap3, result.maxGap, unsafeSubmissions);
    check(std::abs(result.tps - 20.0) < 0.5, "affordable game updates remain at 20 Hz");
    check(unsafeSubmissions == 0, "LCD consumes previous swap before next submission");
    return result;
}
void expectFps(const Result& result, double fps, const char* label) {
    check(std::abs(result.lcdFps - fps) < 0.1, label);
}

int main() {
    expectFps(runScenario("healthy 60", 60, 12, 24, 12), 60, "healthy 60 retains interpolation");
    runScenario("overloaded 60", 60, 60, 120, 36);
    runScenario("variable 60", 60, 36, 90, 24, true);
    expectFps(runScenario("healthy 30", 30, 24, 36, 18), 30, "healthy 30 throughput");
    runScenario("overloaded 30", 30, 60, 120, 36);
    expectFps(runScenario("native 20", 20, 60, 120, 36), 20, "native 20 throughput");

    const auto constant = runScenario("adequate constant 30", 30, 62, 132, 43);
    expectFps(constant, 30, "adequate constant load retains 30 LCD FPS");
    check(constant.gap1 == 0 && constant.gap3 == 0 && constant.maxGap == 2,
          "adequate constant 30 uses only two-vblank display intervals");
    for (unsigned gpu : {18u, 43u}) {
        const auto variable = runScenario("variable 30", 30, 42, 120, gpu, true);
        expectFps(variable, 30, "overlapping CPU preparation recovers variable 30 throughput");
        check(variable.skips == 0, "bounded variable 30 workload keeps optional frames");
        // Average throughput is not a lock: this workload has 50 ms intervals.
        check(variable.maxGap <= 3, "bounded variable 30 interval is at most three blanks");
    }
    runScenario("late GPU 60", 60, 12, 24, 120);
    expectFps(runScenario("non-intermediate 30", 30, 24, 36, 18, false, false), 30,
              "all display configurations retain a 60 Hz pacing clock");
    expectFps(runScenario("counter wrap 30", 30, 24, 36, 18, false, true, true), 30,
              "30 FPS pacing recovers normally across uint32 counter wrap");

    // The bottom clear participates in a swap although bottomDrawnLastFrame
    // becomes false. Offset blanks exercise that pending bottom presentation.
    reset(60, true, 20);
    gpuCost = 43;
    GfxRenderingAPICitro3D bottom;
    check(present(bottom, 6, true), "draw bottom page");
    check(present(bottom, 6), "clear bottom page");
    check(present(bottom, 6), "draw after bottom clear");
    check(bottom.state.sampleFrameSplitCount == 1, "bottom exit clears exactly once");
    check(unsafeSubmissions == 0, "bottom clear consumed despite top/bottom phase drift");

    reset(30);
    GfxRenderingAPICitro3D retry;
    check(present(retry, 24), "initial begin succeeds");
    const auto submitted = lastSubmitted;
    const auto pace = sPaceVblank;
    failNextBegin = true;
    check(!present(retry, 24), "failed FrameBegin leaves frame inactive");
    check(lastSubmitted == submitted && sPaceVblank == pace,
          "failed FrameBegin neither submits nor advances pacing slot");
    check(present(retry, 24), "FrameBegin retry succeeds");

    reset(60);
    sPacePeriod = 1;
    sPaceVblank = 102;
    advance(10400);
    check(Soh3dsFrameBehind(), "skip decision uses the live LCD clock");

    reset(60);
    GfxRenderingAPICitro3D stall;
    present(stall, 24);
    advance(clockUnits + 60000);
    present(stall, 24);
    check(sPaceVblank - C3D_FrameCounter(0) <= 2 && !Soh3dsFrameBehind(),
          "long load stall rebases without a catch-up burst");
    advance(clockUnits + 60000);
    Soh3dsGraphicsWake();
    check(sPaceVblank == 0 && sPacePeriod == 0, "wake discards old pacing debt");
    check(present(stall, 24) && !Soh3dsFrameBehind(), "presentation resumes after wake");
    check(unsafeSubmissions == 0, "wake keeps swap submission safe");

    if (failures) {
        std::fprintf(stderr, "pacing: %u failed checks\n", failures);
        return 1;
    }
    std::puts("pacing: game speed, LCD cadence, swap safety, retry, wrap and wake pass");
}
'''
with tempfile.TemporaryDirectory(prefix='soh-pacing-') as tmp:
    cpp = Path(tmp) / 'test.cpp'
    exe = Path(tmp) / 'test'
    cpp.write_text(code)
    flags = ['-fsanitize=address,undefined', '-fno-omit-frame-pointer', '-no-pie'] if os.environ.get('PACING_SANITIZE') else []
    subprocess.run(['c++', '-std=c++20', '-O1', '-g', *flags, str(cpp), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
