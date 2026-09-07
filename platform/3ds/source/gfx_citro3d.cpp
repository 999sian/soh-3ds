#include "gfx_citro3d.h"
#include "depth_snapshot_3ds.h"
#include "frame_trace_3ds.hpp"
#include "ship/utils/logging_3ds.h"
#include "fast/backends/gfx_profile_3ds.h"

#include <3ds.h>
#include <citro3d.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <malloc.h>
#include <cstring>
#include <deque>
#include <new>
#include <stdexcept>
#include <utility>
#include <vector>

#include "fast3d_passthrough_shbin.h"

extern "C" uint16_t Mk64Settings3DSGetResolutionWidth(void) __attribute__((weak));
extern "C" int Mk64Settings3DSGetAspectRatio(void) __attribute__((weak));
extern "C" uint8_t Mk64Settings3DSGetRenderScalePercent(void) __attribute__((weak));
extern "C" int Mk64Settings3DSGetDisplayFilter(void) __attribute__((weak));
// SoH-3DS: labels framebuffer dumps with the scene actually rendered. Weak +
// C linkage, at file scope: a block-scope `extern` would get C++ linkage and
// never bind to the extern "C" definition, which is exactly why an earlier
// capture reported scene=-1 for every frame.
extern "C" const char* Soh3dsCurrentSceneName(void) __attribute__((weak));
extern "C" bool Mk64Diagnostics3DSIsNewModel(void) __attribute__((weak));
// SoH-3DS: boot-status console provided by the port main (tests/soh_3ds_main.cpp).
extern "C" void Soh3dsBootConsoleReady(void) __attribute__((weak));
// SoH-3DS: hands the bottom LCD back from the boot console to the renderer.
extern "C" void Soh3dsBootConsoleRelease(void) __attribute__((weak));
extern "C" bool Mk64Diagnostics3DSSupportsWideMode(void) __attribute__((weak));
extern "C" bool Mk64Graphics3DSResolvedNewModel(void) __attribute__((weak));
extern "C" uint32_t Mk64Graphics3DSResolvedOutputWidth(void) __attribute__((weak));
extern "C" bool Mk64Graphics3DSUsesIntermediatePresentation(void) __attribute__((weak));


// Handler-entry telemetry from libultraship's ResourceLoader; weak so probe
// binaries that link this backend without libultraship stay linkable.
extern "C" unsigned Soh3dsResourceCatchCount(void) __attribute__((weak));
// Resource-cache entry count, for attributing long-session heap growth.
extern "C" unsigned Soh3dsResourceCacheSize(unsigned* unreferenced) __attribute__((weak));
extern "C" void Soh3dsResourceCacheReport(char* out, unsigned size) __attribute__((weak));
// Game-loop target frame rate from gfx_window_manager_3ds.cpp; weak for the
// same probe binaries.
extern "C" int Soh3dsTargetFps(void) __attribute__((weak));
extern "C" uint32_t Soh3dsGameTicks(void) __attribute__((weak));
// Per-tick phase ticks accumulated by graph.c (StartFrame, PadMgr, Update).
extern "C" uint64_t gSoh3dsTickPhaseTicks[3];
uint64_t gSoh3dsTickPhaseTicks[3] = { 0, 0, 0 };
// Detailed probes require perfprofile.flag as well as perftrace.flag.
// Basic capture leaves these hot-path counters and clock reads disabled.
// Detail mode samples hot calls at 1/16 or 1/64, coarse phases every call.
struct Soh3dsProfileCounter {
    uint64_t ticks = 0;
    uint32_t calls = 0;
    uint32_t samples = 0;
    uint32_t ordinal = 0;
};
static bool sRenderProfileEnabled = false;
static std::array<Soh3dsProfileCounter, static_cast<unsigned>(Soh3dsProfileSection::Count)> sRenderProfile;
struct Soh3dsPackCoverage {
    uint32_t totalBatches = 0, totalVertices = 0, commonBatches = 0, commonVertices = 0;
};
static Soh3dsPackCoverage sPackCoverage;
extern "C" void Soh3dsProfilePack(unsigned common, uint32_t vertices) {
    if (!sRenderProfileEnabled) return;
    ++sPackCoverage.totalBatches;
    sPackCoverage.totalVertices += vertices;
    if (common) {
        ++sPackCoverage.commonBatches;
        sPackCoverage.commonVertices += vertices;
    }
}
extern "C" uint64_t Soh3dsProfileBegin(unsigned section) {
    if (!sRenderProfileEnabled || section >= sRenderProfile.size()) return 0;
    auto& counter = sRenderProfile[section];
    ++counter.calls;
    const bool hot = (section >= static_cast<unsigned>(Soh3dsProfileSection::Draw) &&
                      section <= static_cast<unsigned>(Soh3dsProfileSection::State)) ||
                     section >= static_cast<unsigned>(Soh3dsProfileSection::TriangleState);
    const bool veryHot = section == static_cast<unsigned>(Soh3dsProfileSection::Vertex) ||
                         section == static_cast<unsigned>(Soh3dsProfileSection::Triangle) ||
                         section == static_cast<unsigned>(Soh3dsProfileSection::TriangleKey) ||
                         section == static_cast<unsigned>(Soh3dsProfileSection::TriangleEmit);
    const uint32_t sampleMask = veryHot ? 63u : hot ? 15u : 0u;
    if (sampleMask != 0 && (++counter.ordinal & sampleMask) != 0) return 0;
    ++counter.samples;
    return svcGetSystemTick();
}
extern "C" void Soh3dsProfileEnd(unsigned section, uint64_t start) {
    if (start != 0 && section < sRenderProfile.size()) {
        sRenderProfile[section].ticks += svcGetSystemTick() - start;
    }
}
static FILE* Soh3dsOpenPerformanceLog() {
    sRenderProfileEnabled = false;
    if (!Soh3dsLoggingEnabled(SOH3DS_LOG_GENERAL)) return nullptr;
    FILE* log = std::fopen("perf.csv", "ab");
    if (log == nullptr) return nullptr;
    sRenderProfileEnabled = Soh3dsLoggingEnabled(SOH3DS_LOG_PROFILE);
    return log;
}
static FILE* Soh3dsPerformanceLog() {
    // React to the menu without filesystem probes during ordinary frames.
    static FILE* log = nullptr;
    static bool wasEnabled = false;
    const bool enabled = Soh3dsLoggingEnabled(SOH3DS_LOG_GENERAL);
    if (enabled != wasEnabled) {
        if (log) std::fclose(log);
        log = enabled ? Soh3dsOpenPerformanceLog() : nullptr;
        wasEnabled = enabled;
    }
    sRenderProfileEnabled = log && Soh3dsLoggingEnabled(SOH3DS_LOG_PROFILE);
    return log;
}
// Frame pacing state (see EndFrame). RunCommands checks the current LCD
// clock before optional interpolated frames. A skip consumes its scheduled
// slot so the following game update can recover its original deadline.
constexpr uint32_t kMaxPacingDebtVblanks = 6; // 100 ms, two normal game ticks
static bool* sActiveFrame = nullptr;
static uint32_t sPaceVblank = 0;
static uint32_t sPacePeriod = 0;
static uint32_t sSwapReadyVblank = 0; // top callback fence after the previous GX queue drains
static uint32_t sFramesDropped = 0;
extern "C" bool Soh3dsFrameBehind(void) {
    // Game updates and interpolation happen after EndFrame. Consult the live
    // LCD clock so their cost can suppress an already overdue extra render.
    return sPaceVblank != 0 && static_cast<int32_t>(C3D_FrameCounter(0) - sPaceVblank) >= 0;
}
extern "C" void Soh3dsFrameDropped(void) {
    ++sFramesDropped;
    sPaceVblank += sPacePeriod;
}
extern "C" void Soh3dsSleepInit() __attribute__((weak));
extern "C" void Soh3dsSleepShutdown() __attribute__((weak));

namespace Fast {
namespace {

constexpr uint32_t kTopLogicalWidth = 400;
constexpr uint32_t kTopWideWidth = 800;
constexpr uint32_t kTopHeight = 240;
constexpr uint32_t kSceneBackingHeight = 256;
constexpr size_t kPostprocessVertexCapacity = 24;
constexpr uint16_t kCrtMaskSize = 8;
constexpr uint32_t kNativeWidth = 320;
constexpr uint32_t kNativeHeight = 240;
constexpr uint32_t kMaxBackingTextureSize = 512;
constexpr uint32_t kMaxSourceVertices = 256 * 3;
constexpr uint32_t kMaxDrawVertices = 256 * 6;
// Busy MK64 GP scenes exceeded that backend's original 32K budget, and it
// silently discarded every later batch (missing/corrupted sprites). Keep enough
// bounded linear memory for the worst class of scene and fail diagnostically
// (DrawTriangles throws, one logged dropped frame) if a scene exceeds it.
// SoH-3DS measurement 2026-09-03 (Azahar, f0e8b00e; Market, Dodongo's Cavern,
// Forest Temple, Ganon's Castle, Kakariko, shop + pause menu): OoT worst case
// was 7302 vertices/frame, so 64K (2.25 MiB) is 8.8x over-provisioned - but
// combat, boss fights and particle storms were not reachable on the harness.
// Right-size only after a hardware perf.csv `vtxPeak` from those; linear is
// not under pressure (7.3 MiB floor of 16 MiB), so this is margin, not need.
constexpr uint32_t kVertexBufferCapacity = 64 * 1024;
constexpr size_t kPresentedTimestampCapacity = 1024;
// SoH-3DS: 512, not MK64's 320.
//
// 320 was the largest dimension in the vanilla MK64 O2R texture set. OoT's
// asset set is wider: the title-screen strips arrive as 384x2, which the old
// cap rejected outright, leaving the previous contents of the texture slot to
// be sampled instead. 512 is the next power of two covering OoT's practical
// maximum and is within PICA's 1024 hardware limit.
//
// This feeds GetMaxTextureSize(), which libultraship uses to malloc a
// max*max*4 conversion buffer without checking the result
// (interpreter.cpp:4993) - 1 MiB here instead of 400 KiB. Measured: reverting
// to 320 left the late 32x32 uploads byte-identically corrupt, so this is not
// the source of that corruption.
constexpr uint32_t kMaxTextureSize = 512;
constexpr size_t kMaxVertexStrideFloats = 64;

struct PackedVertex {
    float position[4];
    float texcoord0[2];
    float texcoord1[2];
    uint8_t color[4];
};

static_assert(sizeof(PackedVertex) == 36, "Packed PICA vertex layout changed");

constexpr uint32_t ScaledDimension(uint32_t dimension, uint8_t percent) {
    return dimension * percent / 100U;
}

static_assert(ScaledDimension(400, 50) == 200);
static_assert(ScaledDimension(400, 75) == 300);
static_assert(ScaledDimension(400, 100) == 400);
static_assert(ScaledDimension(800, 50) == 400);
static_assert(ScaledDimension(800, 75) == 600);
static_assert(ScaledDimension(800, 100) == 800);
static_assert(ScaledDimension(240, 50) == 120);
static_assert(ScaledDimension(240, 75) == 180);
static_assert(ScaledDimension(240, 100) == 240);

enum DisplayFilter : int {
    DisplayFilterBilinear = 0,
    DisplayFilterBlur = 1,
    DisplayFilterCrt = 2,
};

uint8_t FloatColorToByte(float value) {
    if (!(value > 0.0f)) return 0;
    if (value >= 1.0f) return 255;
    return static_cast<uint8_t>(value * 255.0f + 0.5f);
}

#ifndef SOH3DS_EXPERIMENT_COMMON_PACK
// Rolled back after reported graphical issues. Use the original generic
// packer in normal builds; the experimental path is for explicit A/B tests.
#define SOH3DS_EXPERIMENT_COMMON_PACK 0
#endif

void PackGenericVertices(const float* drawVertices, PackedVertex* packedVertices, size_t vertexCount,
                         const ShaderProgram* program, int varyingInput, float coverScale,
                         const std::array<float, 2>& textureScaleU,
                         const std::array<float, 2>& textureScaleV,
                         const std::array<float, 2>& textureOffsetV,
                         const std::array<bool, 2>& rotatedFramebuffer) {
    for (size_t vertex = 0; vertex < vertexCount; ++vertex) {
        const float* source = drawVertices + vertex * program->strideFloats;
        PackedVertex& destination = packedVertices[vertex];
        destination.position[0] = source[0] * coverScale;
        destination.position[1] = source[1] * coverScale;
        destination.position[2] = source[2];
        destination.position[3] = source[3];

        for (int texture = 0; texture < 2; ++texture) {
            float u = 0.0f;
            float v = 0.0f;
            if (program->usedTextures[texture]) {
                const uint8_t textureOffset = program->textureOffsets[texture];
                u = source[textureOffset];
                v = source[textureOffset + 1];
                uint8_t clampOffset = textureOffset + 2;
                if (program->clamp[texture][0]) {
                    u = std::min(u, source[clampOffset++]);
                }
                if (program->clamp[texture][1]) {
                    v = std::min(v, source[clampOffset]);
                }
                if (rotatedFramebuffer[texture]) {
                    const float uprightU = std::clamp(u, 0.0f, 1.0f);
                    const float uprightV = std::clamp(v, 0.0f, 1.0f);
                    u = (1.0f - uprightV) * textureScaleU[texture];
                    v = 1.0f - textureOffsetV[texture] - uprightU * textureScaleV[texture];
                } else {
                    u *= textureScaleU[texture];
                    v *= textureScaleV[texture];
                }
            }
            float* texcoord = texture == 0 ? destination.texcoord0 : destination.texcoord1;
            texcoord[0] = u;
            texcoord[1] = v;
        }

        if (varyingInput >= 0) {
            const float* color = source + program->inputOffsets[varyingInput];
            destination.color[0] = FloatColorToByte(color[0]);
            destination.color[1] = FloatColorToByte(color[1]);
            destination.color[2] = FloatColorToByte(color[2]);
            destination.color[3] = program->alpha ? FloatColorToByte(color[3]) : 255;
        } else {
            destination.color[0] = 255;
            destination.color[1] = 255;
            destination.color[2] = 255;
            destination.color[3] = 255;
        }
        // Fog rides the vertex alpha: PICA has one interpolated colour, and
        // Fast3D forces the shade alpha to 1.0 whenever it emits fog, so the
        // channel is free (the alpha TEV side reads that 1.0 as a constant).
        if (program->fog) {
            destination.color[3] = FloatColorToByte(source[program->fogOffset + 3]);
        }
    }
}

// These eight layouts retain the float stream and run after clipping. The
// interpreter still expands color/UV attributes; only backend packing changes.
template <unsigned TextureMask, bool Alpha, bool Fog>
void PackCommonVertices(const float* source, PackedVertex* destination, size_t vertexCount,
                        float coverScale, const std::array<float, 2>& textureScaleU,
                        const std::array<float, 2>& textureScaleV) {
    static_assert(TextureMask == 1 || TextureMask == 3);
    constexpr unsigned textureEnd = TextureMask == 3 ? 8 : 6;
    constexpr unsigned colorOffset = textureEnd + (Fog ? 4 : 0);
    constexpr unsigned stride = colorOffset + (Alpha ? 4 : 3);
    for (size_t vertex = 0; vertex < vertexCount; ++vertex, source += stride, ++destination) {
        destination->position[0] = source[0] * coverScale;
        destination->position[1] = source[1] * coverScale;
        destination->position[2] = source[2];
        destination->position[3] = source[3];
        destination->texcoord0[0] = source[4] * textureScaleU[0];
        destination->texcoord0[1] = source[5] * textureScaleV[0];
        if constexpr (TextureMask == 3) {
            destination->texcoord1[0] = source[6] * textureScaleU[1];
            destination->texcoord1[1] = source[7] * textureScaleV[1];
        } else {
            destination->texcoord1[0] = 0.0f;
            destination->texcoord1[1] = 0.0f;
        }
        destination->color[0] = FloatColorToByte(source[colorOffset]);
        destination->color[1] = FloatColorToByte(source[colorOffset + 1]);
        destination->color[2] = FloatColorToByte(source[colorOffset + 2]);
        if constexpr (Fog) {
            destination->color[3] = FloatColorToByte(source[textureEnd + 3]);
        } else if constexpr (Alpha) {
            destination->color[3] = FloatColorToByte(source[colorOffset + 3]);
        } else {
            destination->color[3] = 255;
        }
    }
}

[[maybe_unused]] bool TryPackCommonVertices(
    const float* drawVertices, PackedVertex* packedVertices, size_t vertexCount,
    const ShaderProgram* program, int varyingInput, float coverScale,
    const std::array<float, 2>& textureScaleU, const std::array<float, 2>& textureScaleV,
    const std::array<bool, 2>& rotatedFramebuffer) {
    if (program->numInputs != 1 || varyingInput != 0 || program->grayscale ||
        !program->usedTextures[0] || program->clamp[0][0] || program->clamp[0][1] ||
        program->clamp[1][0] || program->clamp[1][1] || rotatedFramebuffer[0] ||
        (program->usedTextures[1] && rotatedFramebuffer[1])) {
        return false;
    }
    const unsigned textureEnd = program->usedTextures[1] ? 8 : 6;
    const unsigned colorOffset = textureEnd + (program->fog ? 4 : 0);
    if (program->textureOffsets[0] != 4 ||
        (program->usedTextures[1] && program->textureOffsets[1] != 6) ||
        (program->fog && program->fogOffset != textureEnd) ||
        program->inputOffsets[0] != colorOffset ||
        program->strideFloats != colorOffset + (program->alpha ? 4 : 3)) {
        return false;
    }
    // Select once per batch. Each instantiation has fixed attribute offsets,
    // stride and color/UV writes, with no per-vertex layout tests.
    const unsigned layout = (program->usedTextures[1] ? 4U : 0U) |
                            (program->alpha ? 2U : 0U) | (program->fog ? 1U : 0U);
    switch (layout) {
        case 0:
            PackCommonVertices<1, false, false>(drawVertices, packedVertices, vertexCount, coverScale,
                                                  textureScaleU, textureScaleV);
            break;
        case 1:
            PackCommonVertices<1, false, true>(drawVertices, packedVertices, vertexCount, coverScale,
                                                  textureScaleU, textureScaleV);
            break;
        case 2:
            PackCommonVertices<1, true, false>(drawVertices, packedVertices, vertexCount, coverScale,
                                                  textureScaleU, textureScaleV);
            break;
        case 3:
            PackCommonVertices<1, true, true>(drawVertices, packedVertices, vertexCount, coverScale,
                                                  textureScaleU, textureScaleV);
            break;
        case 4:
            PackCommonVertices<3, false, false>(drawVertices, packedVertices, vertexCount, coverScale,
                                                  textureScaleU, textureScaleV);
            break;
        case 5:
            PackCommonVertices<3, false, true>(drawVertices, packedVertices, vertexCount, coverScale,
                                                  textureScaleU, textureScaleV);
            break;
        case 6:
            PackCommonVertices<3, true, false>(drawVertices, packedVertices, vertexCount, coverScale,
                                                  textureScaleU, textureScaleV);
            break;
        case 7:
            PackCommonVertices<3, true, true>(drawVertices, packedVertices, vertexCount, coverScale,
                                                  textureScaleU, textureScaleV);
            break;
    }
    return true;
}

bool PackVertices(const float* drawVertices, PackedVertex* packedVertices, size_t vertexCount,
                  const ShaderProgram* program, int varyingInput, float coverScale,
                  const std::array<float, 2>& textureScaleU,
                  const std::array<float, 2>& textureScaleV,
                  const std::array<float, 2>& textureOffsetV,
                  const std::array<bool, 2>& rotatedFramebuffer) {
#if SOH3DS_EXPERIMENT_COMMON_PACK
    if (TryPackCommonVertices(drawVertices, packedVertices, vertexCount, program, varyingInput,
                              coverScale, textureScaleU, textureScaleV, rotatedFramebuffer)) {
        return true;
    }
#endif
    PackGenericVertices(drawVertices, packedVertices, vertexCount, program, varyingInput, coverScale,
                        textureScaleU, textureScaleV, textureOffsetV, rotatedFramebuffer);
    return false;
}

constexpr uint32_t kDisplayTransferFlags =
    GX_TRANSFER_FLIP_VERT(0) | GX_TRANSFER_OUT_TILED(0) | GX_TRANSFER_RAW_COPY(0) |
    GX_TRANSFER_IN_FORMAT(GX_TRANSFER_FMT_RGBA8) | GX_TRANSFER_OUT_FORMAT(GX_TRANSFER_FMT_RGB8) |
    GX_TRANSFER_SCALING(GX_TRANSFER_SCALE_NO);

enum CombinerSource : int {
    SourceZero = 0,
    SourceInput1 = 1,
    SourceInput7 = 7,
    SourceTexel0 = 8,
    SourceTexel0Alpha = 9,
    SourceTexel1 = 10,
    SourceTexel1Alpha = 11,
    SourceOne = 12,
    SourceCombined = 13,
    SourceNoise = 14,
    // SoH-3DS: a CPU-folded constant carried in ChannelPlan::folded.
    SourceFolded = 15,
};

enum ShaderOption : uint8_t {
    OptionAlpha = 0,
    OptionFog = 1,
    OptionTextureEdge = 2,
    OptionNoise = 3,
    OptionTwoCycle = 4,
    OptionAlphaThreshold = 5,
    OptionInvisible = 6,
    OptionGrayscale = 7,
    OptionTexel0ClampS = 8,
    OptionTexel0ClampT = 9,
    OptionTexel1ClampS = 10,
    OptionTexel1ClampT = 11,
};

constexpr bool HasOption(uint64_t options, ShaderOption option) {
    return (options & (uint64_t{1} << static_cast<uint8_t>(option))) != 0;
}

uint16_t NextPowerOfTwo(uint32_t value) {
    uint32_t result = 8;
    while (result < value && result < kMaxTextureSize) {
        result <<= 1;
    }
    return static_cast<uint16_t>(result);
}

uint8_t ToByte(float value) {
    // SoH-3DS: std::lround is a libcall plus a slow VFP float->int round on
    // ARM11; this runs per TEV constant per draw. Same rounding for in-range
    // values, saturating outside.
    if (!(value > 0.0f)) return 0;
    if (value >= 1.0f) return 255;
    return static_cast<uint8_t>(value * 255.0f + 0.5f);
}

// SoH-3DS telemetry: non-allocating saturating conversions for the perf record.
uint32_t PositiveTenths(float value) {
    return value > 0.0f ? static_cast<uint32_t>(value * 10.0f + 0.5f) : 0U;
}

uint32_t PositiveHundredths(float value) {
    return value > 0.0f ? static_cast<uint32_t>(value * 100.0f + 0.5f) : 0U;
}

constexpr uint32_t MortonOffset8x8(uint32_t x, uint32_t y) {
    return (x & 1U) | ((y & 1U) << 1U) | ((x & 2U) << 1U) | ((y & 2U) << 2U) |
           ((x & 4U) << 2U) | ((y & 4U) << 3U);
}

constexpr uint32_t SourceRowForBackingRow(uint32_t backingHeight, uint32_t sourceHeight,
                                           uint32_t destinationRow) {
    return (backingHeight - 1U - destinationRow) % sourceHeight;
}

static_assert(SourceRowForBackingRow(256, 240, 255) == 0);
static_assert(SourceRowForBackingRow(256, 240, 240) == 15);
static_assert(SourceRowForBackingRow(256, 240, 239) == 16);
static_assert(SourceRowForBackingRow(16, 12, 15) == 0);
static_assert(SourceRowForBackingRow(16, 12, 4) == 11);
// SoH-3DS: the tiling loop indexes sourceColumns/sourceRows by the POT backing
// size, and NextPowerOfTwo never exceeds kMaxTextureSize rounded up. Raising the
// source cap past the backing bound would overflow two stack arrays with no
// diagnostic, so tie the two constants together here instead of by comment.
static_assert(kMaxTextureSize <= kMaxBackingTextureSize,
              "source texture cap must fit the tiling loop's backing arrays");

// SoH-3DS: OoT's title-screen logo arrives as 384x2, which the 320 cap rejected
// outright. It becomes a 512x8 backing, so V=0 must still resolve to source row
// 0 from the final backing row, and the wrap must alternate over 2 source rows.
static_assert(SourceRowForBackingRow(8, 2, 7) == 0);
static_assert(SourceRowForBackingRow(8, 2, 6) == 1);
static_assert(SourceRowForBackingRow(8, 2, 0) == 1);

uint32_t PackColor(const std::array<float, 4>& color) {
    return static_cast<uint32_t>(ToByte(color[0])) | (static_cast<uint32_t>(ToByte(color[1])) << 8U) |
           (static_cast<uint32_t>(ToByte(color[2])) << 16U) |
           (static_cast<uint32_t>(ToByte(color[3])) << 24U);
}

// Clip a triangle against the near plane z = -w (the plane the RSP and every
// desktop GPU clip at). The old cut was w >= 1e-4 - the eye plane - which
// keeps fragments the N64 would have discarded and, worse, emits vertices with
// w ~ 1e-4 and x/y in the hundreds: a 1e7:1 ratio the PICA's 24-bit-float
// clipper/rasterizer cannot resolve, so large polygons (water planes) with a
// corner behind the camera dropped out on hardware depending on the camera.
size_t ClipTriangleAgainstW(const float* vertices[3], size_t stride, float* output) {
    // Every emitted component below is assigned before it is read. Avoid
    // clearing the full 1 KiB maximum scratch polygon for each clipped
    // triangle on the CPU hot path.
    std::array<std::array<float, kMaxVertexStrideFloats>, 4> polygon;
    int polygonCount = 0;

    for (int index = 0; index < 3; ++index) {
        const float* current = vertices[index];
        const float* next = vertices[(index + 1) % 3];
        const float currentDistance = current[2] + current[3];
        const float nextDistance = next[2] + next[3];
        const bool currentInside = currentDistance >= 0.0f;
        const bool nextInside = nextDistance >= 0.0f;
        if (currentInside) {
            std::copy_n(current, stride, polygon[polygonCount++].begin());
        }
        if (currentInside != nextInside) {
            const float amount = currentDistance / (currentDistance - nextDistance);
            auto& clipped = polygon[polygonCount++];
            for (size_t component = 0; component < stride; ++component) {
                clipped[component] = current[component] + (next[component] - current[component]) * amount;
            }
        }
    }

    size_t outputCount = 0;
    for (int index = 2; index < polygonCount; ++index) {
        std::copy_n(polygon[0].begin(), stride, output + outputCount++ * stride);
        std::copy_n(polygon[index - 1].begin(), stride, output + outputCount++ * stride);
        std::copy_n(polygon[index].begin(), stride, output + outputCount++ * stride);
    }
    return outputCount;
}

std::array<float, 4> ConstantForSource(int source, const std::array<std::array<float, 4>, 7>& inputs) {
    if (source >= SourceInput1 && source <= SourceInput7) {
        return inputs[source - SourceInput1];
    }
    if (source == SourceOne) {
        return { 1.0f, 1.0f, 1.0f, 1.0f };
    }
    if (source == SourceNoise) {
        return { 0.5f, 0.5f, 0.5f, 0.5f };
    }
    return { 0.0f, 0.0f, 0.0f, 0.0f };
}

bool IsConstantSource(int source, int varyingInput) {
    if (source >= SourceInput1 && source <= SourceInput7) {
        return source - SourceInput1 != varyingInput;
    }
    return source == SourceZero || source == SourceOne || source == SourceNoise || source == SourceFolded;
}

struct ChannelOperation {
    GPU_COMBINEFUNC function = GPU_REPLACE;
    std::array<int, 3> source = { SourceCombined, SourceZero, SourceZero };
    bool oneMinusSecond = false; // read source[1] as (1 - x)
};

struct ChannelPlan {
    std::array<ChannelOperation, 2> operations = {};
    std::array<float, 4> folded = {}; // value behind SourceFolded
    uint8_t count = 0;

    size_t size() const {
        return count;
    }

    const ChannelOperation& operator[](size_t index) const {
        return operations[index];
    }
};

ChannelPlan SingleOperationPlan(const ChannelOperation& operation) {
    ChannelPlan plan;
    plan.operations[0] = operation;
    plan.count = 1;
    return plan;
}

// SoH-3DS: counted per TEV rebuild for the perf heartbeat (`fold=`).
uint64_t gFoldedPlanCount = 0;

// Azahar splits fprintf output at every format argument: build the line first.
void LogFoldedPlan(const char* kind, const int formula[4]) {
    static unsigned sLogged = 0;
    if (sLogged < 16) {
        ++sLogged;
        char line[96];
        std::snprintf(line, sizeof(line), "soh-3ds tev: %s (%d - %d) * %d + %d\n", kind, formula[0], formula[1],
                      formula[2], formula[3]);
        std::fputs(line, stderr);
    }
}

// A PICA TEV stage reads ONE constant colour, and every op clamps to [0,1].
// The plan keeps each stage to one GPU_CONSTANT where a two-constant shape
// is common, and folds the general subtract on the CPU when its inputs are
// known for the draw.
ChannelPlan BuildChannelPlan(const int formula[4], int varyingInput,
                             const std::array<std::array<float, 4>, 7>& constants, bool alphaChannel) {
    const int a = formula[0];
    const int b = formula[1];
    const int c = formula[2];
    const int d = formula[3];
    const auto isConstant = [varyingInput](int source) { return IsConstantSource(source, varyingInput); };

    if (c == SourceZero) {
        return SingleOperationPlan({ GPU_REPLACE, { d, SourceZero, SourceZero } });
    }
    if (b == SourceZero && d == SourceZero) {
        return SingleOperationPlan({ GPU_MODULATE, { a, c, SourceZero } });
    }
    if (b == d) {
        // lerp(B, A, C) with two distinct constants A, B (HUD text is
        // (PRIM - ENV) * TEXEL0 + ENV; with a varying shade both are
        // constants): A*C, then + B*(1-C). Both terms lie in [0,1], exact.
        if (isConstant(a) && isConstant(b) && a != b && !isConstant(c)) {
            ChannelPlan plan;
            plan.operations[0] = { GPU_MODULATE, { a, c, SourceZero } };
            plan.operations[1] = { GPU_MULTIPLY_ADD, { b, c, SourceCombined }, true };
            plan.count = 2;
            ++gFoldedPlanCount;
            LogFoldedPlan(alphaChannel ? "split alpha" : "split rgb", formula);
            return plan;
        }
        return SingleOperationPlan({ GPU_INTERPOLATE, { a, b, c } });
    }
    if (b == SourceZero) {
        return SingleOperationPlan({ GPU_MULTIPLY_ADD, { a, c, d } });
    }

    ChannelPlan plan;
    plan.count = 2;
    // Subtract fold: SUBTRACT(A, B) clamps at 0, so the two-stage split is
    // too bright wherever A < B and D > 0. With B, C, D constant the cycle is
    // A*C + (D - B*C): MODULATE, then ADD a CPU-folded constant, exact when
    // that constant is non-negative on every channel this plan feeds.
    // ponytail: fold only for all-constant B, C, D; a varying B or C keeps the
    // clamped split (no OoT combiner found that needs it).
    if (isConstant(b) && isConstant(c) && isConstant(d)) {
        const auto bValue = ConstantForSource(b, constants);
        const auto cValue = ConstantForSource(c, constants);
        const auto dValue = ConstantForSource(d, constants);
        bool foldable = true;
        for (int channel = alphaChannel ? 3 : 0; channel < (alphaChannel ? 4 : 3); ++channel) {
            plan.folded[channel] = dValue[channel] - bValue[channel] * cValue[channel];
            foldable = foldable && plan.folded[channel] >= 0.0f;
        }
        if (foldable) {
            plan.operations[0] = { GPU_MODULATE, { a, c, SourceZero } };
            plan.operations[1] = { GPU_ADD, { SourceCombined, SourceFolded, SourceZero } };
            ++gFoldedPlanCount;
            LogFoldedPlan(alphaChannel ? "fold alpha" : "fold rgb", formula);
            return plan;
        }
    }
    plan.operations[0] = { GPU_SUBTRACT, { a, b, SourceZero } };
    plan.operations[1] = { GPU_MULTIPLY_ADD, { SourceCombined, c, d } };
    return plan;
}

// The one constant colour a stage reads: the first constant source of the
// operation, or the plan's CPU-folded value.
std::array<float, 4> StageConstant(const ChannelPlan& plan, const ChannelOperation& operation, int varyingInput,
                                   const std::array<std::array<float, 4>, 7>& constants) {
    for (int source : operation.source) {
        if (source == SourceFolded) {
            return plan.folded;
        }
        if (IsConstantSource(source, varyingInput)) {
            return ConstantForSource(source, constants);
        }
    }
    return { 0.0f, 0.0f, 0.0f, 0.0f };
}

ChannelOperation PassPrevious() {
    return { GPU_REPLACE, { SourceCombined, SourceZero, SourceZero } };
}

GPU_TEVSRC SourceToTev(int source, int varyingInput, int cycle) {
    if (source >= SourceInput1 && source <= SourceInput7) {
        return source - SourceInput1 == varyingInput ? GPU_PRIMARY_COLOR : GPU_CONSTANT;
    }
    switch (source) {
        case SourceTexel0:
        case SourceTexel0Alpha:
            // SoH-3DS: do NOT swap by cycle. The RDP hardware does alias
            // TEXEL0/TEXEL1 across the two cycles, but Fast3D already
            // normalises that when it packs the combiner, exactly as the
            // desktop GL/D3D backends assume - they map TEXEL0 to texture 0
            // unconditionally. Swapping here sent every two-cycle draw's
            // cycle-1 texel to unit 1, which holds no matching texture, so
            // the texel was dropped and the surface fell back to flat shade
            // colour. That is the green Link's House / yellow Kokiri shop:
            // room geometry is two-cycle, actors mostly are not. Proven by
            // probe: forcing stage 0 to TEXEL0 made 100% of the frame sample
            // the texture, so binding was never the problem.
            return GPU_TEXTURE0;
        case SourceTexel1:
        case SourceTexel1Alpha:
            return GPU_TEXTURE1;
        case SourceCombined:
            return GPU_PREVIOUS;
        default:
            return GPU_CONSTANT;
    }
}

GPU_TEVOP_RGB SourceToRgbOperand(int source, bool oneMinus) {
    const bool alpha = source == SourceTexel0Alpha || source == SourceTexel1Alpha;
    if (oneMinus) {
        return alpha ? GPU_TEVOP_RGB_ONE_MINUS_SRC_ALPHA : GPU_TEVOP_RGB_ONE_MINUS_SRC_COLOR;
    }
    return alpha ? GPU_TEVOP_RGB_SRC_ALPHA : GPU_TEVOP_RGB_SRC_COLOR;
}

void ConfigureChannel(C3D_TexEnv* environment, C3D_TexEnvMode mode, const ChannelOperation& operation,
                      int varyingInput, int cycle) {
    C3D_TexEnvSrc(environment, mode, SourceToTev(operation.source[0], varyingInput, cycle),
                  SourceToTev(operation.source[1], varyingInput, cycle),
                  SourceToTev(operation.source[2], varyingInput, cycle));
    C3D_TexEnvFunc(environment, mode, operation.function);
}

void ConfigureOperands(C3D_TexEnv* environment, const ChannelOperation& rgbOperation,
                       const ChannelOperation& alphaOperation) {
    C3D_TexEnvOpRgb(environment, SourceToRgbOperand(rgbOperation.source[0], false),
                    SourceToRgbOperand(rgbOperation.source[1], rgbOperation.oneMinusSecond),
                    SourceToRgbOperand(rgbOperation.source[2], false));
    C3D_TexEnvOpAlpha(environment, GPU_TEVOP_A_SRC_ALPHA,
                      alphaOperation.oneMinusSecond ? GPU_TEVOP_A_ONE_MINUS_SRC_ALPHA : GPU_TEVOP_A_SRC_ALPHA,
                      GPU_TEVOP_A_SRC_ALPHA);
}

void ConfigureAlphaPass(C3D_TexEnv* environment) {
    C3D_TexEnvSrc(environment, C3D_Alpha, GPU_PREVIOUS);
    C3D_TexEnvOpAlpha(environment, GPU_TEVOP_A_SRC_ALPHA);
    C3D_TexEnvFunc(environment, C3D_Alpha, GPU_REPLACE);
}

} // namespace

struct GfxRenderingAPICitro3D::Impl {
    struct TextureSlot {
        C3D_Tex texture = {};
        bool initialized = false;
        bool hasTransparency = false;
        uint16_t sourceWidth = 0;
        uint16_t sourceHeight = 0;
        size_t allocatedBytes = 0;
        // Frame ordinal of the last recorded draw that samples this slot's
        // current storage; used to detect same-frame content replacement.
        uint32_t lastDrawnFrame = 0;
    };

    // Offscreen framebuffer. Storage is allocated lazily on first use, because
    // OoT creates five of these at boot (4 MiB colour+depth in a 6 MiB VRAM if
    // allocated eagerly) and most are rarely or never touched. Everything drawn
    // to or copied from a non-scene target is ROTATED in memory (SetViewport
    // and the projection tilt treat every such target like the top screen), so
    // the texture backing is (POT(contentHeight) x POT(contentWidth)) and the
    // vertex packer rotates UVs when sampling it.
    struct FramebufferSlot {
        C3D_Tex texture = {};
        C3D_RenderTarget* target = nullptr; // only once something draws into it
        bool initialized = false;           // texture storage exists
        bool rotated = false;
        bool wantsDepth = true;
        bool needsClear = false;
        uint16_t logicalWidth = 0;  // requested by UpdateFramebufferParameters
        uint16_t logicalHeight = 0;
        uint16_t contentWidth = 0;  // upright pixel size of what the storage holds
        uint16_t contentHeight = 0;
        uint16_t contentOffsetY = 0; // leading memory rows before rotated rendered content
        DepthSnapshot3DS depthSnapshot;
    };

    Impl() {
        framebuffers.emplace_back(); // ID 0 is the top-screen render target.
        // SoH-3DS: ID 0 means "no texture" to UploadTexture/DeleteTexture and to
        // SelectTextureFb, which parks selectedTextures at 0. NewTexture used to
        // hand out 0 as the first real id, so the first texture the game cached
        // could never be uploaded.
        textures.emplace_back();
    }

    bool initialized = false;
    bool ready = false;
    bool frameActive = false;
    bool newModel = false;
    uint32_t outputWidth = kTopLogicalWidth;
    bool originalAspect = false;
    bool useAlpha = false;
    bool depthTest = false;
    bool depthWrite = true;
    bool decal = false;
    FilteringMode filteringMode = FILTER_THREE_POINT;
    DVLB_s* shaderBinary = nullptr;
    shaderProgram_s shaderProgram = {};
    int projectionUniform = -1;
    C3D_RenderTarget* topTarget = nullptr;
    // Dual screen: the bottom LCD as a render target. A Fast3D framebuffer id
    // registered via Soh3dsSetBottomScreenFramebuffer() draws straight into
    // it (no texture, no blit); the game brackets kaleido / the minimap with
    // gsSPSetFB(that id). Cleared on first use each frame; a target that is
    // not drawn on is not presented, so the frame after the last use is
    // drawn-on + cleared once (otherwise the LCD keeps the stale image).
    C3D_RenderTarget* bottomTarget = nullptr;
    int bottomScreenFb = -1;
    int bottomScreenZoomFb = -1; // same LCD, draws get bottomZoom applied
    bool bottomZoomActive = false;
    bool bottomDrawnThisFrame = false;
    struct BottomZoom {
        float scale = 1.0f;
        float centerX = 0.0f; // NDC of the point that lands at the bottom screen's centre
        float centerY = 0.0f;
    } bottomZoom;
    bool bottomDrawnLastFrame = false;
    bool bottomConsoleReleased = false;
    C3D_Tex sceneTexture = {};
    C3D_RenderTarget* sceneTarget = nullptr;
    bool sceneTextureInitialized = false;
    C3D_Tex crtMaskTexture = {};
    bool crtMaskTextureInitialized = false;
    PackedVertex* postprocessVertices = nullptr;
    C3D_RenderTarget* gameTarget = nullptr;
    C3D_RenderTarget* activeTarget = nullptr;
    uint32_t renderWidth = kTopLogicalWidth;
    uint32_t renderHeight = kTopHeight;
    uint8_t renderScalePercent = 100;
    int displayFilter = DisplayFilterBilinear;
    bool postprocessActive = false;
    bool scenePresented = false;
    PackedVertex* packedVertices = nullptr;
    size_t packedVertexCount = 0;
    size_t dirtyVertexBegin = 0;
    size_t dirtyVertexEnd = 0;
    std::vector<float> clipScratch;
    ShaderProgram* currentProgram = nullptr;
    bool tevStateValid = false;
    ShaderProgram* tevStateProgram = nullptr;
    int tevStateVaryingInput = -2;
    std::array<uint32_t, 7> tevStateConstants = {};
    uint32_t tevStateGrayscale = 0;
    uint32_t tevStateFog = 0;
    bool tevStateDepthTest = false;
    bool tevStateDepthWrite = false;
    std::unordered_map<uint64_t, std::unique_ptr<ShaderProgram>> shaderPrograms;
    // Citro3D retains C3D_Tex pointers after C3D_TexBind. A vector can move
    // every slot when NewTexture grows it, leaving the GPU state pointing at
    // freed storage. deque keeps existing slot addresses stable while the
    // Fast3D cache creates textures incrementally.
    std::deque<TextureSlot> textures = { TextureSlot{} };
    // C3D_DrawArrays only records commands; the GPU reads texture memory when
    // the frame executes. Storage replaced mid-frame is parked here and freed
    // once the next C3D_FrameBegin has synced on that execution.
    std::vector<C3D_Tex> retiredTextures;
    // SoH-3DS: framebuffer storage release requested while a frame is being
    // recorded. C3D_RenderTargetDelete svcBreak-panics when called in-frame,
    // and deleting a texture the recorded commands still reference is a GPU
    // use-after-free, so the release waits for EndFrame; the next use then
    // reallocates storage with the current parameters.
    std::vector<int> pendingFramebufferReleases;
    uint32_t frameOrdinal = 0;
    std::vector<std::unique_ptr<FramebufferSlot>> framebuffers;
    std::array<uint32_t, 6> selectedTextures = {};
    std::array<int, 6> selectedFramebuffers = {};
    // Track actual hardware bindings, including fallback/postprocess aliases.
    std::array<C3D_Tex*, 3> boundTextures = {};
    void BindTexture(int unit, C3D_Tex* texture) {
        if (unit < 0 || unit >= static_cast<int>(boundTextures.size())) return;
        boundTextures[unit] = texture;
        C3D_TexBind(unit, texture);
    }
    void RebindTexture(C3D_Tex* texture) {
        for (int unit = 0; unit < static_cast<int>(boundTextures.size()); ++unit) {
            if (boundTextures[unit] == texture) BindTexture(unit, texture);
        }
    }
    int selectedTextureUnit = 0;
    int viewportX = 0;
    int viewportY = 0;
    int viewportWidth = static_cast<int>(kTopLogicalWidth);
    int viewportHeight = static_cast<int>(kTopHeight);
    int scissorX = 0;
    int scissorY = 0;
    int scissorWidth = static_cast<int>(kTopLogicalWidth);
    int scissorHeight = static_cast<int>(kTopHeight);
    bool scissorEnabled = false;
    bool externalLinearBuffersDirty = false;
    std::array<uint64_t, kPresentedTimestampCapacity> presentedTimestamps = {};
    size_t presentedTimestampHead = 0;
    size_t presentedTimestampCount = 0;
    uint64_t firstPresentedTimestamp = 0;
    uint64_t drawCallCount = 0;
    uint64_t triangleCount = 0;
    // Main-thread ticks spent between StartFrame and EndFrame (interpreter +
    // backend work), the number Tier-1 CPU optimizations move.
    uint64_t frameStartTick = 0;
    uint64_t busyTickAccumulator = 0;
    uint64_t waitTickAccumulator = 0;
    // Main-thread ticks from EndFrame to the next StartFrame, before its
    // pacing wait: game tick, interpolation and everything else outside the
    // renderer. Diagnostic dump/report IO after submission is outside both
    // CPU scopes; waitTickAccumulator reports swap/GX/pacing waits separately.
    uint64_t frameEndTick = 0;
    uint64_t loopTickAccumulator = 0;
    uint64_t frameLoopTicks = 0;
    uint64_t frameBeginWaitTicks = 0;
    // Snapshot is allocated only after this target is queried. Storage is
    // reused; framebuffer snapshots are released with their GPU storage.
    DepthSnapshot3DS depthSnapshot;
    uint64_t textureCacheUploadCount = 0;
    uint64_t textureCacheUploadBytes = 0;
    uint64_t vertexUploadCount = 0;
    uint64_t vertexUploadBytes = 0;
    uint64_t linearHeapFlushFrameCount = 0;
    // SoH-3DS telemetry: per-sample deltas and peaks for the 60-frame perf
    // record. Lifetime counters above stay monotonic; these track the window.
    uint64_t sampleFrameSplitCount = 0;
    uint64_t sampleFogDrawCount = 0;
    uint64_t sampleDepthQueryCount = 0;
    uint64_t lastSampleDrawCallCount = 0;
    uint64_t lastSampleTriangleCount = 0;
    uint64_t lastSampleTextureUploadCount = 0;
    uint64_t lastSampleTextureUploadBytes = 0;
    size_t framePeakPackedVertices = 0;
    size_t samplePeakPackedVertices = 0;

    ~Impl() {
        if (Soh3dsSleepShutdown) Soh3dsSleepShutdown();
        sActiveFrame = nullptr;
        for (auto& slot : framebuffers) {
            if (slot == nullptr) {
                continue;
            }
            if (slot->target != nullptr) {
                C3D_RenderTargetDelete(slot->target);
            }
            if (slot->initialized) {
                C3D_TexDelete(&slot->texture);
            }
        }
        for (auto& slot : textures) {
            if (slot.initialized) {
                C3D_TexDelete(&slot.texture);
            }
        }
        for (auto& texture : retiredTextures) {
            C3D_TexDelete(&texture);
        }
        if (packedVertices != nullptr) {
            linearFree(packedVertices);
        }
        if (postprocessVertices != nullptr) {
            linearFree(postprocessVertices);
        }
        if (sceneTarget != nullptr) {
            C3D_RenderTargetDelete(sceneTarget);
        }
        if (sceneTextureInitialized) {
            C3D_TexDelete(&sceneTexture);
        }
        if (crtMaskTextureInitialized) {
            C3D_TexDelete(&crtMaskTexture);
        }
        if (shaderBinary != nullptr) {
            shaderProgramFree(&shaderProgram);
            DVLB_Free(shaderBinary);
        }
        if (initialized) {
            if (topTarget != nullptr) {
                C3D_RenderTargetDelete(topTarget);
            }
            if (bottomTarget != nullptr) {
                C3D_RenderTargetDelete(bottomTarget);
            }
            C3D_Fini();
            gfxExit();
        }
    }
};

namespace {

// Fast3D's fragment-side alpha contract (default.shader.glsl:263-280):
//   texture_edge (CVG_X_ALPHA, no blend selected): pass iff a > 0.19, then
//     force a = 1.0 - an opaque cut-out even though use_alpha was set.
//   alpha_threshold (CVG_X_ALPHA with blend, or G_AC_THRESHOLD): discard
//     a < 8/256, alpha kept for blending.
// PICA compares int(a*255) OP ref on the final TEV alpha, so the edge case
// needs ref 0x30 and an opaque blend (the alpha itself cannot be forced to 1
// before the test). Both are gated on the program having an alpha channel:
// without one the TEV alpha at stage 0 is undefined leftover state, and the
// test discarded whole prerendered room backgrounds.
void ApplyAlphaTest(const ShaderProgram* program) {
    if (program == nullptr || !program->alpha || !(program->textureEdge || program->alphaThreshold)) {
        C3D_AlphaTest(false, GPU_ALWAYS, 0);
    } else if (program->textureEdge) {
        C3D_AlphaTest(true, GPU_GREATER, 0x30);
    } else {
        C3D_AlphaTest(true, GPU_GEQUAL, 0x08);
    }
}

void ApplyAlphaBlend(bool useAlpha, const ShaderProgram* program) {
    const bool opaqueCutout = program != nullptr && program->alpha && program->textureEdge;
    const bool blend = useAlpha && !opaqueCutout;
    C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, blend ? GPU_SRC_ALPHA : GPU_ONE,
                   blend ? GPU_ONE_MINUS_SRC_ALPHA : GPU_ZERO, GPU_ONE, GPU_ONE_MINUS_SRC_ALPHA);
}

} // namespace

GfxRenderingAPICitro3D::GfxRenderingAPICitro3D() : mImpl(std::make_unique<Impl>()) {
    sActiveFrame = &mImpl->frameActive;
}

GfxRenderingAPICitro3D::~GfxRenderingAPICitro3D() = default;

bool GfxRenderingAPICitro3D::IsInitialized() const {
    return mImpl->ready;
}

const char* GfxRenderingAPICitro3D::GetName() {
    return "Citro3D";
}

int GfxRenderingAPICitro3D::GetMaxTextureSize() {
    return kMaxTextureSize;
}

GfxClipParameters GfxRenderingAPICitro3D::GetClipParameters() {
    // Fast3D emits OpenGL-style clip Z in [-w, w]. StartFrame's projection
    // matrix performs the sole conversion to PICA's [-w, 0] range.
    return { false, false };
}

void GfxRenderingAPICitro3D::UnloadShader(ShaderProgram* oldPrg) {
    (void) oldPrg;
}

void GfxRenderingAPICitro3D::LoadShader(ShaderProgram* newPrg) {
    mImpl->currentProgram = newPrg;
    // The blend factors depend on the program (texture_edge cut-outs are
    // opaque), and SetUseAlpha may not be called again before the next draw.
    ApplyAlphaBlend(mImpl->useAlpha, newPrg);
}

void GfxRenderingAPICitro3D::ClearShaderCache() {
    mImpl->currentProgram = nullptr;
    mImpl->shaderPrograms.clear();
}

ShaderProgram* GfxRenderingAPICitro3D::CreateAndLoadNewShader(uint64_t shaderId0, uint64_t shaderId1) {
    const uint64_t key = shaderId0 ^ (shaderId1 + 0x9E3779B97F4A7C15ULL + (shaderId0 << 6U) + (shaderId0 >> 2U));
    auto program = std::make_unique<ShaderProgram>();
    program->shaderId0 = shaderId0;
    program->shaderId1 = shaderId1;
    program->alpha = HasOption(shaderId1, OptionAlpha);
    program->fog = HasOption(shaderId1, OptionFog);
    program->grayscale = HasOption(shaderId1, OptionGrayscale);
    program->textureEdge = HasOption(shaderId1, OptionTextureEdge);
    program->alphaThreshold = HasOption(shaderId1, OptionAlphaThreshold);
    program->invisible = HasOption(shaderId1, OptionInvisible);
    program->twoCycle = HasOption(shaderId1, OptionTwoCycle);
    program->clamp[0][0] = HasOption(shaderId1, OptionTexel0ClampS);
    program->clamp[0][1] = HasOption(shaderId1, OptionTexel0ClampT);
    program->clamp[1][0] = HasOption(shaderId1, OptionTexel1ClampS);
    program->clamp[1][1] = HasOption(shaderId1, OptionTexel1ClampT);

    for (int cycle = 0; cycle < 2; ++cycle) {
        for (int channel = 0; channel < 2; ++channel) {
            for (int term = 0; term < 4; ++term) {
                program->combiner[cycle][channel][term] =
                    static_cast<int>((shaderId0 >> (cycle * 32 + channel * 16 + term * 4)) & 0xFULL);
                const int source = program->combiner[cycle][channel][term];
                if (source >= SourceInput1 && source <= SourceInput7) {
                    program->numInputs = std::max(program->numInputs, static_cast<uint8_t>(source));
                } else if (source == SourceTexel0 || source == SourceTexel0Alpha ||
                           source == SourceTexel1 || source == SourceTexel1Alpha) {
                    const int texture =
                        (source == SourceTexel0 || source == SourceTexel0Alpha) ? 0 : 1;
                    program->usedTextures[texture] = true;
                    // Fast3D supplies both coordinate sets for two-cycle
                    // programs because the RDP swaps TEXEL0/TEXEL1 between
                    // cycles. Keep ShaderGetInfo's vertex layout identical to
                    // the desktop backends even if only one token appears in
                    // the packed combiner.
                    if (program->twoCycle) {
                        program->usedTextures[texture ^ 1] = true;
                    }
                }
            }
        }
    }

    uint8_t offset = 4;
    for (int texture = 0; texture < 2; ++texture) {
        if (!program->usedTextures[texture]) {
            continue;
        }
        program->textureOffsets[texture] = offset;
        offset += 2;
        offset += program->clamp[texture][0] ? 1 : 0;
        offset += program->clamp[texture][1] ? 1 : 0;
    }
    if (program->fog) {
        program->fogOffset = offset;
        offset += 4;
    }
    if (program->grayscale) {
        program->grayscaleOffset = offset;
        offset += 4;
    }
    for (uint8_t input = 0; input < program->numInputs; ++input) {
        program->inputOffsets[input] = offset;
        offset += program->alpha ? 4 : 3;
    }
    program->strideFloats = offset;

    ShaderProgram* result = program.get();
    mImpl->shaderPrograms[key] = std::move(program);
    LoadShader(result);
    return result;
}

ShaderProgram* GfxRenderingAPICitro3D::LookupShader(uint64_t shaderId0, uint64_t shaderId1) {
    const uint64_t key = shaderId0 ^ (shaderId1 + 0x9E3779B97F4A7C15ULL + (shaderId0 << 6U) + (shaderId0 >> 2U));
    const auto iterator = mImpl->shaderPrograms.find(key);
    if (iterator == mImpl->shaderPrograms.end()) {
        return nullptr;
    }
    const ShaderProgram* candidate = iterator->second.get();
    return candidate->shaderId0 == shaderId0 && candidate->shaderId1 == shaderId1 ? iterator->second.get() : nullptr;
}

void GfxRenderingAPICitro3D::ShaderGetInfo(ShaderProgram* prg, uint8_t* numInputs, bool usedTextures[2]) {
    *numInputs = prg->numInputs;
    usedTextures[0] = prg->usedTextures[0];
    usedTextures[1] = prg->usedTextures[1];
}

uint32_t GfxRenderingAPICitro3D::NewTexture() {
    mImpl->textures.emplace_back();
    return static_cast<uint32_t>(mImpl->textures.size() - 1);
}

void GfxRenderingAPICitro3D::SelectTexture(int tile, uint32_t textureId) {
    if (tile < 0 || tile >= static_cast<int>(mImpl->selectedTextures.size()) || textureId >= mImpl->textures.size()) {
        return;
    }
    mImpl->selectedTextureUnit = tile;
    mImpl->selectedTextures[tile] = textureId;
    mImpl->selectedFramebuffers[tile] = 0;
    auto& slot = mImpl->textures[textureId];
    if (slot.initialized && tile < 3) {
        mImpl->BindTexture(tile, &slot.texture);
    }
}

void GfxRenderingAPICitro3D::UploadTexture(const uint8_t* rgba32Buf, uint32_t width, uint32_t height) {
    // SoH-3DS: a silent return here leaves the previous contents in the texture
    // slot, so the draw samples an unrelated image - garbled 2D rather than a
    // missing one. MK64's asset set never exceeded kMaxTextureSize; SoH's may.
    if (rgba32Buf != nullptr && (width > kMaxTextureSize || height > kMaxTextureSize)) {
        static uint32_t sMaxW = 0, sMaxH = 0, sRejects = 0;
        ++sRejects;
        if (width > sMaxW || height > sMaxH) {
            sMaxW = width > sMaxW ? width : sMaxW;
            sMaxH = height > sMaxH ? height : sMaxH;
            std::fprintf(stderr, "soh-3ds gfx: UPLOAD REJECT %lux%lu (max %lu, rejects=%lu)\n",
                         (unsigned long)width, (unsigned long)height,
                         (unsigned long)kMaxTextureSize, (unsigned long)sRejects);
        }
    }
    if (rgba32Buf == nullptr || width == 0 || height == 0 || width > kMaxTextureSize || height > kMaxTextureSize) {
        return;
    }

    const uint32_t textureId = mImpl->selectedTextures[mImpl->selectedTextureUnit];
    if (textureId == 0 || textureId >= mImpl->textures.size()) {
        return;
    }

    const uint16_t textureWidth = NextPowerOfTwo(width);
    const uint16_t textureHeight = NextPowerOfTwo(height);
    auto& slot = mImpl->textures[textureId];
    // Fast3D recycles LRU texture IDs heavily for animated sprites. Keep a
    // matching RGBA8 backing allocation and overwrite it in place instead of
    // churning the linear allocator on every upload - EXCEPT when a draw
    // recorded this frame still references the current contents. The GPU only
    // reads texture memory when the frame's command list executes, so an
    // in-place overwrite would retroactively repaint every earlier rect with
    // the new image (the title screen drew one glyph for all of PRESS START
    // this way). Give the new image fresh storage and retire the old one until
    // the next frame sync.
    const bool referencedThisFrame =
        slot.initialized && mImpl->frameActive && slot.lastDrawnFrame == mImpl->frameOrdinal;
    const bool canReuseAllocation = !referencedThisFrame && slot.initialized && slot.texture.data != nullptr &&
                                    slot.texture.width == textureWidth && slot.texture.height == textureHeight &&
                                    slot.texture.fmt == GPU_RGBA8;
    if (!canReuseAllocation) {
        if (slot.initialized) {
            if (referencedThisFrame) {
                mImpl->retiredTextures.push_back(slot.texture);
            } else {
                C3D_TexDelete(&slot.texture);
            }
            slot.initialized = false;
            slot.allocatedBytes = 0;
        }
        if (!C3D_TexInit(&slot.texture, textureWidth, textureHeight, GPU_RGBA8)) {
            // SoH-3DS: this used to throw bad_alloc, which the window layer turns
            // into a dropped frame - and since the interpreter had already
            // inserted the cache entry, the texture stayed empty until the LRU
            // recycled it (invisible text boxes, flat prerendered backgrounds
            // once linear memory was exhausted). Leave the slot without storage:
            // draws that need it are skipped, TextureHasStorage() reports the
            // loss and the interpreter re-imports it once memory is available.
            static uint32_t sFailures = 0;
            if (++sFailures <= 8 || (sFailures & 255u) == 0) {
                std::fprintf(stderr, "soh-3ds gfx: TexInit %ux%u FAILED (linear free %lu KiB, failures=%lu)\n",
                             (unsigned)textureWidth, (unsigned)textureHeight,
                             (unsigned long)(linearSpaceFree() / 1024u), (unsigned long)sFailures);
            }
            return;
        }
        slot.initialized = true;
        slot.allocatedBytes = static_cast<size_t>(textureWidth) * textureHeight * 4U;
    }
    slot.hasTransparency = false;
    slot.sourceWidth = static_cast<uint16_t>(width);
    slot.sourceHeight = static_cast<uint16_t>(height);

    // Fast3D supplies RGBA bytes while PICA stores GPU_RGBA8 texels in tiled
    // A-B-G-R byte order. Swap each host word while building the Morton layout;
    // otherwise red is interpreted as alpha. Direct swizzling also avoids an
    // extra transfer and allocation.
    // Fill POT padding by wrapping so filtering cannot sample transparent
    // border texels. C3D_TexInit already provides writable linear memory, so
    // write its Morton layout directly and avoid a second allocation, cache
    // flush, transfer, and free for every texture upload.
    auto* texturePixels = static_cast<uint32_t*>(slot.texture.data);
    const uint32_t tilesPerRow = textureWidth / 8U;
    std::array<uint16_t, kMaxBackingTextureSize> sourceColumns;
    std::array<uint16_t, kMaxBackingTextureSize> sourceRows;
    uint32_t sourceColumn = 0;
    for (uint32_t destinationColumn = 0; destinationColumn < textureWidth; ++destinationColumn) {
        sourceColumns[destinationColumn] = static_cast<uint16_t>(sourceColumn);
        if (++sourceColumn == width) {
            sourceColumn = 0;
        }
    }
    uint32_t sourceRow = SourceRowForBackingRow(textureHeight, height, 0);
    for (uint32_t destinationRow = 0; destinationRow < textureHeight; ++destinationRow) {
        sourceRows[destinationRow] = static_cast<uint16_t>(sourceRow);
        sourceRow = sourceRow == 0 ? height - 1U : sourceRow - 1U;
    }
    uint8_t combinedAlpha = 0xFF;
    for (uint32_t tileY = 0; tileY < textureHeight; tileY += 8U) {
        for (uint32_t tileX = 0; tileX < textureWidth; tileX += 8U) {
            uint32_t* tile = texturePixels +
                (static_cast<size_t>(tileY / 8U) * tilesPerRow + tileX / 8U) * 64U;
            for (uint32_t row = 0; row < 8U; ++row) {
                // PICA samples V=0 from the final row of the POT backing while
                // Fast3D supplies rows top-down and scales V by logical/POT
                // height. Anchor the flip to the backing height: for a 240-row
                // image in a 256-row texture, backing row 255 must contain
                // source row 0. Flipping around 240 instead starts at source
                // row 224 and visibly wraps after the first 16 screen rows.
                const uint32_t sourceRow = sourceRows[tileY + row];
                for (uint32_t column = 0; column < 8U; ++column) {
                    const uint32_t sourceColumn = sourceColumns[tileX + column];
                    uint32_t pixel = 0;
                    const uint8_t* sourcePixel =
                        rgba32Buf + (static_cast<size_t>(sourceRow) * width + sourceColumn) * 4;
                    combinedAlpha &= sourcePixel[3];
                    std::memcpy(&pixel, sourcePixel, sizeof(pixel));
                    tile[MortonOffset8x8(column, row)] = __builtin_bswap32(pixel);
                }
            }
        }
    }
    slot.hasTransparency = combinedAlpha != 0xFF;

    C3D_TexFlush(&slot.texture);
    ++mImpl->textureCacheUploadCount;
    mImpl->textureCacheUploadBytes += slot.allocatedBytes;
    // TextureCacheValue is value-initialized with linear_filter=false on a
    // miss (and reset to that state when an LRU node is recycled). Keep the
    // uploaded texture descriptor consistent so point-filtered draws are not
    // skipped by the interpreter's sampler-state cache.
    C3D_TexSetFilter(&slot.texture, GPU_NEAREST, GPU_NEAREST);
    C3D_TexSetWrap(&slot.texture, GPU_REPEAT, GPU_REPEAT);
    mImpl->BindTexture(mImpl->selectedTextureUnit, &slot.texture);
}

void GfxRenderingAPICitro3D::SetSamplerParameters(int sampler, bool linearFilter, uint32_t cms, uint32_t cmt) {
    if (sampler < 0 || sampler >= static_cast<int>(mImpl->selectedTextures.size())) {
        return;
    }
    const int framebufferId = mImpl->selectedFramebuffers[sampler];
    if (framebufferId > 0 && framebufferId < static_cast<int>(mImpl->framebuffers.size()) &&
        mImpl->framebuffers[framebufferId] != nullptr && mImpl->framebuffers[framebufferId]->initialized) {
        C3D_Tex& texture = mImpl->framebuffers[framebufferId]->texture;
        const GPU_TEXTURE_FILTER_PARAM filter = linearFilter ? GPU_LINEAR : GPU_NEAREST;
        C3D_TexSetFilter(&texture, filter, filter);
        C3D_TexSetWrap(&texture, GPU_CLAMP_TO_EDGE, GPU_CLAMP_TO_EDGE);
        mImpl->RebindTexture(&texture);
        return;
    }
    const uint32_t textureId = mImpl->selectedTextures[sampler];
    if (textureId == 0 || textureId >= mImpl->textures.size() || !mImpl->textures[textureId].initialized) {
        return;
    }
    auto& texture = mImpl->textures[textureId].texture;
    const GPU_TEXTURE_FILTER_PARAM filter = linearFilter ? GPU_LINEAR : GPU_NEAREST;
    C3D_TexSetFilter(&texture, filter, filter);
    const auto wrapS = (cms & 2U) != 0 ? GPU_CLAMP_TO_EDGE : ((cms & 1U) != 0 ? GPU_MIRRORED_REPEAT : GPU_REPEAT);
    const auto wrapT = (cmt & 2U) != 0 ? GPU_CLAMP_TO_EDGE : ((cmt & 1U) != 0 ? GPU_MIRRORED_REPEAT : GPU_REPEAT);
    C3D_TexSetWrap(&texture, wrapS, wrapT);
    mImpl->RebindTexture(&texture);
}

void GfxRenderingAPICitro3D::SetDepthTestAndMask(bool depthTest, bool zUpdate) {
    mImpl->depthTest = depthTest;
    mImpl->depthWrite = zUpdate;
    C3D_DepthTest(depthTest, depthTest ? GPU_GREATER : GPU_ALWAYS,
                  static_cast<GPU_WRITEMASK>(GPU_WRITE_COLOR | (zUpdate ? GPU_WRITE_DEPTH : 0)));
    // SetDepthTestAndMask already emitted the complete depth state. Keep the
    // draw-state cache synchronized so the next batch does not rebuild all six
    // TEV stages solely because these two cached values lagged behind.
    mImpl->tevStateDepthTest = depthTest;
    mImpl->tevStateDepthWrite = zUpdate;
}

void GfxRenderingAPICitro3D::SetZmodeDecal(bool decal) {
    mImpl->decal = decal;
    // The vertex shader maps OpenGL clip depth into PICA's [-w, 0] range.
    // Reverse that range onto [1, 0] for GPU_GREATER and the zero-cleared
    // depth buffer. An offset of +1 saturates nearly every fragment at 1 and
    // makes later geometry fail the depth test.
    C3D_DepthMap(true, -1.0f, decal ? -0.001f : 0.0f);
}

void GfxRenderingAPICitro3D::SetViewport(int x, int y, int width, int height) {
    if (mImpl->activeTarget == mImpl->sceneTarget) {
        const float horizontalScale =
            static_cast<float>(mImpl->renderWidth) / static_cast<float>(kTopLogicalWidth);
        const float verticalScale =
            static_cast<float>(mImpl->renderHeight) / static_cast<float>(kTopHeight);
        x = static_cast<int>(std::lround(x * horizontalScale));
        y = static_cast<int>(std::lround(y * verticalScale));
        width = static_cast<int>(std::lround(width * horizontalScale));
        height = static_cast<int>(std::lround(height * verticalScale));
        mImpl->viewportX = x;
        mImpl->viewportY = y;
        mImpl->viewportWidth = width;
        mImpl->viewportHeight = height;
        C3D_SetViewport(static_cast<uint32_t>(std::max(0, x)),
                        static_cast<uint32_t>(std::max(0, y)),
                        static_cast<uint32_t>(std::max(0, width)),
                        static_cast<uint32_t>(std::max(0, height)));
        return;
    }
    if (mImpl->activeTarget == mImpl->topTarget && mImpl->outputWidth == kTopWideWidth) {
        x *= 2;
        width *= 2;
    }
    mImpl->viewportX = x;
    mImpl->viewportY = y;
    mImpl->viewportWidth = width;
    mImpl->viewportHeight = height;
    C3D_SetViewport(static_cast<uint32_t>(std::max(0, y)), static_cast<uint32_t>(std::max(0, x)),
                    static_cast<uint32_t>(std::max(0, height)), static_cast<uint32_t>(std::max(0, width)));
}

void GfxRenderingAPICitro3D::SetScissor(int x, int y, int width, int height) {
    if (mImpl->activeTarget == mImpl->sceneTarget) {
        const float horizontalScale =
            static_cast<float>(mImpl->renderWidth) / static_cast<float>(kTopLogicalWidth);
        const float verticalScale =
            static_cast<float>(mImpl->renderHeight) / static_cast<float>(kTopHeight);
        x = static_cast<int>(std::lround(x * horizontalScale));
        y = static_cast<int>(std::lround(y * verticalScale));
        width = static_cast<int>(std::lround(width * horizontalScale));
        height = static_cast<int>(std::lround(height * verticalScale));
        mImpl->scissorX = x;
        mImpl->scissorY = y;
        mImpl->scissorWidth = width;
        mImpl->scissorHeight = height;
        mImpl->scissorEnabled = width > 0 && height > 0;
        if (!mImpl->scissorEnabled) {
            C3D_SetScissor(GPU_SCISSOR_DISABLE, 0, 0, 0, 0);
            return;
        }
        C3D_SetScissor(GPU_SCISSOR_NORMAL,
                       static_cast<uint32_t>(std::max(0, x)),
                       static_cast<uint32_t>(std::max(0, y)),
                       static_cast<uint32_t>(std::max(0, x + width)),
                       static_cast<uint32_t>(std::max(0, y + height)));
        return;
    }
    if (mImpl->activeTarget == mImpl->topTarget && mImpl->outputWidth == kTopWideWidth) {
        x *= 2;
        width *= 2;
    }
    mImpl->scissorX = x;
    mImpl->scissorY = y;
    mImpl->scissorWidth = width;
    mImpl->scissorHeight = height;
    mImpl->scissorEnabled = width > 0 && height > 0;
    if (!mImpl->scissorEnabled) {
        C3D_SetScissor(GPU_SCISSOR_DISABLE, 0, 0, 0, 0);
        return;
    }
    C3D_SetScissor(GPU_SCISSOR_NORMAL, static_cast<uint32_t>(std::max(0, y)),
                   static_cast<uint32_t>(std::max(0, x)), static_cast<uint32_t>(std::max(0, y + height)),
                   static_cast<uint32_t>(std::max(0, x + width)));
}

void GfxRenderingAPICitro3D::SetUseAlpha(bool useAlpha) {
    mImpl->useAlpha = useAlpha;
    ApplyAlphaBlend(useAlpha, mImpl->currentProgram);
}

void GfxRenderingAPICitro3D::DrawTriangles(float bufVbo[], size_t bufVboLen, size_t bufVboNumTris) {
    Soh3dsProfileScope drawProfile(Soh3dsProfileSection::Draw);
    ShaderProgram* program = mImpl->currentProgram;
    if (!mImpl->frameActive || program == nullptr || program->invisible || bufVbo == nullptr ||
        program->strideFloats == 0 || bufVboNumTris == 0 || mImpl->packedVertices == nullptr) {
        return;
    }

    const size_t sourceVertexCount = std::min(bufVboNumTris * 3, static_cast<size_t>(kMaxSourceVertices));
    const size_t sourceTriangleCount = sourceVertexCount / 3;
    if (program->strideFloats > kMaxVertexStrideFloats ||
        bufVboLen < sourceVertexCount * program->strideFloats) {
        return;
    }
    // A texture whose allocation failed (or a framebuffer never drawn to or
    // copied into) has no storage; the unit still holds whatever was bound
    // before. For unit 0 skip the draw rather than paint it with an unrelated
    // image - the interpreter re-imports on the next lookup. Unit 1 is
    // different: OoT's stock two-cycle idiom `TEXEL1 * PRIM_LOD_FRAC + COMBINED`
    // (webs, vines, scene water, most static geometry) references TEXEL1 with
    // only tile 0 loaded, so Fast3D imports a STALE tile 1 that may or may not
    // resolve from frame to frame. Desktop samples that garbage times zero;
    // skipping made those surfaces pop in and out on hardware. Bind unit 0's
    // texture there instead - deterministic, and multiplied by zero anyway.
    for (int texture = 0; texture < 2; ++texture) {
        if (!program->usedTextures[texture]) {
            continue;
        }
        const int framebufferId = mImpl->selectedFramebuffers[texture];
        bool hasStorage;
        if (framebufferId != 0) {
            hasStorage = framebufferId > 0 && framebufferId < static_cast<int>(mImpl->framebuffers.size()) &&
                         mImpl->framebuffers[framebufferId] != nullptr && mImpl->framebuffers[framebufferId]->initialized;
        } else {
            const uint32_t textureId = mImpl->selectedTextures[texture];
            hasStorage = textureId < mImpl->textures.size() && mImpl->textures[textureId].initialized;
        }
        if (hasStorage) {
            continue;
        }
        if (texture == 0) {
            return;
        }
        const uint32_t fallbackId = mImpl->selectedTextures[0];
        if (mImpl->selectedFramebuffers[0] == 0 && fallbackId < mImpl->textures.size() &&
            mImpl->textures[fallbackId].initialized) {
            mImpl->BindTexture(1, &mImpl->textures[fallbackId].texture);
        }
    }

    const float* drawVertices = bufVbo;
    size_t vertexCount = sourceVertexCount;
    bool needsClipping = false;
    for (size_t vertex = 0; vertex < sourceVertexCount; ++vertex) {
        const float* v = bufVbo + vertex * program->strideFloats;
        if (v[2] + v[3] < 0.0f) { // in front of the near plane
            needsClipping = true;
            break;
        }
    }
    if (needsClipping) {
        mImpl->clipScratch.resize(sourceTriangleCount * 6 * program->strideFloats);
        size_t clippedVertexCount = 0;
        for (size_t triangle = 0; triangle < sourceTriangleCount; ++triangle) {
            const float* triangleVertices[3] = {
                bufVbo + (triangle * 3 + 0) * program->strideFloats,
                bufVbo + (triangle * 3 + 1) * program->strideFloats,
                bufVbo + (triangle * 3 + 2) * program->strideFloats,
            };
            const int insideCount =
                static_cast<int>(triangleVertices[0][2] + triangleVertices[0][3] >= 0.0f) +
                static_cast<int>(triangleVertices[1][2] + triangleVertices[1][3] >= 0.0f) +
                static_cast<int>(triangleVertices[2][2] + triangleVertices[2][3] >= 0.0f);
            if (insideCount == 3) {
                std::copy_n(triangleVertices[0], 3 * program->strideFloats,
                            mImpl->clipScratch.data() +
                                clippedVertexCount * program->strideFloats);
                clippedVertexCount += 3;
                continue;
            }
            if (insideCount == 0) {
                continue;
            }
            clippedVertexCount += ClipTriangleAgainstW(
                triangleVertices, program->strideFloats,
                mImpl->clipScratch.data() + clippedVertexCount * program->strideFloats);
        }
        if (clippedVertexCount == 0) {
            return;
        }
        drawVertices = mImpl->clipScratch.data();
        vertexCount = std::min(clippedVertexCount, static_cast<size_t>(kMaxDrawVertices));
    }
    if (mImpl->packedVertexCount + vertexCount > kVertexBufferCapacity) {
        throw std::length_error("3DS packed vertex buffer exhausted");
    }

    const size_t firstVertex = mImpl->packedVertexCount;

    std::array<std::array<float, 4>, 7> constants = {};
    // A program with one combiner input can always source it from the primary
    // vertex color. This is exact for both uniform and varying batches, avoids
    // scanning every vertex to rediscover uniformity, and keeps per-draw tint
    // changes out of the six-stage TEV rebuild path.
    int varyingInput = program->numInputs == 1 ? 0 : -1;
    for (uint8_t input = 0; input < program->numInputs; ++input) {
        const uint8_t inputOffset = program->inputOffsets[input];
        for (int component = 0; component < 4; ++component) {
            // Inputs are packed rgb (3 floats) or rgba (4) by Fast3D. The old
            // `std::min(component, 2)` read the BLUE channel as alpha whenever
            // alpha was present: every constant alpha (PRIM/ENV alpha fades,
            // PRIM_LOD_FRAC) was wrong - the Deku Tree webs drew opaque white
            // because `TEXEL1 * PRIM_LOD_FRAC(=blue=1) + COMBINED` saturated.
            constants[input][component] =
                component == 3 && !program->alpha ? 1.0f : drawVertices[inputOffset + component];
        }
        // PICA exposes one primary vertex color to the TEV pipeline. Once that
        // varying input is chosen, every later input is necessarily represented
        // by its first-vertex constant, so scanning the rest of the batch cannot
        // affect the generated TEV state.
        if (varyingInput >= 0) {
            continue;
        }
        bool constant = true;
        for (size_t vertex = 1; vertex < vertexCount && constant; ++vertex) {
            const float* source = drawVertices + vertex * program->strideFloats + inputOffset;
            const int componentCount = program->alpha ? 4 : 3;
            for (int component = 0; component < componentCount; ++component) {
                if (std::fabs(source[component] - constants[input][component]) > 1.0e-5f) {
                    constant = false;
                    break;
                }
            }
        }
        if (!constant && varyingInput < 0) {
            varyingInput = input;
        }
    }
    // SoH-3DS: every input uniform (2D HUD, menus) still puts input 0 on the
    // primary colour, so a stage mixing two inputs - (PRIM - ENV) * TEXEL0 +
    // ENV is the HUD text/outline shape - needs only one GPU_CONSTANT.
    // ponytail: input 0 is a fixed pick; three uniform inputs in one stage
    // still collide (none found in OoT).
    if (varyingInput < 0 && program->numInputs > 0) {
        varyingInput = 0;
    }

    // Fast3D already applies the same aspect correction to backgrounds and
    // 3D geometry. Enlarging only a 320x240 backdrop displaces its doorways
    // relative to the door actors (for example the Market Guard House).
    const float coverScale = 1.0f;
    std::array<float, 2> textureScaleU = { 1.0f, 1.0f };
    std::array<float, 2> textureScaleV = { 1.0f, 1.0f };
    std::array<float, 2> textureOffsetV = { 0.0f, 0.0f };
    std::array<bool, 2> rotatedFramebuffer = { false, false };
    for (int texture = 0; texture < 2; ++texture) {
        if (!program->usedTextures[texture]) {
            continue;
        }
        // SoH-3DS: SelectTextureFb parks selectedTextures at 0 (the null slot).
        // A bound framebuffer owns the unit's scale outright; scaling by both
        // made the fullscreen composite sample a corner ("zoomed in" game).
        const int framebufferId = mImpl->selectedFramebuffers[texture];
        if (framebufferId > 0 && framebufferId < static_cast<int>(mImpl->framebuffers.size()) &&
            mImpl->framebuffers[framebufferId] != nullptr &&
            mImpl->framebuffers[framebufferId]->initialized) {
            const auto& slot = *mImpl->framebuffers[framebufferId];
            if (slot.rotated) {
                // PICA samples v=0 from the final memory row. Rendered content
                // has leading rows from the POT target's rasterizer Y flip;
                // GX copies from the top LCD start at row zero instead.
                rotatedFramebuffer[texture] = true;
                textureScaleU[texture] = static_cast<float>(slot.contentHeight) / slot.texture.width;
                textureScaleV[texture] = static_cast<float>(slot.contentWidth) / slot.texture.height;
                textureOffsetV[texture] = static_cast<float>(slot.contentOffsetY) / slot.texture.height;
            } else {
                textureScaleU[texture] *= static_cast<float>(slot.contentWidth) / slot.texture.width;
                textureScaleV[texture] *= static_cast<float>(slot.contentHeight) / slot.texture.height;
            }
            continue;
        }
        const uint32_t textureId = mImpl->selectedTextures[texture];
        if (textureId < mImpl->textures.size() && mImpl->textures[textureId].initialized) {
            const auto& slot = mImpl->textures[textureId];
            textureScaleU[texture] *= static_cast<float>(slot.sourceWidth) / slot.texture.width;
            textureScaleV[texture] *= static_cast<float>(slot.sourceHeight) / slot.texture.height;
        }
    }
    {
        Soh3dsProfileScope packProfile(Soh3dsProfileSection::Pack);
        const bool commonPacked = PackVertices(drawVertices, mImpl->packedVertices + firstVertex, vertexCount, program,
                                               varyingInput, coverScale, textureScaleU, textureScaleV, textureOffsetV,
                                               rotatedFramebuffer);
        Soh3dsProfilePackBatch(commonPacked, static_cast<uint32_t>(vertexCount));
    }
    Soh3dsProfileScope stateProfile(Soh3dsProfileSection::State);
    std::array<float, 4> fogColor = { 0.0f, 0.0f, 0.0f, 0.0f };
    if (program->fog) {
        std::copy_n(drawVertices + program->fogOffset, 3, fogColor.begin());
    }
    const int alphaVaryingInput = program->fog ? -1 : varyingInput;

    std::array<float, 4> grayscaleColor = { 1.0f, 1.0f, 1.0f, 0.0f };
    if (program->grayscale) {
        const float* color = drawVertices + program->grayscaleOffset;
        std::copy_n(color, grayscaleColor.size(), grayscaleColor.begin());
    }

    std::array<uint32_t, 7> packedTevConstants = {};
    for (size_t index = 0; index < program->numInputs; ++index) {
        if (static_cast<int>(index) == varyingInput) {
            continue;
        }
        packedTevConstants[index] = PackColor(constants[index]);
    }
    const uint32_t packedTevGrayscale =
        program->grayscale ? PackColor(grayscaleColor) : 0;
    // The alpha side reads the varying input as a constant while fog owns the
    // vertex alpha, so that constant is part of the TEV state too.
    if (program->fog && varyingInput >= 0) {
        fogColor[3] = constants[varyingInput][3];
    }
    const uint32_t packedTevFog = program->fog ? PackColor(fogColor) : 0;
    const bool updateTevState = !mImpl->tevStateValid ||
                                mImpl->tevStateProgram != program ||
                                mImpl->tevStateVaryingInput != varyingInput ||
                                mImpl->tevStateConstants != packedTevConstants ||
                                mImpl->tevStateGrayscale != packedTevGrayscale ||
                                mImpl->tevStateFog != packedTevFog ||
                                mImpl->tevStateDepthTest != mImpl->depthTest ||
                                mImpl->tevStateDepthWrite != mImpl->depthWrite;
    if (updateTevState) {

    // The previous-buffer update mask is persistent Citro3D state. Reset it
    // for every draw so a grayscale program cannot leak into the next one.
    C3D_TexEnvBufUpdate(C3D_Both, 0);
    C3D_TexEnvBufColor(0xFFFFFFFF);

    int stage = 0;
    const int cycleCount = program->twoCycle ? 2 : 1;
    for (int cycle = 0; cycle < cycleCount && stage < 6; ++cycle) {
        const ChannelPlan rgbPlan = BuildChannelPlan(program->combiner[cycle][0], varyingInput, constants, false);
        const ChannelPlan alphaPlan =
            program->alpha ? BuildChannelPlan(program->combiner[cycle][1], alphaVaryingInput, constants, true)
                           : SingleOperationPlan(PassPrevious());
        const size_t operationCount = std::max(rgbPlan.size(), alphaPlan.size());
        for (size_t operationIndex = 0; operationIndex < operationCount && stage < 6; ++operationIndex, ++stage) {
            const ChannelOperation rgbOperation =
                operationIndex < rgbPlan.size() ? rgbPlan[operationIndex] : PassPrevious();
            const ChannelOperation alphaOperation =
                operationIndex < alphaPlan.size() ? alphaPlan[operationIndex] : PassPrevious();
            C3D_TexEnv* environment = C3D_GetTexEnv(stage);
            C3D_TexEnvInit(environment);
            ConfigureChannel(environment, C3D_RGB, rgbOperation, varyingInput, cycle);
            ConfigureChannel(environment, C3D_Alpha, alphaOperation, alphaVaryingInput, cycle);
            ConfigureOperands(environment, rgbOperation, alphaOperation);

            std::array<float, 4> environmentColor = StageConstant(rgbPlan, rgbOperation, varyingInput, constants);
            environmentColor[3] = StageConstant(alphaPlan, alphaOperation, alphaVaryingInput, constants)[3];
            C3D_TexEnvColor(environment, PackColor(environmentColor));
        }
    }

    // Fog: Fast3D's `rgb = mix(rgb, fog.rgb, factor)` after the combiner, RGB
    // only. GPU_INTERPOLATE(s1, s2, s3) = s1*s3 + s2*(1-s3) with s1 = fog
    // colour (constant), s2 = combiner output, s3 = the per-vertex factor
    // carried in the primary colour's alpha. One stage; the alpha side passes
    // the combiner alpha through untouched, so the alpha test still sees it.
    if (program->fog && stage > 0 && stage < 6) {
        C3D_TexEnv* environment = C3D_GetTexEnv(stage++);
        C3D_TexEnvInit(environment);
        C3D_TexEnvSrc(environment, C3D_RGB, GPU_CONSTANT, GPU_PREVIOUS, GPU_PRIMARY_COLOR);
        C3D_TexEnvOpRgb(environment, GPU_TEVOP_RGB_SRC_COLOR, GPU_TEVOP_RGB_SRC_COLOR, GPU_TEVOP_RGB_SRC_ALPHA);
        C3D_TexEnvFunc(environment, C3D_RGB, GPU_INTERPOLATE);
        ConfigureAlphaPass(environment);
        C3D_TexEnvColor(environment, PackColor({ fogColor[0], fogColor[1], fogColor[2], 0.0f }));
    }

    // Fast3D's grayscale option is: mix(original, tint * average(rgb),
    // tint.a). PICA has no programmable fragment shader, but its previous
    // buffer and per-source R/G/B replication make the average exact (apart
    // from the normal 8-bit TEV quantization): accumulate r/3, g/3, and b/3
    // while applying the tint. The buffer update becomes readable one stage
    // later, so the R stage reads GPU_PREVIOUS directly; the G/B stages and
    // optional final mix read the preserved base RGB from the buffer.
    const float grayscaleMix = std::clamp(grayscaleColor[3], 0.0f, 1.0f);
    const bool needsGrayscaleMix = grayscaleMix < 1.0f - (0.5f / 255.0f);
    const int grayscaleStages = needsGrayscaleMix ? 4 : 3;
    bool grayscaleApplied = false;
    if (program->grayscale && grayscaleMix > 0.5f / 255.0f && stage > 0 &&
        stage + grayscaleStages <= 6 && stage - 1 < 4) {
        grayscaleApplied = true;
        C3D_TexEnvBufUpdate(C3D_RGB, 1 << (stage - 1));

        const std::array<float, 4> scaledTint = {
            std::clamp(grayscaleColor[0], 0.0f, 1.0f) / 3.0f,
            std::clamp(grayscaleColor[1], 0.0f, 1.0f) / 3.0f,
            std::clamp(grayscaleColor[2], 0.0f, 1.0f) / 3.0f,
            grayscaleMix,
        };
        constexpr std::array<GPU_TEVOP_RGB, 3> channelOperands = {
            GPU_TEVOP_RGB_SRC_R,
            GPU_TEVOP_RGB_SRC_G,
            GPU_TEVOP_RGB_SRC_B,
        };
        for (int channel = 0; channel < 3; ++channel, ++stage) {
            C3D_TexEnv* environment = C3D_GetTexEnv(stage);
            C3D_TexEnvInit(environment);
            C3D_TexEnvSrc(environment, C3D_RGB,
                          channel == 0 ? GPU_PREVIOUS : GPU_PREVIOUS_BUFFER, GPU_CONSTANT,
                          GPU_PREVIOUS);
            C3D_TexEnvOpRgb(environment, channelOperands[channel], GPU_TEVOP_RGB_SRC_COLOR,
                            GPU_TEVOP_RGB_SRC_COLOR);
            C3D_TexEnvFunc(environment, C3D_RGB,
                           channel == 0 ? GPU_MODULATE : GPU_MULTIPLY_ADD);
            ConfigureAlphaPass(environment);
            C3D_TexEnvColor(environment, PackColor(scaledTint));
        }

        if (needsGrayscaleMix) {
            C3D_TexEnv* environment = C3D_GetTexEnv(stage++);
            C3D_TexEnvInit(environment);
            C3D_TexEnvSrc(environment, C3D_RGB, GPU_PREVIOUS, GPU_PREVIOUS_BUFFER,
                          GPU_CONSTANT);
            C3D_TexEnvOpRgb(environment, GPU_TEVOP_RGB_SRC_COLOR,
                            GPU_TEVOP_RGB_SRC_COLOR, GPU_TEVOP_RGB_SRC_ALPHA);
            C3D_TexEnvFunc(environment, C3D_RGB, GPU_INTERPOLATE);
            ConfigureAlphaPass(environment);
            C3D_TexEnvColor(environment, PackColor({ 0.0f, 0.0f, 0.0f, grayscaleMix }));
        }
    }

    // Some stock menu-background combiners consume four or five TEV stages,
    // leaving too little room for the exact three-stage luminance pass above.
    // Those draws use a fully opaque grayscale color and previously lost the
    // red/green/blue menu filter completely. Preserve the visible stock tint
    // with a one-stage modulation fallback; simpler shaders still take the
    // exact luminance path.
    if (program->grayscale && !grayscaleApplied &&
        grayscaleMix >= 1.0f - (0.5f / 255.0f) && stage > 0 && stage < 6) {
        C3D_TexEnv* environment = C3D_GetTexEnv(stage++);
        C3D_TexEnvInit(environment);
        C3D_TexEnvSrc(environment, C3D_RGB, GPU_PREVIOUS, GPU_CONSTANT, GPU_PREVIOUS);
        C3D_TexEnvOpRgb(environment, GPU_TEVOP_RGB_SRC_COLOR,
                       GPU_TEVOP_RGB_SRC_COLOR, GPU_TEVOP_RGB_SRC_COLOR);
        C3D_TexEnvFunc(environment, C3D_RGB, GPU_MODULATE);
        ConfigureAlphaPass(environment);
        C3D_TexEnvColor(environment,
                       PackColor({ std::clamp(grayscaleColor[0], 0.0f, 1.0f),
                                   std::clamp(grayscaleColor[1], 0.0f, 1.0f),
                                   std::clamp(grayscaleColor[2], 0.0f, 1.0f), 1.0f }));
    }
    for (; stage < 6; ++stage) {
        C3D_TexEnv* environment = C3D_GetTexEnv(stage);
        C3D_TexEnvInit(environment);
    }

    // Alpha-test gating and references live in ApplyAlphaTest. History: an
    // ungated GPU_GREATER 0x08 test read undefined stage-0 alpha on alpha-less
    // programs and discarded OoT's prerendered room backgrounds (z_room.c sets
    // G_AC_THRESHOLD with G_RM_NOOP) - the green Link's House / yellow shop.
    ApplyAlphaTest(program);
    C3D_DepthTest(mImpl->depthTest, mImpl->depthTest ? GPU_GREATER : GPU_ALWAYS,
                  static_cast<GPU_WRITEMASK>(GPU_WRITE_COLOR | (mImpl->depthWrite ? GPU_WRITE_DEPTH : 0)));
        mImpl->tevStateValid = true;
        mImpl->tevStateProgram = program;
        mImpl->tevStateVaryingInput = varyingInput;
        mImpl->tevStateConstants = packedTevConstants;
        mImpl->tevStateGrayscale = packedTevGrayscale;
        mImpl->tevStateFog = packedTevFog;
        mImpl->tevStateDepthTest = mImpl->depthTest;
        mImpl->tevStateDepthWrite = mImpl->depthWrite;
    }
    if (mImpl->dirtyVertexBegin == mImpl->dirtyVertexEnd) {
        mImpl->dirtyVertexBegin = firstVertex;
    }
    mImpl->dirtyVertexEnd = firstVertex + vertexCount;
    // Record that this draw samples the bound slots' current storage, so a
    // later same-frame upload knows it must not overwrite that memory.
    for (int texture = 0; texture < 2; ++texture) {
        if (!program->usedTextures[texture] || mImpl->selectedFramebuffers[texture] != 0) {
            continue;
        }
        const uint32_t sampledId = mImpl->selectedTextures[texture];
        if (sampledId != 0 && sampledId < mImpl->textures.size() && mImpl->textures[sampledId].initialized) {
            mImpl->textures[sampledId].lastDrawnFrame = mImpl->frameOrdinal;
        }
    }
    C3D_DrawArrays(GPU_TRIANGLES, static_cast<int>(firstVertex), static_cast<int>(vertexCount));
    ++mImpl->drawCallCount;
    mImpl->sampleFogDrawCount += program->fog ? 1u : 0u;
    mImpl->triangleCount += vertexCount / 3;
    mImpl->packedVertexCount += vertexCount;
    mImpl->framePeakPackedVertices = std::max(mImpl->framePeakPackedVertices, mImpl->packedVertexCount);
    mImpl->samplePeakPackedVertices =
        std::max(mImpl->samplePeakPackedVertices, mImpl->framePeakPackedVertices);
}

void GfxRenderingAPICitro3D::FlushPackedVertices() {
    if (mImpl->packedVertices == nullptr || mImpl->dirtyVertexBegin >= mImpl->dirtyVertexEnd) {
        return;
    }

    const size_t vertexCount = mImpl->dirtyVertexEnd - mImpl->dirtyVertexBegin;
    const size_t byteCount = vertexCount * sizeof(PackedVertex);
    GSPGPU_FlushDataCache(mImpl->packedVertices + mImpl->dirtyVertexBegin, byteCount);
    ++mImpl->vertexUploadCount;
    mImpl->vertexUploadBytes += byteCount;
    mImpl->dirtyVertexBegin = mImpl->packedVertexCount;
    mImpl->dirtyVertexEnd = mImpl->packedVertexCount;
}

bool GfxRenderingAPICitro3D::EnsurePresentationResources() {
    if (mImpl->sceneTextureInitialized && mImpl->sceneTarget != nullptr &&
        mImpl->crtMaskTextureInitialized && mImpl->postprocessVertices != nullptr) {
        return true;
    }

    const auto releasePartialResources = [this]() {
        if (mImpl->sceneTarget != nullptr) {
            C3D_RenderTargetDelete(mImpl->sceneTarget);
            mImpl->sceneTarget = nullptr;
        }
        if (mImpl->sceneTextureInitialized) {
            C3D_TexDelete(&mImpl->sceneTexture);
            mImpl->sceneTextureInitialized = false;
        }
        if (mImpl->crtMaskTextureInitialized) {
            C3D_TexDelete(&mImpl->crtMaskTexture);
            mImpl->crtMaskTextureInitialized = false;
        }
        if (mImpl->postprocessVertices != nullptr) {
            linearFree(mImpl->postprocessVertices);
            mImpl->postprocessVertices = nullptr;
        }
    };

    const uint16_t backingWidth = mImpl->outputWidth == kTopWideWidth ? 1024 : 512;
    if (!C3D_TexInitVRAM(&mImpl->sceneTexture, backingWidth, kSceneBackingHeight, GPU_RGBA8)) {
        releasePartialResources();
        return false;
    }
    mImpl->sceneTextureInitialized = true;
    mImpl->sceneTarget = C3D_RenderTargetCreateFromTex(
        &mImpl->sceneTexture, GPU_TEXFACE_2D, 0, C3D_DEPTHTYPE(GPU_RB_DEPTH24_STENCIL8));
    if (mImpl->sceneTarget == nullptr) {
        releasePartialResources();
        return false;
    }
    C3D_TexSetFilter(&mImpl->sceneTexture, GPU_LINEAR, GPU_LINEAR);
    C3D_TexSetWrap(&mImpl->sceneTexture, GPU_CLAMP_TO_EDGE, GPU_CLAMP_TO_EDGE);

    mImpl->postprocessVertices = static_cast<PackedVertex*>(
        linearAlloc(kPostprocessVertexCapacity * sizeof(PackedVertex)));
    if (mImpl->postprocessVertices == nullptr) {
        releasePartialResources();
        return false;
    }

    if (!C3D_TexInit(&mImpl->crtMaskTexture, kCrtMaskSize, kCrtMaskSize, GPU_RGBA8)) {
        releasePartialResources();
        return false;
    }
    mImpl->crtMaskTextureInitialized = true;
    auto* maskPixels = static_cast<uint32_t*>(mImpl->crtMaskTexture.data);
    for (uint32_t y = 0; y < kCrtMaskSize; ++y) {
        const uint8_t scanline = (y & 1U) == 0 ? 255 : 216;
        for (uint32_t x = 0; x < kCrtMaskSize; ++x) {
            uint8_t red = 240;
            uint8_t green = 240;
            uint8_t blue = 240;
            switch (x % 3U) {
                case 0: red = 255; break;
                case 1: green = 255; break;
                default: blue = 255; break;
            }
            red = static_cast<uint8_t>(static_cast<uint16_t>(red) * scanline / 255U);
            green = static_cast<uint8_t>(static_cast<uint16_t>(green) * scanline / 255U);
            blue = static_cast<uint8_t>(static_cast<uint16_t>(blue) * scanline / 255U);
            const uint32_t rgba = static_cast<uint32_t>(red) |
                                  (static_cast<uint32_t>(green) << 8U) |
                                  (static_cast<uint32_t>(blue) << 16U) | 0xFF000000U;
            maskPixels[MortonOffset8x8(x, y)] = __builtin_bswap32(rgba);
        }
    }
    C3D_TexFlush(&mImpl->crtMaskTexture);
    C3D_TexSetFilter(&mImpl->crtMaskTexture, GPU_NEAREST, GPU_NEAREST);
    C3D_TexSetWrap(&mImpl->crtMaskTexture, GPU_REPEAT, GPU_REPEAT);
    return true;
}

void GfxRenderingAPICitro3D::UploadProjectionForActiveTarget() {
    C3D_Mtx depthConversion;
    Mtx_Identity(&depthConversion);
    depthConversion.r[2].z = 0.4999f;
    depthConversion.r[2].w = -0.5f;
    if (mImpl->activeTarget == mImpl->sceneTarget) {
        C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, mImpl->projectionUniform, &depthConversion);
        return;
    }

    C3D_Mtx tilt;
    Mtx_Identity(&tilt);
    tilt.r[0].x = 0.0f;
    tilt.r[0].y = 1.0f;
    tilt.r[1].x = -1.0f;
    tilt.r[1].y = 0.0f;
    if (mImpl->activeTarget == mImpl->bottomTarget && mImpl->bottomZoomActive && mImpl->bottomZoom.scale != 1.0f) {
        // Dual screen: zoom the bottom pass about an NDC centre so the game's
        // 320x240 minimap group (map, compass icons, marks) fills the screen
        // without touching every texrect. Homogeneous: x' = s*x - s*cx*w.
        C3D_Mtx zoom;
        Mtx_Identity(&zoom);
        zoom.r[0].x = mImpl->bottomZoom.scale;
        zoom.r[0].w = -mImpl->bottomZoom.scale * mImpl->bottomZoom.centerX;
        zoom.r[1].y = mImpl->bottomZoom.scale;
        zoom.r[1].w = -mImpl->bottomZoom.scale * mImpl->bottomZoom.centerY;
        C3D_Mtx zoomed;
        Mtx_Multiply(&zoomed, &zoom, &depthConversion);
        depthConversion = zoomed;
    }
    C3D_Mtx projection;
    Mtx_Multiply(&projection, &tilt, &depthConversion);
    C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, mImpl->projectionUniform, &projection);
}

void GfxRenderingAPICitro3D::PresentSceneToTopTarget() {
    if (!mImpl->frameActive || !mImpl->postprocessActive || mImpl->scenePresented ||
        mImpl->sceneTarget == nullptr || mImpl->postprocessVertices == nullptr) {
        return;
    }

    FlushPackedVertices();
    ++mImpl->sampleFrameSplitCount;
    C3D_FrameSplit(GX_CMDLIST_FLUSH);
    C3D_RenderTargetClear(mImpl->topTarget, C3D_CLEAR_ALL, 0x000000FF, 0);
    C3D_FrameDrawOn(mImpl->topTarget);
    mImpl->activeTarget = mImpl->topTarget;

    C3D_BindProgram(&mImpl->shaderProgram);
    C3D_AttrInfo* attributeInfo = C3D_GetAttrInfo();
    AttrInfo_Init(attributeInfo);
    AttrInfo_AddLoader(attributeInfo, 0, GPU_FLOAT, 4);
    AttrInfo_AddLoader(attributeInfo, 1, GPU_FLOAT, 2);
    AttrInfo_AddLoader(attributeInfo, 2, GPU_FLOAT, 2);
    AttrInfo_AddLoader(attributeInfo, 3, GPU_UNSIGNED_BYTE, 4);
    C3D_BufInfo* bufferInfo = C3D_GetBufInfo();
    BufInfo_Init(bufferInfo);
    BufInfo_Add(bufferInfo, mImpl->postprocessVertices, sizeof(PackedVertex), 4, 0x3210);
    UploadProjectionForActiveTarget();

    C3D_SetViewport(0, 0, kTopHeight, mImpl->outputWidth);
    C3D_SetScissor(GPU_SCISSOR_DISABLE, 0, 0, 0, 0);
    C3D_CullFace(GPU_CULL_NONE);
    C3D_DepthMap(true, -1.0f, 0.0f);
    C3D_DepthTest(false, GPU_ALWAYS, GPU_WRITE_COLOR);
    C3D_AlphaTest(false, GPU_ALWAYS, 0);
    C3D_TexEnvBufUpdate(C3D_Both, 0);
    C3D_TexEnvBufColor(0xFFFFFFFF);

    C3D_TexSetFilter(&mImpl->sceneTexture, GPU_LINEAR, GPU_LINEAR);
    C3D_TexSetWrap(&mImpl->sceneTexture, GPU_CLAMP_TO_EDGE, GPU_CLAMP_TO_EDGE);
    mImpl->BindTexture(0, &mImpl->sceneTexture);
    if (mImpl->displayFilter == DisplayFilterCrt) {
        mImpl->BindTexture(1, &mImpl->crtMaskTexture);
    }

    C3D_TexEnv* environment = C3D_GetTexEnv(0);
    C3D_TexEnvInit(environment);
    C3D_TexEnvSrc(environment, C3D_RGB, GPU_TEXTURE0, GPU_TEXTURE0, GPU_TEXTURE0);
    C3D_TexEnvFunc(environment, C3D_RGB, GPU_REPLACE);
    if (mImpl->displayFilter == DisplayFilterBlur) {
        C3D_TexEnvSrc(environment, C3D_Alpha, GPU_PRIMARY_COLOR, GPU_PRIMARY_COLOR,
                      GPU_PRIMARY_COLOR);
    } else {
        C3D_TexEnvSrc(environment, C3D_Alpha, GPU_TEXTURE0, GPU_TEXTURE0, GPU_TEXTURE0);
    }
    C3D_TexEnvFunc(environment, C3D_Alpha, GPU_REPLACE);

    int usedStages = 1;
    if (mImpl->displayFilter == DisplayFilterCrt) {
        environment = C3D_GetTexEnv(1);
        C3D_TexEnvInit(environment);
        C3D_TexEnvSrc(environment, C3D_RGB, GPU_PREVIOUS, GPU_TEXTURE1, GPU_PREVIOUS);
        C3D_TexEnvFunc(environment, C3D_RGB, GPU_MODULATE);
        ConfigureAlphaPass(environment);
        usedStages = 2;
    }
    for (int stage = usedStages; stage < 6; ++stage) {
        C3D_TexEnvInit(C3D_GetTexEnv(stage));
    }

    const float backingWidth = static_cast<float>(mImpl->sceneTexture.width);
    const float backingHeight = static_cast<float>(mImpl->sceneTexture.height);
    const float uMinimum = 0.5f / backingWidth;
    const float uMaximum = (static_cast<float>(mImpl->renderWidth) - 0.5f) / backingWidth;
    const float vMinimum = 0.5f / backingHeight;
    const float vMaximum = (static_cast<float>(mImpl->renderHeight) - 0.5f) / backingHeight;
    const float maskMaximumU = static_cast<float>(mImpl->outputWidth) / kCrtMaskSize;
    const float maskMaximumV = static_cast<float>(kTopHeight) / kCrtMaskSize;

    const auto writeQuad = [this, maskMaximumU, maskMaximumV](
                               size_t firstVertex, float u0, float v0, float u1, float v1,
                               uint8_t alpha) {
        constexpr std::array<std::array<float, 2>, 6> kPositions = {{
            {{ -1.0f, -1.0f }}, {{ 1.0f, -1.0f }}, {{ 1.0f, 1.0f }},
            {{ -1.0f, -1.0f }}, {{ 1.0f, 1.0f }}, {{ -1.0f, 1.0f }},
        }};
        const std::array<std::array<float, 2>, 6> sceneCoordinates = {{
            {{ u0, v0 }}, {{ u1, v0 }}, {{ u1, v1 }},
            {{ u0, v0 }}, {{ u1, v1 }}, {{ u0, v1 }},
        }};
        const std::array<std::array<float, 2>, 6> maskCoordinates = {{
            {{ 0.0f, 0.0f }}, {{ maskMaximumU, 0.0f }}, {{ maskMaximumU, maskMaximumV }},
            {{ 0.0f, 0.0f }}, {{ maskMaximumU, maskMaximumV }}, {{ 0.0f, maskMaximumV }},
        }};
        for (size_t vertex = 0; vertex < 6; ++vertex) {
            PackedVertex& destination = mImpl->postprocessVertices[firstVertex + vertex];
            destination.position[0] = kPositions[vertex][0];
            destination.position[1] = kPositions[vertex][1];
            destination.position[2] = 0.0f;
            destination.position[3] = 1.0f;
            destination.texcoord0[0] = sceneCoordinates[vertex][0];
            destination.texcoord0[1] = sceneCoordinates[vertex][1];
            destination.texcoord1[0] = maskCoordinates[vertex][0];
            destination.texcoord1[1] = maskCoordinates[vertex][1];
            destination.color[0] = 255;
            destination.color[1] = 255;
            destination.color[2] = 255;
            destination.color[3] = alpha;
        }
    };

    size_t quadCount = 1;
    if (mImpl->displayFilter == DisplayFilterBlur) {
        constexpr std::array<std::array<float, 2>, 4> kBlurOffsets = {{
            {{ -0.35f, -0.35f }}, {{ 0.35f, -0.35f }},
            {{ -0.35f, 0.35f }}, {{ 0.35f, 0.35f }},
        }};
        quadCount = kBlurOffsets.size();
        for (size_t quad = 0; quad < quadCount; ++quad) {
            const float offsetU = kBlurOffsets[quad][0] / backingWidth;
            const float offsetV = kBlurOffsets[quad][1] / backingHeight;
            writeQuad(quad * 6, uMinimum + offsetU, vMinimum + offsetV,
                      uMaximum + offsetU, vMaximum + offsetV, 64);
        }
        C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_SRC_ALPHA, GPU_ONE,
                       GPU_ONE, GPU_ONE);
    } else {
        writeQuad(0, uMinimum, vMinimum, uMaximum, vMaximum, 255);
        C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_ONE, GPU_ZERO,
                       GPU_ONE, GPU_ZERO);
    }

    const size_t vertexCount = quadCount * 6;
    const size_t byteCount = vertexCount * sizeof(PackedVertex);
    GSPGPU_FlushDataCache(mImpl->postprocessVertices, byteCount);
    ++mImpl->vertexUploadCount;
    mImpl->vertexUploadBytes += byteCount;
    C3D_DrawArrays(GPU_TRIANGLES, 0, static_cast<int>(vertexCount));
    ++mImpl->drawCallCount;
    mImpl->triangleCount += vertexCount / 3;

    mImpl->tevStateValid = false;
    mImpl->scenePresented = true;
}

void GfxRenderingAPICitro3D::RestoreFast3DState() {
    mImpl->tevStateValid = false;
    C3D_BindProgram(&mImpl->shaderProgram);

    C3D_AttrInfo* attributeInfo = C3D_GetAttrInfo();
    AttrInfo_Init(attributeInfo);
    AttrInfo_AddLoader(attributeInfo, 0, GPU_FLOAT, 4);
    AttrInfo_AddLoader(attributeInfo, 1, GPU_FLOAT, 2);
    AttrInfo_AddLoader(attributeInfo, 2, GPU_FLOAT, 2);
    AttrInfo_AddLoader(attributeInfo, 3, GPU_UNSIGNED_BYTE, 4);
    C3D_BufInfo* bufferInfo = C3D_GetBufInfo();
    BufInfo_Init(bufferInfo);
    BufInfo_Add(bufferInfo, mImpl->packedVertices, sizeof(PackedVertex), 4, 0x3210);

    UploadProjectionForActiveTarget();

    C3D_CullFace(GPU_CULL_NONE);
    C3D_DepthMap(true, -1.0f, mImpl->decal ? -0.001f : 0.0f);
    C3D_DepthTest(mImpl->depthTest, mImpl->depthTest ? GPU_GREATER : GPU_ALWAYS,
                  static_cast<GPU_WRITEMASK>(GPU_WRITE_COLOR |
                                             (mImpl->depthWrite ? GPU_WRITE_DEPTH : 0)));
    ApplyAlphaBlend(mImpl->useAlpha, mImpl->currentProgram);
    ApplyAlphaTest(mImpl->currentProgram);

    if (mImpl->activeTarget == mImpl->sceneTarget) {
        C3D_SetViewport(static_cast<uint32_t>(std::max(0, mImpl->viewportX)),
                        static_cast<uint32_t>(std::max(0, mImpl->viewportY)),
                        static_cast<uint32_t>(std::max(0, mImpl->viewportWidth)),
                        static_cast<uint32_t>(std::max(0, mImpl->viewportHeight)));
    } else {
        C3D_SetViewport(static_cast<uint32_t>(std::max(0, mImpl->viewportY)),
                        static_cast<uint32_t>(std::max(0, mImpl->viewportX)),
                        static_cast<uint32_t>(std::max(0, mImpl->viewportHeight)),
                        static_cast<uint32_t>(std::max(0, mImpl->viewportWidth)));
    }
    if (mImpl->scissorEnabled) {
        if (mImpl->activeTarget == mImpl->sceneTarget) {
            C3D_SetScissor(GPU_SCISSOR_NORMAL,
                           static_cast<uint32_t>(std::max(0, mImpl->scissorX)),
                           static_cast<uint32_t>(std::max(0, mImpl->scissorY)),
                           static_cast<uint32_t>(std::max(0, mImpl->scissorX + mImpl->scissorWidth)),
                           static_cast<uint32_t>(std::max(0, mImpl->scissorY + mImpl->scissorHeight)));
        } else {
            C3D_SetScissor(GPU_SCISSOR_NORMAL,
                           static_cast<uint32_t>(std::max(0, mImpl->scissorY)),
                           static_cast<uint32_t>(std::max(0, mImpl->scissorX)),
                           static_cast<uint32_t>(std::max(0, mImpl->scissorY + mImpl->scissorHeight)),
                           static_cast<uint32_t>(std::max(0, mImpl->scissorX + mImpl->scissorWidth)));
        }
    } else {
        C3D_SetScissor(GPU_SCISSOR_DISABLE, 0, 0, 0, 0);
    }

    C3D_TexEnvBufUpdate(C3D_Both, 0);
    C3D_TexEnvBufColor(0xFFFFFFFF);
    for (int stage = 0; stage < 6; ++stage) {
        C3D_TexEnv* environment = C3D_GetTexEnv(stage);
        C3D_TexEnvInit(environment);
    }

    for (int unit = 0; unit < 3; ++unit) {
        const int framebufferId = mImpl->selectedFramebuffers[unit];
        if (framebufferId > 0 && framebufferId < static_cast<int>(mImpl->framebuffers.size()) &&
            mImpl->framebuffers[framebufferId] != nullptr &&
            mImpl->framebuffers[framebufferId]->initialized) {
            mImpl->BindTexture(unit, &mImpl->framebuffers[framebufferId]->texture);
            continue;
        }
        const uint32_t textureId = mImpl->selectedTextures[unit];
        if (textureId < mImpl->textures.size() && mImpl->textures[textureId].initialized) {
            mImpl->BindTexture(unit, &mImpl->textures[textureId].texture);
        }
    }
}

void GfxRenderingAPICitro3D::Init() {
    if (mImpl->initialized) {
        return;
    }
    gfxInit(GSP_BGR8_OES, GSP_BGR8_OES, false);

    bool useIntermediatePresentation = false;
    if (Mk64Graphics3DSResolvedNewModel != nullptr &&
        Mk64Graphics3DSResolvedOutputWidth != nullptr &&
        Mk64Graphics3DSUsesIntermediatePresentation != nullptr) {
        mImpl->newModel = Mk64Graphics3DSResolvedNewModel();
        mImpl->outputWidth = Mk64Graphics3DSResolvedOutputWidth();
        useIntermediatePresentation = Mk64Graphics3DSUsesIntermediatePresentation();
    } else {
        // Standalone renderer probes do not link the game runtime. Keep a
        // conservative fallback for them without affecting the real build's
        // single resolved configuration.
        bool aptNewModel = false;
        const bool aptModelKnown = R_SUCCEEDED(APT_CheckNew3DS(&aptNewModel));
        const bool reportedNewModel = Mk64Diagnostics3DSIsNewModel != nullptr &&
                                      Mk64Diagnostics3DSIsNewModel();
        mImpl->newModel = reportedNewModel || (aptModelKnown && aptNewModel);
        const uint32_t requestedWidth = Mk64Settings3DSGetResolutionWidth != nullptr
                                            ? Mk64Settings3DSGetResolutionWidth()
                                            : kTopLogicalWidth;
        const bool supportsWide = Mk64Diagnostics3DSSupportsWideMode != nullptr
                                      ? Mk64Diagnostics3DSSupportsWideMode()
                                      : mImpl->newModel;
        mImpl->outputWidth = requestedWidth == kTopWideWidth && supportsWide
                                 ? kTopWideWidth
                                 : kTopLogicalWidth;
        useIntermediatePresentation = mImpl->newModel && mImpl->outputWidth == kTopLogicalWidth;
    }
    if (mImpl->outputWidth != kTopWideWidth) mImpl->outputWidth = kTopLogicalWidth;

    gfxSet3D(false);
    gfxSetWide(mImpl->outputWidth == kTopWideWidth);
    gfxSetDoubleBuffering(GFX_BOTTOM, false);
    uint16_t bottomWidth = 0;
    uint16_t bottomHeight = 0;
    uint8_t* bottomFramebuffer = gfxGetFramebuffer(GFX_BOTTOM, GFX_LEFT, &bottomWidth, &bottomHeight);
    if (bottomFramebuffer != nullptr) {
        const size_t bottomBytes = static_cast<size_t>(bottomWidth) * bottomHeight * 3;
        std::memset(bottomFramebuffer, 0, bottomBytes);
        GSPGPU_FlushDataCache(bottomFramebuffer, bottomBytes);
    }
    aptSetHomeAllowed(true);
    aptSetSleepAllowed(true);

    if (mImpl->newModel) {
        osSetSpeedupEnable(true);
    }

    if (!C3D_Init(C3D_DEFAULT_CMDBUF_SIZE)) {
        // SoH-3DS: every bail-out below leaves ready==false, which StartFrame
        // silently honours - the symptom is a black screen and an empty log.
        std::fprintf(stderr, "soh-3ds gfx: INIT FAIL C3D_Init\n");
        gfxExit();
        return;
    }
    mImpl->initialized = true;
    if (Soh3dsSleepInit) Soh3dsSleepInit();
    // Keep the display target on Citro3D's proven RGBA8/depth-stencil path.
    // E2's 16-bit render target could leave the top-screen GPU queue stalled
    // while the independently rendered bottom screen remained visible.
    mImpl->topTarget = C3D_RenderTargetCreate(kTopHeight, mImpl->outputWidth, GPU_RB_RGBA8,
                                               GPU_RB_DEPTH24_STENCIL8);
    if (mImpl->topTarget == nullptr) {
        std::fprintf(stderr, "soh-3ds gfx: INIT FAIL topTarget (%lux%u)\n",
                     (unsigned long)mImpl->outputWidth, (unsigned)kTopHeight);
        return;
    }
    C3D_RenderTargetSetOutput(mImpl->topTarget, GFX_TOP, GFX_LEFT, kDisplayTransferFlags);
    // Dual screen: 320x240 RGBA8 + D16 (kaleido's pages use the Z buffer).
    // Rotated like the top target; SetViewport/projection already treat every
    // non-scene target that way. Output is attached lazily on first use so
    // the boot console keeps the LCD until the game actually draws there.
    mImpl->bottomTarget = C3D_RenderTargetCreate(kNativeHeight, kNativeWidth, GPU_RB_RGBA8, GPU_RB_DEPTH16);
    if (mImpl->bottomTarget == nullptr) {
        std::fprintf(stderr, "soh-3ds gfx: bottomTarget FAILED (vram free %lu KiB) - dual screen off\n",
                     (unsigned long)(vramSpaceFree() / 1024u));
    }
    RegisterBottomBridge();

    mImpl->shaderBinary = DVLB_ParseFile(reinterpret_cast<uint32_t*>(const_cast<uint8_t*>(fast3d_passthrough_shbin)),
                                         fast3d_passthrough_shbin_size);
    if (mImpl->shaderBinary == nullptr) {
        std::fprintf(stderr, "soh-3ds gfx: INIT FAIL DVLB_ParseFile\n");
        return;
    }
    shaderProgramInit(&mImpl->shaderProgram);
    shaderProgramSetVsh(&mImpl->shaderProgram, &mImpl->shaderBinary->DVLE[0]);
    C3D_BindProgram(&mImpl->shaderProgram);
    mImpl->projectionUniform = shaderInstanceGetUniformLocation(mImpl->shaderProgram.vertexShader, "projection");
    mImpl->packedVertices =
        static_cast<PackedVertex*>(linearAlloc(kVertexBufferCapacity * sizeof(PackedVertex)));
    if (mImpl->packedVertices == nullptr) {
        std::fprintf(stderr, "soh-3ds gfx: INIT FAIL linearAlloc %lu bytes\n",
                     (unsigned long)(kVertexBufferCapacity * sizeof(PackedVertex)));
        return;
    }
    C3D_AttrInfo* attributeInfo = C3D_GetAttrInfo();
    AttrInfo_Init(attributeInfo);
    AttrInfo_AddLoader(attributeInfo, 0, GPU_FLOAT, 4);
    AttrInfo_AddLoader(attributeInfo, 1, GPU_FLOAT, 2);
    AttrInfo_AddLoader(attributeInfo, 2, GPU_FLOAT, 2);
    AttrInfo_AddLoader(attributeInfo, 3, GPU_UNSIGNED_BYTE, 4);
    C3D_BufInfo* bufferInfo = C3D_GetBufInfo();
    BufInfo_Init(bufferInfo);
    BufInfo_Add(bufferInfo, mImpl->packedVertices, sizeof(PackedVertex), 4, 0x3210);
    C3D_CullFace(GPU_CULL_NONE);
    C3D_DepthMap(true, -1.0f, 0.0f);
    // EndFrame counts physical vblanks; a 30 Hz Citro3D counter would apply
    // our 60/target divisor twice on the non-intermediate display paths.
    C3D_FrameRate(60.0f);
    SetUseAlpha(false);
    SetDepthTestAndMask(false, false);
    mImpl->ready = true;
    std::fprintf(stderr, "soh-3ds gfx: INIT OK outputWidth=%lu newModel=%d intermediate=%d\n",
                 (unsigned long)mImpl->outputWidth, (int)mImpl->newModel,
                 (int)useIntermediatePresentation);
    // SoH-3DS: graphics are up but asset indexing keeps both screens dark for
    // minutes; hand the bottom screen to the port's boot-status console so a
    // slow boot is distinguishable from a hang. Weak: probe binaries that link
    // this backend without the port main simply skip it.
    if (Soh3dsBootConsoleReady != nullptr) {
        Soh3dsBootConsoleReady();
    }
}

void GfxRenderingAPICitro3D::OnResize() {
}

void GfxRenderingAPICitro3D::StartFrame() {
    // SoH-3DS: one-shot trace of the first frame. Zero heartbeats with a
    // successful Init means the first frame never completes; these say where.
    static unsigned sStarts = 0;
    const bool trace = ++sStarts <= 2;
    if (trace) {
        std::fprintf(stderr, "soh-3ds gfx: StartFrame#%u ready=%d active=%d\n", sStarts,
                     (int)mImpl->ready, (int)mImpl->frameActive);
    }
    if (!mImpl->ready || mImpl->frameActive) {
        return;
    }

    const uint8_t requestedScale = Mk64Settings3DSGetRenderScalePercent != nullptr
                                       ? Mk64Settings3DSGetRenderScalePercent()
                                       : 100;
    mImpl->renderScalePercent = requestedScale == 50 || requestedScale == 75
                                    ? requestedScale
                                    : 100;
    const int requestedFilter = Mk64Settings3DSGetDisplayFilter != nullptr
                                    ? Mk64Settings3DSGetDisplayFilter()
                                    : DisplayFilterBilinear;
    mImpl->displayFilter = requestedFilter >= DisplayFilterBilinear &&
                                   requestedFilter <= DisplayFilterCrt
                               ? requestedFilter
                               : DisplayFilterBilinear;
    const bool presentationRequested = mImpl->renderScalePercent != 100 ||
                                       mImpl->displayFilter != DisplayFilterBilinear;
    mImpl->postprocessActive = presentationRequested && EnsurePresentationResources();
    const uint32_t previousWidth = mImpl->renderWidth;
    const uint32_t previousHeight = mImpl->renderHeight;
    const auto* previousTarget = mImpl->gameTarget;
    mImpl->renderWidth = mImpl->postprocessActive
                             ? ScaledDimension(mImpl->outputWidth, mImpl->renderScalePercent)
                             : mImpl->outputWidth;
    mImpl->renderHeight = mImpl->postprocessActive
                              ? ScaledDimension(kTopHeight, mImpl->renderScalePercent)
                              : kTopHeight;
    mImpl->gameTarget = mImpl->postprocessActive ? mImpl->sceneTarget : mImpl->topTarget;
    if (previousTarget != mImpl->gameTarget || previousWidth != mImpl->renderWidth ||
        previousHeight != mImpl->renderHeight) {
        // A target/scale switch has no completed depth image in the new
        // coordinate system yet. Do not sample stale dimensions/storage.
        mImpl->depthSnapshot.Reset();
    }
    if (trace) {
        std::fprintf(stderr, "soh-3ds gfx: pre-FrameBegin postproc=%d target=%p %lux%lu\n",
                     (int)mImpl->postprocessActive, (const void*)mImpl->gameTarget,
                     (unsigned long)mImpl->renderWidth, (unsigned long)mImpl->renderHeight);
    }
    // Drain the previous GX work before reusing its resources, then allow
    // CPU command construction to overlap the pending LCD swap. FrameSplit
    // only queues commands; EndFrame guards the LCD transfers before running
    // the new queue. Waiting for vblank here needlessly stalls the rest of
    // the game tick, which begins this frame from its depth-probe callback.
    {
        const uint64_t now = svcGetSystemTick();
        if (mImpl->frameEndTick != 0) {
            mImpl->loopTickAccumulator += now - mImpl->frameEndTick;
            mImpl->frameLoopTicks += now - mImpl->frameEndTick;
        }
        mImpl->frameEndTick = 0; // do not recount on a failed FrameBegin retry
    }
    const uint64_t beginWaitStart = svcGetSystemTick();
    if (!C3D_FrameBegin(0)) {
        // SoH-3DS: leaves frameActive false, so EndFrame returns immediately
        // and nothing is ever presented. Silent black screen otherwise.
        static unsigned sBeginFailures = 0;
        if (++sBeginFailures <= 3) {
            std::fprintf(stderr, "soh-3ds gfx: C3D_FrameBegin FAILED (%u)\n", sBeginFailures);
        }
        return;
    }
    mImpl->frameStartTick = svcGetSystemTick();
    mImpl->frameBeginWaitTicks = mImpl->frameStartTick - beginWaitStart;
    mImpl->waitTickAccumulator += mImpl->frameBeginWaitTicks;
    sSwapReadyVblank = C3D_FrameCounter(0) + 1;
    if (trace) {
        std::fprintf(stderr, "soh-3ds gfx: post-FrameBegin ok\n");
    }
    // C3D_FrameBegin waited for the previous frame's GX queue to drain, so
    // storage retired during it is no longer referenced.
    for (auto& texture : mImpl->retiredTextures) {
        C3D_TexDelete(&texture);
    }
    mImpl->retiredTextures.clear();
    // ...and the depth buffer is the previous frame's final depth.
    ResolveDepthProbe();
    ++mImpl->frameOrdinal;
    mImpl->frameActive = true;
    mImpl->activeTarget = mImpl->gameTarget;
    mImpl->scenePresented = false;
    mImpl->packedVertexCount = 0;
    mImpl->framePeakPackedVertices = 0;
    mImpl->dirtyVertexBegin = 0;
    mImpl->dirtyVertexEnd = 0;
    mImpl->viewportX = 0;
    mImpl->viewportY = 0;
    mImpl->viewportWidth = static_cast<int>(mImpl->renderWidth);
    mImpl->viewportHeight = static_cast<int>(mImpl->renderHeight);
    mImpl->scissorEnabled = false;
    mImpl->externalLinearBuffersDirty = false;
    mImpl->originalAspect = Mk64Settings3DSGetAspectRatio != nullptr &&
                            Mk64Settings3DSGetAspectRatio() != 0;
    C3D_FrameDrawOn(mImpl->gameTarget);
    RestoreFast3DState();
}

// Same primitive as Citro3D's HOME suspend handler, but never clear the
// queue of an open frame: FrameBegin has already drained the previous frame,
// and FrameSplit may have queued CPU-built commands that must survive sleep.
extern "C" void C3Di_RenderQueueWaitDone();
extern "C" void Soh3dsGraphicsSleep() {
    if (sActiveFrame != nullptr && !*sActiveFrame) C3Di_RenderQueueWaitDone();
}

extern "C" void Soh3dsGraphicsWake() {
    sPaceVblank = 0;
    sPacePeriod = 0;
    sSwapReadyVblank = C3D_FrameCounter(0) + 1;
}

void GfxRenderingAPICitro3D::EndFrame() {
    if (!mImpl->frameActive) {
        return;
    }
    uint64_t pacingWait = 0;
    // Fast3D flushes its exact VBO and texture ranges. Citro2D owns private
    // linear vertex/index buffers, so only frames that actually submit a C2D
    // batch need Citro3D's broad linear-heap coherency pass.
    PresentSceneToTopTarget();
    FlushPackedVertices();
    // Dual screen: a target that is not drawn on is not transferred, so the
    // LCD would keep the last pause page after unpausing. Draw-on + clear it
    // once on the transition; it then stays black without per-frame cost.
    if (mImpl->bottomDrawnLastFrame && !mImpl->bottomDrawnThisFrame && mImpl->bottomTarget != nullptr) {
        ++mImpl->sampleFrameSplitCount;
        C3D_FrameSplit(GX_CMDLIST_FLUSH);
        C3D_FrameDrawOn(mImpl->bottomTarget);
        C3D_RenderTargetClear(mImpl->bottomTarget, C3D_CLEAR_ALL, 0x000000FF, 0);
    }
    mImpl->bottomDrawnLastFrame = mImpl->bottomDrawnThisFrame;
    mImpl->bottomDrawnThisFrame = false;

    // SoH-3DS: game-loop pacing. Nothing else limits the loop - the desktop
    // backends sleep to the target in SwapBuffersEnd, this one does not - and
    // citro3d's own vsync is exactly one vblank, so a cheap frame ran the 20 Hz
    // game at 60 ticks/s (title screen measured 59 fps = 3x speed) and gameplay
    // speed tracked render load.
    //
    // Each frame owns 60/target vblanks of an ABSOLUTE schedule of top-LCD
    // vblanks (C3D_FrameCounter(0), citro3d's vblank interrupt tally): target
    // 20 -> 3, 30 -> 2 (SoH draws an interpolated frame between ticks), 60 ->
    // 1. Absolute, not "since the previous frame": relative pacing turned
    // every overrun into lost game time (Link walking in slow motion). A late
    // frame presents immediately, the schedule stands, and the game side
    // drops the next interpolated frame while `behind` (Soh3dsFrameBehind),
    // so overdue intermediate frames can be dropped. Preserve up to 100 ms
    // of debt: at 60 FPS, rebasing after just one 16.7 ms period erased the
    // time that skipping was meant to recover and slowed 20 Hz gameplay to
    // 15 Hz. Longer scene-load/suspend stalls still resync without a burst
    // of fast-forward game updates.
    //
    // The wait sits here, not before FrameBegin, because the game's depth
    // probe (GetPixelDepth) begins the frame mid-tick: pacing there stalled the
    // rest of the tick - Play_Draw, interpolation record, DL build - behind
    // the slot. FrameEnd queues the GPU work; the swap lands on the vblank
    // after it finishes, so submit one vblank before the slot.
    {
        const int target = Soh3dsTargetFps != nullptr ? Soh3dsTargetFps() : 60;
        const uint32_t period = target > 0 && target < 60 ? 60u / target : 1u;
        const uint32_t now = C3D_FrameCounter(0);
        if (sPaceVblank == 0 || static_cast<int32_t>(now - (sPaceVblank + kMaxPacingDebtVblanks)) > 0 ||
            sPacePeriod != period) {
            sPaceVblank = now + 1; // first frame, stall, or rate change
        }
        sPacePeriod = period;
        const uint64_t waitStart = svcGetSystemTick();
        while (static_cast<int32_t>(C3D_FrameCounter(0) + 1 - sPaceVblank) < 0) {
            gspWaitForAnyEvent();
        }
        pacingWait = svcGetSystemTick() - waitStart;
        sPaceVblank += period;
    }
    // The GSP event thread publishes buffer swaps in onQueueFinish. Its next
    // top-vblank callback fences that publication, even if FrameBegin saw the
    // queue's isRunning bit clear before the callback finished. Pending bits
    // then confirm actual LCD consumption, including the bottom clear after
    // closing a menu. A counter captured at submission cannot prove this:
    // GPU completion may occur after that submission's next vblank.
    // Do not require an unused bottom LCD counter to advance; the two screens
    // drift. All CPU-built commands still wait in the stopped GX queue here.
    {
        const uint64_t waitStart = svcGetSystemTick();
        while (static_cast<int32_t>(C3D_FrameCounter(0) - sSwapReadyVblank) < 0 ||
               gspIsPresentPending(0) || gspIsPresentPending(1)) {
            gspWaitForAnyEvent();
        }
        pacingWait += svcGetSystemTick() - waitStart;
    }
    const bool needsLinearHeapFlush = mImpl->externalLinearBuffersDirty;
    C3D_FrameEnd(needsLinearHeapFlush ? 0 : GX_CMDLIST_FLUSH);
    // Measure the whole submission interval, excluding only its own wait.
    // This includes presentation, flush and submission without erasing work
    // accumulated from earlier frames.
    const uint64_t submissionTick = svcGetSystemTick();
    const uint64_t renderTicks = submissionTick - mImpl->frameStartTick - pacingWait;
    mImpl->busyTickAccumulator += renderTicks;
    mImpl->waitTickAccumulator += pacingWait;
    if (needsLinearHeapFlush) ++mImpl->linearHeapFlushFrameCount;

    // SoH-3DS DIAGNOSTIC: opt-in, fixed-memory submission capture for hitches.
    // Keep SD write cost visible in the following interval. Up to 59 trailing
    // samples remain in RAM on exit; complete batches are flushed to disk.
    static FILE* frameTraceFile = nullptr;
    static Soh3dsFrameTrace frameTrace(nullptr);
    static bool wasFrameTraceEnabled = false;
    const bool frameTraceEnabled = Soh3dsLoggingEnabled(SOH3DS_LOG_FRAMES);
    if (frameTraceEnabled != wasFrameTraceEnabled) {
        if (frameTraceFile) std::fclose(frameTraceFile);
        frameTraceFile = frameTraceEnabled ? std::fopen("frametimes.bin", "wb") : nullptr;
        frameTrace = Soh3dsFrameTrace(frameTraceFile);
        wasFrameTraceEnabled = frameTraceEnabled;
    }
    static uint64_t submissionOrdinal = 0;
    static uint64_t previousTraceTicks = 0;
    ++submissionOrdinal;
    if (frameTrace.Enabled()) {
        const uint64_t traceStart = svcGetSystemTick();
        frameTrace.Record({submissionOrdinal, submissionTick, renderTicks, mImpl->frameBeginWaitTicks,
                           pacingWait, mImpl->frameLoopTicks, mImpl->textureCacheUploadCount, sFramesDropped,
                           Soh3dsGameTicks != nullptr ? Soh3dsGameTicks() : 0,
                           static_cast<uint64_t>(Soh3dsTargetFps != nullptr ? Soh3dsTargetFps() : 60),
                           C3D_FrameCounter(0), previousTraceTicks});
        previousTraceTicks = svcGetSystemTick() - traceStart;
    }
    mImpl->frameLoopTicks = 0;

    // SoH-3DS DIAGNOSTIC: dump the presented top screen once, at a fixed
    // frame, so rendering bugs can be inspected as real pixels instead of
    // guessed at. Works on hardware and in the emulator; no desktop capture.
    // The 3DS framebuffer is column-major (rotated 90 deg) - the reader
    // de-rotates. Enabled by planting fbdump.flag next to the o2r.
    {
        static int sFbFrame = 0;
        static int sFbArmed = -1;
        if (sFbArmed < 0) {
            FILE* flag = fopen("fbdump.flag", "r");
            sFbArmed = (flag != nullptr);
            if (flag != nullptr) {
                fclose(flag);
            }
        }
        // Dump a series so a black transition frame cannot waste the run.
        ++sFbFrame;
        if (Soh3dsLoggingEnabled(SOH3DS_LOG_GENERAL) && sFbArmed == 1 && sFbFrame % 240 == 0 && sFbFrame <= 2400) {
            u16 fbw = 0, fbh = 0;
            u8* fb = gfxGetFramebuffer(GFX_TOP, GFX_LEFT, &fbw, &fbh);
            if (fb != nullptr) {
                char name[32];
                std::snprintf(name, sizeof(name), "fbdump%d.bin", sFbFrame / 240);
                FILE* out = fopen(name, "wb");
                if (out != nullptr) {
                    // gfxGetFramebuffer reports the ROTATED buffer: fbw is the
                    // screen's height in pixels. Dump raw BGR8 plus dimensions.
                    fwrite(&fbw, 1, sizeof(fbw), out);
                    fwrite(&fbh, 1, sizeof(fbh), out);
                    fwrite(fb, 1, (size_t)fbw * fbh * 3, out);
                    fclose(out);
                }
                std::fprintf(stderr, "soh-3ds gfx: fbdump%d %ux%u scene=%s\n", sFbFrame / 240, (unsigned)fbw,
                             (unsigned)fbh,
                             Soh3dsCurrentSceneName != nullptr ? Soh3dsCurrentSceneName() : "unlinked");
            }
        }
    }
    mImpl->frameActive = false;
    mImpl->activeTarget = nullptr;

    // Deferred framebuffer releases (see pendingFramebufferReleases). The frame
    // is closed, so deletion is legal again.
    for (int fbId : mImpl->pendingFramebufferReleases) {
        ReleaseFramebufferStorage(fbId);
    }
    mImpl->pendingFramebufferReleases.clear();

    const uint64_t timestamp = osGetTime();
    if (mImpl->presentedTimestampCount == 0) {
        mImpl->firstPresentedTimestamp = timestamp;
    }
    mImpl->presentedTimestamps[mImpl->presentedTimestampHead] = timestamp;
    mImpl->presentedTimestampHead =
        (mImpl->presentedTimestampHead + 1) % mImpl->presentedTimestamps.size();
    mImpl->presentedTimestampCount =
        std::min(mImpl->presentedTimestampCount + 1, mImpl->presentedTimestamps.size());

    // SoH-3DS: periodic performance record. One fixed-buffer, non-allocating
    // line per 60 presented frames carries the CPU/GPU/command/texture/vertex
    // and memory metrics the measured optimization work compares against.
    static unsigned sFrames = 0;
    FILE* performanceLog = Soh3dsPerformanceLog();
    if (++sFrames % 60 == 0 && performanceLog != nullptr) {
        // SYSCLOCK_ARM11 = 268,111,856 ticks/s -> ticks per microsecond.
        const uint64_t busyMicroseconds = mImpl->busyTickAccumulator / 268u;
        mImpl->busyTickAccumulator = 0;
        const uint64_t waitMicroseconds = mImpl->waitTickAccumulator / 268u;
        mImpl->waitTickAccumulator = 0;
        const uint64_t loopMicroseconds = mImpl->loopTickAccumulator / 268u;
        mImpl->loopTickAccumulator = 0;
        // Game ticks per second over this window (wall clock, so a scene-load
        // stall shows as a real dip). 20.0 in play means Link moves at speed.
        static uint64_t sLastHeartbeatMs = 0;
        static uint32_t sLastGameTicks = 0;
        const uint32_t gameTicks = Soh3dsGameTicks != nullptr ? Soh3dsGameTicks() : 0;
        const uint64_t windowMs = sLastHeartbeatMs != 0 ? timestamp - sLastHeartbeatMs : 0;
        const uint32_t tpsTenths =
            windowMs != 0 ? static_cast<uint32_t>((uint64_t)(gameTicks - sLastGameTicks) * 10000u / windowMs) : 0;
        sLastHeartbeatMs = timestamp;
        sLastGameTicks = gameTicks;
        // mallinfo: OOM diagnoses need the growth curve, not just the corpse.
        struct mallinfo mi = mallinfo();
        const unsigned catches = Soh3dsResourceCatchCount != nullptr ? Soh3dsResourceCatchCount() : 0u;
        // Real APPLICATION-region numbers. Every heap decision in this port
        // has been argued against a DERIVED ceiling (exheader SystemModeExt
        // minus image minus the linear reservation); measure it instead. Both
        // calls are cheap and this runs once per 60 frames.
        // Hardware measurement (perf-hw-5650ffd5): the APPLICATION region is
        // 124 MB as the exheader asks, but osGetMemRegionFree sits at a
        // constant 2 MB because libctru reserves the region at startup - so it
        // is useless as headroom. mallinfo().arena is only what sbrk has
        // touched so far, and fordblks the free bytes inside that, so neither
        // is the ceiling either: sbrk keeps growing the arena until it hits
        // libctru's reservation. envGetHeapSize() IS that reservation, so
        // cap - uordblks is the real distance to a bad_alloc.
        const unsigned heapCapKiB = (unsigned)(envGetHeapSize() / 1024u);
        unsigned resFree = 0;
        const unsigned resCount = Soh3dsResourceCacheSize != nullptr ? Soh3dsResourceCacheSize(&resFree) : 0u;

        size_t initializedTextures = 0;
        size_t textureBytes = 0;
        for (const auto& slot : mImpl->textures) {
            if (!slot.initialized) {
                continue;
            }
            ++initializedTextures;
            textureBytes += slot.allocatedBytes;
        }

        const uint64_t drawDelta = mImpl->drawCallCount - mImpl->lastSampleDrawCallCount;
        const uint64_t triangleDelta = mImpl->triangleCount - mImpl->lastSampleTriangleCount;
        const uint64_t uploadDelta = mImpl->textureCacheUploadCount - mImpl->lastSampleTextureUploadCount;
        const uint64_t uploadByteDelta = mImpl->textureCacheUploadBytes - mImpl->lastSampleTextureUploadBytes;

        const uint32_t fps2Tenths = PositiveTenths(GetPresentedFps2Seconds());
        const uint32_t fps10Tenths = PositiveTenths(GetPresentedFps10Seconds());
        const uint32_t processingHundredths = PositiveHundredths(C3D_GetProcessingTime());
        const uint32_t drawingHundredths = PositiveHundredths(C3D_GetDrawingTime());
        const uint32_t commandPermille = PositiveTenths(std::max(0.0f, C3D_GetCmdBufUsage()) * 100.0f);
        uint32_t tickMs[3];
        for (int i = 0; i < 3; ++i) {
            tickMs[i] = static_cast<uint32_t>(gSoh3dsTickPhaseTicks[i] / 268112u);
            gSoh3dsTickPhaseTicks[i] = 0;
        }

        char record[512];
        const int recordLength = std::snprintf(
            record, sizeof(record),
            "soh-3ds perf: frame=%u fps2=%lu.%lu fps10=%lu.%lu tps=%lu.%lu cpu60=%lums wait60=%lums loop60=%lums "
            "tick=%lu/%lu/%lums dropped=%lu "
            "gpuProc=%lu.%02lums gpuDraw=%lu.%02lums cmd=%lu.%01lu%% "
            "draws=%lu fog=%lu fold=%lu depthQ=%lu tris=%lu splits=%lu texLive=%lu texKiB=%lu "
            "texUploads=%lu uploadKiB=%lu vtxPeak=%lu heap=%uKiB/%uKiB lin=%uKiB free=%uKiB res=%u/%u catches=%u\n",
            sFrames,
            (unsigned long)(fps2Tenths / 10u), (unsigned long)(fps2Tenths % 10u),
            (unsigned long)(fps10Tenths / 10u), (unsigned long)(fps10Tenths % 10u),
            (unsigned long)(tpsTenths / 10u), (unsigned long)(tpsTenths % 10u),
            (unsigned long)(busyMicroseconds / 1000u), (unsigned long)(waitMicroseconds / 1000u),
            (unsigned long)(loopMicroseconds / 1000u),
            (unsigned long)tickMs[0], (unsigned long)tickMs[1], (unsigned long)tickMs[2],
            (unsigned long)sFramesDropped,
            (unsigned long)(processingHundredths / 100u), (unsigned long)(processingHundredths % 100u),
            (unsigned long)(drawingHundredths / 100u), (unsigned long)(drawingHundredths % 100u),
            (unsigned long)(commandPermille / 10u), (unsigned long)(commandPermille % 10u),
            (unsigned long)drawDelta, (unsigned long)mImpl->sampleFogDrawCount, (unsigned long)gFoldedPlanCount,
            (unsigned long)mImpl->sampleDepthQueryCount, (unsigned long)triangleDelta,
            (unsigned long)mImpl->sampleFrameSplitCount,
            (unsigned long)initializedTextures, (unsigned long)(textureBytes / 1024u),
            (unsigned long)uploadDelta, (unsigned long)(uploadByteDelta / 1024u),
            (unsigned long)mImpl->samplePeakPackedVertices,
            (unsigned)(mi.uordblks / 1024u), heapCapKiB, (unsigned)(linearSpaceFree() / 1024u),
            (unsigned)((envGetHeapSize() - (unsigned)mi.uordblks) / 1024u), resCount, resFree,
            catches);
        if (recordLength > 0) {
            std::fputs(record, stderr);
        }

        // Basic FPS capture is independent of detailed per-draw profiling.
        // With no capture flag, normal runs perform no diagnostic file I/O.
        FILE* sPerformanceLog = performanceLog;
        static unsigned sPerformanceLogSamples = 0;
        if (sPerformanceLog != nullptr && recordLength > 0) {
            std::fputs(record, sPerformanceLog);
            // This labels the scene at the end of the window; it does not
            // classify every frame in a window that crossed a scene change.
            const char* scene = Soh3dsCurrentSceneName != nullptr ? Soh3dsCurrentSceneName() : "unlinked";
            char context[160];
            std::snprintf(context, sizeof(context),
                          "soh-3ds context: frame=%u target=%d profile=%u sceneEnd=%.80s\n",
                          sFrames, Soh3dsTargetFps != nullptr ? Soh3dsTargetFps() : 60,
                          static_cast<unsigned>(sRenderProfileEnabled), scene != nullptr ? scene : "unknown");
            std::fputs(context, sPerformanceLog);
            if (++sPerformanceLogSamples % 10u == 0u) {
                std::fflush(sPerformanceLog);
            }
        }

        if (sRenderProfileEnabled) {
            static constexpr const char* labels[] = {
                "dl", "draw", "pack", "state", "depth", "interp", "vertex", "triangle", "tristate", "texture",
                "trikey", "emit"
            };
            static_assert(std::size(labels) == static_cast<unsigned>(Soh3dsProfileSection::Count));
            char detail[1024];
            size_t used = std::snprintf(detail, sizeof(detail), "soh-3ds profile: frame=%u units=us/calls/samples", sFrames);
            for (size_t i = 0; i < sRenderProfile.size(); ++i) {
                auto& counter = sRenderProfile[i];
                if (used < sizeof(detail)) {
                    const int n = std::snprintf(detail + used, sizeof(detail) - used, " %s=%llu/%lu/%lu", labels[i],
                        static_cast<unsigned long long>(counter.ticks / 268u),
                        static_cast<unsigned long>(counter.calls), static_cast<unsigned long>(counter.samples));
                    if (n > 0) used += static_cast<size_t>(n);
                }
                counter.ticks = counter.calls = counter.samples = 0;
            }
            if (used + 1 < sizeof(detail)) {
                detail[used++] = '\n';
                detail[used] = '\0';
                std::fputs(detail, stderr);
                if (sPerformanceLog != nullptr) std::fputs(detail, sPerformanceLog);
            }
            char coverage[192];
            std::snprintf(coverage, sizeof(coverage),
                          "soh-3ds packmix: frame=%u units=batches/vertices common=%lu/%lu total=%lu/%lu\n", sFrames,
                          static_cast<unsigned long>(sPackCoverage.commonBatches),
                          static_cast<unsigned long>(sPackCoverage.commonVertices),
                          static_cast<unsigned long>(sPackCoverage.totalBatches),
                          static_cast<unsigned long>(sPackCoverage.totalVertices));
            std::fputs(coverage, stderr);
            if (sPerformanceLog != nullptr) std::fputs(coverage, sPerformanceLog);
            sPackCoverage = {};
        }

        // Every 10th sample, name what holds the cache. One full cache walk
        // (~2k lines) per 10 seconds, and only while tracing to a file: the
        // `res=` totals alone cannot say which prefix is growing.
        if (sPerformanceLog != nullptr && sPerformanceLogSamples % 10u == 0u &&
            Soh3dsResourceCacheReport != nullptr) {
            char buckets[320];
            Soh3dsResourceCacheReport(buckets, sizeof(buckets));
            char line[400];
            const int length = std::snprintf(line, sizeof(line), "soh-3ds cache: frame=%lu %s\n",
                                             (unsigned long)sFrames, buckets);
            if (length > 0) {
                std::fputs(line, sPerformanceLog);
                std::fputs(line, stderr);
            }
        }

        mImpl->lastSampleDrawCallCount = mImpl->drawCallCount;
        mImpl->lastSampleTriangleCount = mImpl->triangleCount;
        mImpl->lastSampleTextureUploadCount = mImpl->textureCacheUploadCount;
        mImpl->lastSampleTextureUploadBytes = mImpl->textureCacheUploadBytes;
        mImpl->sampleFrameSplitCount = 0;
        mImpl->samplePeakPackedVertices = 0;
        mImpl->sampleFogDrawCount = 0;
        mImpl->sampleDepthQueryCount = 0;
        gFoldedPlanCount = 0;
    }
    mImpl->frameEndTick = svcGetSystemTick();
}

void GfxRenderingAPICitro3D::FinishRender() {
    // EndFrame already submitted and closed the Citro3D frame. FrameSplit is
    // only valid while a frame is being recorded; issuing it here can wait on
    // a command queue that no longer has an active frame and hard-lock the
    // first presentation on real hardware.
}

int GfxRenderingAPICitro3D::CreateFramebuffer() {
    mImpl->framebuffers.emplace_back(std::make_unique<Impl::FramebufferSlot>());
    return static_cast<int>(mImpl->framebuffers.size() - 1);
}

void GfxRenderingAPICitro3D::ReleaseFramebufferStorage(int fbId) {
    auto& slot = *mImpl->framebuffers[fbId];
    slot.depthSnapshot.Reset();
    if (slot.target != nullptr) {
        // Callers guarantee !frameActive: citro3d panics on in-frame deletion.
        C3D_RenderTargetDelete(slot.target);
        slot.target = nullptr;
    }
    if (slot.initialized) {
        // Even between frames the GPU may still be executing the last command
        // list that sampled this texture; retiredTextures is freed after the
        // next C3D_FrameBegin has drained the GX queue.
        mImpl->retiredTextures.push_back(slot.texture);
        slot.initialized = false;
    }
}

bool GfxRenderingAPICitro3D::EnsureFramebufferStorage(int fbId, uint16_t contentWidth, uint16_t contentHeight,
                                                      bool rotated, bool needTarget) {
    auto& slot = *mImpl->framebuffers[fbId];
    const uint16_t textureWidth = NextPowerOfTwo(rotated ? contentHeight : contentWidth);
    const uint16_t textureHeight = NextPowerOfTwo(rotated ? contentWidth : contentHeight);
    const bool storageMatches = slot.initialized && slot.rotated == rotated && slot.texture.width == textureWidth &&
                                slot.texture.height == textureHeight;
    if (!storageMatches) {
        if (slot.target != nullptr && mImpl->frameActive) {
            // Cannot delete the old target mid-frame; the pending update frees
            // the storage at EndFrame and the next use reallocates it.
            mImpl->pendingFramebufferReleases.push_back(fbId);
            return false;
        }
        ReleaseFramebufferStorage(fbId);
        if (!C3D_TexInitVRAM(&slot.texture, textureWidth, textureHeight, GPU_RGBA8)) {
            std::fprintf(stderr, "soh-3ds gfx: framebuffer %d storage %ux%u FAILED (vram free %lu KiB)\n", fbId,
                         (unsigned)textureWidth, (unsigned)textureHeight, (unsigned long)(vramSpaceFree() / 1024u));
            return false;
        }
        C3D_TexSetFilter(&slot.texture, GPU_LINEAR, GPU_LINEAR);
        C3D_TexSetWrap(&slot.texture, GPU_CLAMP_TO_EDGE, GPU_CLAMP_TO_EDGE);
        slot.initialized = true;
        slot.rotated = rotated;
        slot.needsClear = true;
    }
    slot.contentWidth = contentWidth;
    slot.contentHeight = contentHeight;
    // A tilted viewport occupies the final contentWidth memory rows after
    // PICA inverts rasterizer Y across the full POT backing. GX_TextureCopy
    // starts at row zero. Refresh even when reusing storage for a new purpose.
    slot.contentOffsetY = rotated && needTarget ? textureHeight - contentWidth : 0;
    if (needTarget && slot.target == nullptr) {
        slot.target = C3D_RenderTargetCreateFromTex(&slot.texture, GPU_TEXFACE_2D, 0,
                                                    slot.wantsDepth ? C3D_DEPTHTYPE(GPU_RB_DEPTH24_STENCIL8)
                                                                    : C3D_DEPTHTYPE(-1));
        if (slot.target == nullptr) {
            std::fprintf(stderr, "soh-3ds gfx: framebuffer %d target FAILED (vram free %lu KiB)\n", fbId,
                         (unsigned long)(vramSpaceFree() / 1024u));
            return false;
        }
        slot.needsClear = true;
    }
    return true;
}

void GfxRenderingAPICitro3D::UpdateFramebufferParameters(int fbId, uint32_t width, uint32_t height,
                                                          uint32_t msaaLevel, bool openglInvertY, bool renderTarget,
                                                          bool hasDepthBuffer, bool canExtractDepth) {
    if (fbId <= 0 || fbId >= static_cast<int>(mImpl->framebuffers.size()) || width == 0 || height == 0 ||
        width > kMaxTextureSize || height > kMaxTextureSize) {
        return;
    }
    (void) msaaLevel;
    (void) openglInvertY;
    (void) renderTarget;
    (void) canExtractDepth;

    auto& slot = *mImpl->framebuffers[fbId];
    const bool changed = slot.logicalWidth != width || slot.logicalHeight != height || slot.wantsDepth != hasDepthBuffer;
    slot.logicalWidth = static_cast<uint16_t>(width);
    slot.logicalHeight = static_cast<uint16_t>(height);
    slot.wantsDepth = hasDepthBuffer;
    if (!changed || !slot.initialized) {
        return; // storage is allocated on first draw/copy
    }
    if (mImpl->frameActive) {
        // In-frame delete is forbidden (see PendingFramebufferUpdate). Keep the
        // old storage one more frame; release at EndFrame.
        mImpl->pendingFramebufferReleases.push_back(fbId);
        return;
    }
    ReleaseFramebufferStorage(fbId);
}

void GfxRenderingAPICitro3D::StartDrawToFramebuffer(int fbId, float noiseScale) {
    (void) noiseScale; // noise is unreachable in OoT (no G_AC_DITHER, no live NOISE combiner)
    if (!mImpl->frameActive) {
        return;
    }
    C3D_RenderTarget* target = mImpl->gameTarget;
    Impl::FramebufferSlot* slot = nullptr;
    bool clearBottom = false;
    if (fbId > 0 && (fbId == mImpl->bottomScreenFb || fbId == mImpl->bottomScreenZoomFb) &&
        mImpl->bottomTarget != nullptr) {
        mImpl->bottomZoomActive = fbId == mImpl->bottomScreenZoomFb;
        // Dual screen: this id IS the bottom LCD. First use takes the screen
        // from the boot console (format back to BGR8, stdout off the LCD).
        if (!mImpl->bottomConsoleReleased) {
            mImpl->bottomConsoleReleased = true;
            if (Soh3dsBootConsoleRelease != nullptr) {
                Soh3dsBootConsoleRelease();
            }
            gfxSetScreenFormat(GFX_BOTTOM, GSP_BGR8_OES);
            C3D_RenderTargetSetOutput(mImpl->bottomTarget, GFX_BOTTOM, GFX_LEFT, kDisplayTransferFlags);
        }
        target = mImpl->bottomTarget;
        clearBottom = !mImpl->bottomDrawnThisFrame;
        mImpl->bottomDrawnThisFrame = true;
    } else if (fbId > 0 && fbId < static_cast<int>(mImpl->framebuffers.size()) &&
               mImpl->framebuffers[fbId] != nullptr && mImpl->framebuffers[fbId]->logicalWidth != 0) {
        slot = mImpl->framebuffers[fbId].get();
        // Offscreen draws go through the same tilt/viewport swap as the top
        // screen, so the storage is rotated.
        if (EnsureFramebufferStorage(fbId, slot->logicalWidth, slot->logicalHeight, true, true)) {
            target = slot->target;
        } else {
            slot = nullptr; // fall back to the game target rather than drop the draws
        }
    }
    if (mImpl->activeTarget != target) {
        FlushPackedVertices();
        ++mImpl->sampleFrameSplitCount;
        C3D_FrameSplit(GX_CMDLIST_FLUSH);
    }
    C3D_FrameDrawOn(target);
    mImpl->activeTarget = target;
    UploadProjectionForActiveTarget();
    if (clearBottom) {
        C3D_RenderTargetClear(target, C3D_CLEAR_ALL, 0x000000FF, 0);
    }
    if (slot != nullptr && slot->needsClear) {
        C3D_RenderTargetClear(target, slot->target->frameBuf.depthBuf != nullptr ? C3D_CLEAR_ALL : C3D_CLEAR_COLOR,
                              0x000000FF, 0);
        slot->needsClear = false;
    }
}

void GfxRenderingAPICitro3D::CopyFramebuffer(int fbDstId, int fbSrcId, int srcX0, int srcY0, int srcX1, int srcY1,
                                              int dstX0, int dstY0, int dstX1, int dstY1) {
    // OoT only copies the whole game image (pause backdrop, vismono, picto
    // capture): the rectangles are always full-frame, so this is a raw
    // tiled-to-tiled GX_TextureCopy of the current game target into the slot's
    // texture, laid out exactly like the source (rotated for the top screen).
    // ponytail: sub-rectangles and fb-to-fb copies are not needed by OoT.
    (void) srcX0; (void) srcY0; (void) srcX1; (void) srcY1;
    (void) dstX0; (void) dstY0; (void) dstX1; (void) dstY1;
    if (!mImpl->frameActive || fbSrcId != 0 || fbDstId <= 0 || fbDstId >= static_cast<int>(mImpl->framebuffers.size()) ||
        mImpl->framebuffers[fbDstId] == nullptr) {
        return;
    }
    C3D_RenderTarget* source = mImpl->gameTarget;
    if (source == nullptr || source->frameBuf.colorBuf == nullptr || source->frameBuf.colorFmt != GPU_RB_RGBA8) {
        return;
    }
    const bool rotated = source != mImpl->sceneTarget;
    // Content dims in upright pixels; the memory layout is the source's.
    const uint16_t contentWidth = static_cast<uint16_t>(rotated ? source->frameBuf.height : mImpl->renderWidth);
    const uint16_t contentHeight = static_cast<uint16_t>(rotated ? source->frameBuf.width : mImpl->renderHeight);
    if (!EnsureFramebufferStorage(fbDstId, contentWidth, contentHeight, rotated, false)) {
        return;
    }
    auto& slot = *mImpl->framebuffers[fbDstId];
    // Tile rows: the source is (width) texels wide = width/8 tiles of 256 B;
    // the POT destination row is texture.width/8 tiles. TextureCopy takes the
    // line size and the destination gap in 16-byte units.
    const uint32_t sourceWidth = source->frameBuf.width;
    const uint32_t sourceHeight = source->frameBuf.height;
    const uint32_t lineBytes = sourceWidth * 8U * 4U;                          // one 8-row tile strip
    const uint32_t gapBytes = (slot.texture.width - sourceWidth) * 8U * 4U;
    const uint32_t sizeBytes = sourceWidth * sourceHeight * 4U;
    // The copy must observe every draw recorded so far this frame: split the
    // command list so the GX queue orders it after them (queue runs at EndFrame).
    FlushPackedVertices();
    ++mImpl->sampleFrameSplitCount;
    C3D_FrameSplit(GX_CMDLIST_FLUSH);
    GX_TextureCopy(static_cast<u32*>(source->frameBuf.colorBuf), GX_BUFFER_DIM(lineBytes / 16U, 0),
                   static_cast<u32*>(slot.texture.data), GX_BUFFER_DIM(lineBytes / 16U, gapBytes / 16U), sizeBytes,
                   BIT(3)); // GX "texture copy" mode: honour the line/gap dims
    slot.needsClear = false;
}

void GfxRenderingAPICitro3D::ClearFramebuffer(bool color, bool depth) {
    if (mImpl->activeTarget == nullptr) {
        return;
    }
    // C3D_FrameBufClear dereferences the depth buffer unconditionally.
    if (mImpl->activeTarget->frameBuf.depthBuf == nullptr) {
        depth = false;
    }
    C3D_ClearBits clear = static_cast<C3D_ClearBits>((color ? C3D_CLEAR_COLOR : 0) | (depth ? C3D_CLEAR_DEPTH : 0));
    if (clear != 0) {
        C3D_RenderTargetClear(mImpl->activeTarget, clear, 0x000000FF, 0);
    }
}

void GfxRenderingAPICitro3D::ClearDepthRegion(int x, int y, int width, int height) {
    (void) x;
    (void) y;
    (void) width;
    (void) height;
    ClearFramebuffer(false, true);
}

void GfxRenderingAPICitro3D::ReadFramebufferToCPU(int fbId, uint32_t width, uint32_t height, uint16_t* rgba16Buf) {
    (void) fbId;
    if (rgba16Buf != nullptr) {
        std::fill_n(rgba16Buf, static_cast<size_t>(width) * height, uint16_t{0});
    }
}

void GfxRenderingAPICitro3D::ResolveMSAAColorBuffer(int fbIdTarget, int fbIdSource) {
    (void) fbIdTarget;
    (void) fbIdSource;
}

std::unordered_map<std::pair<float, float>, uint16_t, hash_pair_ff>
GfxRenderingAPICitro3D::GetPixelDepth(int fbId, const std::set<std::pair<float, float>>& coordinates) {
    // Read the completed frame at CURRENT coordinates. No coordinate-keyed
    // history, lost batches, GPU reads, or queue wait in the game callback.
    DepthSnapshot3DS* snapshot = nullptr;
    if (fbId == 0) {
        snapshot = &mImpl->depthSnapshot;
    } else if (fbId > 0 && fbId < static_cast<int>(mImpl->framebuffers.size()) &&
               mImpl->framebuffers[fbId] && mImpl->framebuffers[fbId]->initialized) {
        snapshot = &mImpl->framebuffers[fbId]->depthSnapshot;
    }
    if (snapshot) snapshot->requested = true;
    std::unordered_map<std::pair<float, float>, uint16_t, hash_pair_ff> result;
    for (const auto& coordinate : coordinates) {
        result.emplace(coordinate, snapshot ? snapshot->Sample(coordinate.first, coordinate.second)
                                            : DepthSnapshot3DS::Far);
    }
    mImpl->sampleDepthQueryCount += coordinates.size();
    return result;
}

// FrameBegin has drained the previous GX queue. Snapshot before any clear or
// draw can overwrite it. One packed copy per queried target; no per-pixel
// conversion until Sample(). Additional batches share the same immutable copy.
void GfxRenderingAPICitro3D::ResolveDepthProbe() {
    Soh3dsProfileScope depthProfile(Soh3dsProfileSection::Depth);
    auto capture = [](DepthSnapshot3DS& snapshot, C3D_RenderTarget* target,
                      uint32_t width, uint32_t height, bool rotated) {
        if (!snapshot.requested) return;
        if (!target || !target->frameBuf.depthBuf || target->frameBuf.depthFmt != GPU_RB_DEPTH24_STENCIL8) {
            snapshot.Capture(nullptr, 0, 0, 0, 0, false);
            return;
        }
        const auto& fb = target->frameBuf;
        GSPGPU_InvalidateDataCache(fb.depthBuf, static_cast<size_t>(fb.width) * fb.height * 4U);
        if (!snapshot.Capture(fb.depthBuf, fb.width, fb.height, width, height, rotated)) {
            static unsigned failures = 0;
            if (++failures <= 3) std::fprintf(stderr, "soh-3ds gfx: depth snapshot unavailable\n");
        }
    };
    capture(mImpl->depthSnapshot, mImpl->gameTarget, mImpl->renderWidth, mImpl->renderHeight,
            mImpl->gameTarget == mImpl->topTarget);
    for (auto& slot : mImpl->framebuffers) {
        if (slot && slot->initialized) {
            capture(slot->depthSnapshot, slot->target, slot->contentWidth, slot->contentHeight, slot->rotated);
        }
    }
}

void* GfxRenderingAPICitro3D::GetFramebufferTextureId(int fbId) {
    if (fbId <= 0 || fbId >= static_cast<int>(mImpl->framebuffers.size()) ||
        mImpl->framebuffers[fbId] == nullptr || !mImpl->framebuffers[fbId]->initialized) {
        return nullptr;
    }
    return mImpl->framebuffers[fbId]->texture.data;
}

void GfxRenderingAPICitro3D::SelectTextureFb(int fbId) {
    if (fbId <= 0 || fbId >= static_cast<int>(mImpl->framebuffers.size()) || mImpl->framebuffers[fbId] == nullptr) {
        return;
    }
    mImpl->selectedTextureUnit = 0;
    mImpl->selectedTextures[0] = 0;
    mImpl->selectedFramebuffers[0] = fbId;
    // No storage yet (never drawn to or copied into): leave the unit bound to
    // whatever it held; DrawTriangles skips draws that sample such a slot.
    if (mImpl->framebuffers[fbId]->initialized) {
        mImpl->BindTexture(0, &mImpl->framebuffers[fbId]->texture);
    }
}

void GfxRenderingAPICitro3D::DeleteTexture(uint32_t textureId) {
    if (textureId == 0 || textureId >= mImpl->textures.size()) {
        return;
    }
    auto& slot = mImpl->textures[textureId];
    if (slot.initialized) {
        // Same hazard as UploadTexture: a draw recorded this frame may still
        // read this storage when the command list executes.
        if (mImpl->frameActive && slot.lastDrawnFrame == mImpl->frameOrdinal) {
            mImpl->retiredTextures.push_back(slot.texture);
        } else {
            C3D_TexDelete(&slot.texture);
        }
        slot.initialized = false;
        slot.allocatedBytes = 0;
    }
}

bool GfxRenderingAPICitro3D::TextureHasStorage(uint32_t textureId) {
    return textureId != 0 && textureId < mImpl->textures.size() && mImpl->textures[textureId].initialized;
}

void GfxRenderingAPICitro3D::ReleaseTextureAllocations() {
    if (mImpl == nullptr) {
        return;
    }
    // C3D_FrameEnd queues the submitted frame asynchronously. Memory-pressure
    // recovery releases both the GPU allocations below and the source-resource
    // owners immediately afterwards, so wait until neither can still be read by
    // the GPU. This path is exceptional; its stall is preferable to a use-after-
    // free that presents as corrupted sprites followed by an application exit.
    if (mImpl->initialized && !mImpl->frameActive) {
        C3D_FrameSync();
    }
    for (uint32_t textureId = 1; textureId < mImpl->textures.size(); ++textureId) {
        DeleteTexture(textureId);
    }
    mImpl->selectedTextures.fill(0);
}

void GfxRenderingAPICitro3D::SetTextureFilter(FilteringMode mode) {
    mImpl->filteringMode = mode;
}

FilteringMode GfxRenderingAPICitro3D::GetTextureFilter() {
    return mImpl->filteringMode;
}

void GfxRenderingAPICitro3D::SetSrgbMode() {
    mSrgbMode = true;
}

ImTextureID GfxRenderingAPICitro3D::GetTextureById(int id) {
    return reinterpret_cast<ImTextureID>(static_cast<uintptr_t>(id));
}

void GfxRenderingAPICitro3D::SetCurrentPrimDepth(float depth) {
    mCurrentPrimDepth = depth;
    mPrimDepthDirty = true;
}

void GfxRenderingAPICitro3D::GetDebugStats(size_t* textureSlots, size_t* initializedTextures,
                                           size_t* textureBytes, size_t* shaderPrograms,
                                           size_t* clipScratchBytes) const {
    size_t live = 0;
    size_t bytes = 0;
    for (const auto& slot : mImpl->textures) {
        if (!slot.initialized) {
            continue;
        }
        ++live;
        bytes += slot.allocatedBytes;
    }
    if (textureSlots != nullptr) {
        *textureSlots = mImpl->textures.size();
    }
    if (initializedTextures != nullptr) {
        *initializedTextures = live;
    }
    if (textureBytes != nullptr) {
        *textureBytes = bytes;
    }
    if (shaderPrograms != nullptr) {
        *shaderPrograms = mImpl->shaderPrograms.size();
    }
    if (clipScratchBytes != nullptr) {
        *clipScratchBytes = mImpl->clipScratch.capacity() * sizeof(float);
    }
}

float GfxRenderingAPICitro3D::GetPresentedFps(uint64_t windowMilliseconds) const {
    if (windowMilliseconds == 0 || mImpl->presentedTimestampCount == 0) {
        return 0.0f;
    }

    const uint64_t now = osGetTime();
    const uint64_t cutoff = now > windowMilliseconds ? now - windowMilliseconds : 0;
    size_t framesInWindow = 0;
    for (size_t offset = 0; offset < mImpl->presentedTimestampCount; ++offset) {
        const size_t index = (mImpl->presentedTimestampHead + mImpl->presentedTimestamps.size() - 1 - offset) %
                             mImpl->presentedTimestamps.size();
        if (mImpl->presentedTimestamps[index] <= cutoff) {
            break;
        }
        ++framesInWindow;
    }

    const uint64_t lifetime = now >= mImpl->firstPresentedTimestamp
                                  ? now - mImpl->firstPresentedTimestamp
                                  : 0;
    if (lifetime < windowMilliseconds) {
        if (mImpl->presentedTimestampCount < 2 || lifetime == 0) {
            return 0.0f;
        }
        return static_cast<float>(mImpl->presentedTimestampCount - 1) * 1000.0f /
               static_cast<float>(lifetime);
    }
    return static_cast<float>(framesInWindow) * 1000.0f /
           static_cast<float>(windowMilliseconds);
}

float GfxRenderingAPICitro3D::GetPresentedFps2Seconds() const {
    return GetPresentedFps(2000);
}

float GfxRenderingAPICitro3D::GetPresentedFps10Seconds() const {
    return GetPresentedFps(10000);
}

uint64_t GfxRenderingAPICitro3D::GetDrawCallCount() const {
    return mImpl->drawCallCount;
}

uint64_t GfxRenderingAPICitro3D::GetTriangleCount() const {
    return mImpl->triangleCount;
}

uint64_t GfxRenderingAPICitro3D::GetTextureCacheUploadCount() const {
    return mImpl->textureCacheUploadCount;
}

uint64_t GfxRenderingAPICitro3D::GetTextureCacheUploadBytes() const {
    return mImpl->textureCacheUploadBytes;
}

uint64_t GfxRenderingAPICitro3D::GetVertexUploadCount() const {
    return mImpl->vertexUploadCount;
}

uint64_t GfxRenderingAPICitro3D::GetVertexUploadBytes() const {
    return mImpl->vertexUploadBytes;
}

uint64_t GfxRenderingAPICitro3D::GetLinearHeapFlushFrameCount() const {
    return mImpl->linearHeapFlushFrameCount;
}

void* GfxRenderingAPICitro3D::PrepareForExternalDraw() {
    if (!mImpl->frameActive) {
        return nullptr;
    }
    // Citro2D's target-clear helper performs an internal FrameSplit. Complete
    // any scaled presentation first, then hand it a coherent top target.
    PresentSceneToTopTarget();
    FlushPackedVertices();
    return mImpl->topTarget;
}

void GfxRenderingAPICitro3D::MarkExternalLinearBuffersDirty() {
    if (mImpl->frameActive) {
        mImpl->externalLinearBuffersDirty = true;
    }
}

// Dual screen bridge for the game side (framebuffer_effects.c): the Fast3D
// framebuffer id whose draws go straight to the bottom LCD. May be called
// before the renderer exists; the id is applied when it does.
namespace {
int* sBottomScreenFbSlot = nullptr;
int* sBottomScreenZoomFbSlot = nullptr;
int sPendingBottomScreenFb = -1;
int sPendingBottomScreenZoomFb = -1;
float* sBottomZoomSlot = nullptr; // scale, centerX, centerY
} // namespace

void GfxRenderingAPICitro3D::RegisterBottomBridge() {
    sBottomScreenFbSlot = &mImpl->bottomScreenFb;
    sBottomScreenZoomFbSlot = &mImpl->bottomScreenZoomFb;
    mImpl->bottomScreenFb = sPendingBottomScreenFb;
    mImpl->bottomScreenZoomFb = sPendingBottomScreenZoomFb;
    static_assert(sizeof(Impl::BottomZoom) == 3 * sizeof(float), "BottomZoom is three floats");
    sBottomZoomSlot = &mImpl->bottomZoom.scale;
}

extern "C" void Soh3dsSetBottomScreenFramebuffer(int fbId) {
    sPendingBottomScreenFb = fbId;
    if (sBottomScreenFbSlot != nullptr) {
        *sBottomScreenFbSlot = fbId;
    }
}

// Second id for the same LCD: draws under it get the bottom-pass zoom below,
// draws under the plain id do not, so a frame can carry unzoomed chrome (tab
// strip) and a zoomed minimap together.
extern "C" void Soh3dsSetBottomScreenZoomFramebuffer(int fbId) {
    sPendingBottomScreenZoomFb = fbId;
    if (sBottomScreenZoomFbSlot != nullptr) {
        *sBottomScreenZoomFbSlot = fbId;
    }
}

// Per-frame zoom for the bottom pass (game code sets it while building the
// display list; the renderer applies it when that list executes). scale 1 =
// none. centre is in NDC of the 320x240 framebuffer.
extern "C" void Soh3dsSetBottomPassZoom(float scale, float ndcCenterX, float ndcCenterY) {
    if (sBottomZoomSlot != nullptr) {
        sBottomZoomSlot[0] = scale;
        sBottomZoomSlot[1] = ndcCenterX;
        sBottomZoomSlot[2] = ndcCenterY;
    }
}

} // namespace Fast
