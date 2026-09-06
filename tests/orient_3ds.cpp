// Offscreen render-target orientation probe.
//
// The backend applies a -90 degree rotation for the physical screens, whose
// framebuffers are stored portrait. Whether a C3D_RenderTargetCreateFromTex
// target needs the same correction is a hardware layout question, and guessing
// it would render every framebuffer effect sideways with no obvious symptom.
//
// So: draw a deliberately asymmetric pattern into an offscreen target, read it
// back through the backend's own ReadFramebufferToCPU, and report where each
// marker landed. The answer decides which matrix StartDrawToFramebuffer binds.
//
// Markers, in clip space, chosen so every candidate transform gives a distinct
// result:
//   RED   fills the top-left quadrant
//   GREEN is a small square in the top-right corner
// Upright -> red top-left, green top-right
// V-flip  -> red bottom-left, green bottom-right
// Rotated -> red elsewhere; green identifies the direction

#include <3ds.h>
#include <citro3d.h>

#include <cstdio>
#include <vector>

#include "pica/gfx_citro3d.h"
#include "ctr_log.h"

namespace {

// Deliberately NOT square. A square target hides viewport/scissor
// transposition entirely: swapping w and h is a no-op when they are equal.
constexpr uint32_t kW = 256;
constexpr uint32_t kH = 128;
constexpr uint64_t kFlatColourId = 1ull << 12; // (A-B)*C+D with C==0 selects D

// Two triangles covering [x0,x1] x [y0,y1] in clip space, flat colour, in the
// 7-float layout Fast3D emits for this combiner.
void AddQuad(std::vector<float>& vbo, float x0, float y0, float x1, float y1, float r, float g, float b) {
    const float corner[6][2] = { { x0, y0 }, { x1, y0 }, { x1, y1 }, { x0, y0 }, { x1, y1 }, { x0, y1 } };
    for (const auto& c : corner) {
        vbo.insert(vbo.end(), { c[0], c[1], 0.5f, 1.0f, r, g, b });
    }
}

struct Counts {
    int red = 0;
    int green = 0;
};

const char* kQuadrantName[4] = { "top-left", "top-right", "bottom-left", "bottom-right" };

} // namespace

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;

    gfxInitDefault();
    consoleInit(GFX_BOTTOM, nullptr);
    ctr_log_init();

    ctr_log("SoH-3DS offscreen orientation probe\n");

    Fast::GfxRenderingAPICitro3D api;
    api.Init();
    if (!api.IsReady()) {
        ctr_log("FAIL: backend did not initialise\n");
        gfxExit();
        return 1;
    }

    const int fb = api.CreateFramebuffer();
    api.UpdateFramebufferParameters(fb, kW, kH, 0, false, true, true, false);
    ctr_log("offscreen fb id %d, %ux%u (non-square on purpose)\n", fb, kW, kH);

    Fast::ShaderProgram* prg = api.CreateAndLoadNewShader(kFlatColourId, 0x0);
    api.LoadShader(prg);
    api.SetDepthTestAndMask(false, false);
    api.SetUseAlpha(false);
    api.SetViewport(0, 0, kW, kH);
    api.SetScissor(0, 0, kW, kH);

    std::vector<float> vbo;
    AddQuad(vbo, -1.0f, 0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 0.0f); // red: top-left quadrant
    AddQuad(vbo, 0.7f, 0.7f, 1.0f, 1.0f, 0.0f, 1.0f, 0.0f);  // green: top-right corner

    api.StartFrame();
    api.StartDrawToFramebuffer(fb, 0.0f);
    api.DrawTriangles(vbo.data(), vbo.size(), 4); // 2 quads = 4 triangles
    api.EndFrame();

    std::vector<uint16_t> px(static_cast<size_t>(kW) * kH, 0);
    api.ReadFramebufferToCPU(fb, kW, kH, px.data());

    // RGB5A1: rrrrrgggggbbbbba
    Counts q[4];
    int nonZero = 0;
    for (uint32_t y = 0; y < kH; ++y) {
        for (uint32_t x = 0; x < kW; ++x) {
            const uint16_t v = px[static_cast<size_t>(y) * kW + x];
            const int r = (v >> 11) & 0x1F, g = (v >> 6) & 0x1F, b = (v >> 1) & 0x1F;
            if (r + g + b == 0) {
                continue;
            }
            ++nonZero;
            const int idx = (y < kH / 2 ? 0 : 2) + (x < kW / 2 ? 0 : 1);
            if (r > g && r > b) {
                ++q[idx].red;
            } else if (g > r && g > b) {
                ++q[idx].green;
            }
        }
    }

    ctr_log("readback   : %d non-black of %u\n", nonZero, kW * kH);
    // A correct upright viewport fills exactly a quarter of the surface with
    // red. A transposed one clips to the shorter axis and undercounts.
    const int expectRed = static_cast<int>((kW / 2) * (kH / 2));
    ctr_log("expect red : %d (quarter of %ux%u)\n", expectRed, kW, kH);
    for (int i = 0; i < 4; ++i) {
        ctr_log("  %-13s red=%5d green=%5d\n", kQuadrantName[i], q[i].red, q[i].green);
    }

    int redQ = 0, greenQ = 0;
    for (int i = 1; i < 4; ++i) {
        if (q[i].red > q[redQ].red) {
            redQ = i;
        }
        if (q[i].green > q[greenQ].green) {
            greenQ = i;
        }
    }

    if (nonZero == 0) {
        ctr_log("VERDICT: nothing rendered - offscreen target or readback is broken\n");
    } else if (redQ == 0 && greenQ == 1) {
        const int got = q[0].red;
        if (got < expectRed - expectRed / 8) {
            ctr_log("VERDICT: UPRIGHT but viewport CLIPPED - red %d of expected %d\n", got, expectRed);
        } else {
            ctr_log("VERDICT: UPRIGHT - offscreen orientation and viewport both correct\n");
        }
    } else if (redQ == 2 && greenQ == 3) {
        ctr_log("VERDICT: V-FLIPPED - readback row order is inverted\n");
    } else {
        ctr_log("VERDICT: ROTATED - red in %s, green in %s\n", kQuadrantName[redQ], kQuadrantName[greenQ]);
    }

    ctr_log("done\n");

    for (int frame = 0; frame < 90 && aptMainLoop(); ++frame) {
        hidScanInput();
        if (hidKeysDown() & KEY_START) {
            break;
        }
        gspWaitForVBlank();
    }

    api.Shutdown();
    gfxExit();
    return 0;
}
