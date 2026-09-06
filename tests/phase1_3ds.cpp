// Phase 1 acceptance test: drive the PICA200 backend the way Fast3D does and
// verify the resulting pixels.
//
// Verification goes through an offscreen render target and the backend's own
// ReadFramebufferToCPU, not the display framebuffer. Reading the live screen
// buffer mixes frames when anything logs between samples, and adds the screen's
// rotation and double buffering to every assertion; the offscreen path is a
// plain linear image and is deterministic.
//
// The orientation of that path is not assumed - tests/orient_3ds.cpp measures
// it with an asymmetric marker on a non-square target. Its result: offscreen
// surfaces are upright, so clip (-1,+1) maps to pixel (0,0).
//
// Nothing is mocked: the shader is the assembled fast3d.v.pica, shader ids are
// decoded by libultraship's gfx_cc_get_features, and vertices are in Fast3D's
// buf_vbo layout.

#include <3ds.h>
#include <citro3d.h>

#include <cstdarg>
#include <cstdio>
#include <utility>
#include <vector>

#include "pica/combiner_lower.hpp"
#include "pica/gfx_citro3d.h"
#include "ctr_log.h"

namespace {

constexpr uint32_t kW = 400;
constexpr uint32_t kH = 240;

// (A-B)*C+D with C==0 selects D, so a flat vertex colour puts the shading input
// in slot D - term index 3, i.e. bit 12 of shader_id0. Putting it in slot A
// renders black, which is a good way to waste an afternoon.
constexpr uint64_t kFlatColourId = 1ull << 12;
// A*C, selected by B==0 && D==0, with A=INPUT_1 and C=INPUT_2.
constexpr uint64_t kMultiplyId = (1ull << 0) | (2ull << 8);

struct Rgb {
    int r, g, b;
};

// RGB5A1 -> 0..255 per channel.
Rgb Decode(uint16_t v) {
    const int r5 = (v >> 11) & 0x1F, g5 = (v >> 6) & 0x1F, b5 = (v >> 1) & 0x1F;
    return { r5 * 255 / 31, g5 * 255 / 31, b5 * 255 / 31 };
}

// Clip space -> pixel, per the measured upright orientation.
std::pair<int, int> ClipToPixel(float cx, float cy) {
    return { static_cast<int>((cx + 1.0f) * 0.5f * kW), static_cast<int>((1.0f - cy) * 0.5f * kH) };
}

const float kTriX[3] = { 0.0f, -0.8f, 0.8f };
const float kTriY[3] = { 0.8f, -0.8f, -0.8f };

bool gOk = true;

void Fail(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    char buf[256];
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    ctr_log("FAIL: %s\n", buf);
    gOk = false;
}

} // namespace

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;

    gfxInitDefault();
    consoleInit(GFX_BOTTOM, nullptr);
    ctr_log_init();

    ctr_log("SoH-3DS Phase 1: PICA200 backend\n");

    Fast::GfxRenderingAPICitro3D api;
    api.Init();
    if (!api.IsReady()) {
        ctr_log("FAIL: backend did not initialise\nPHASE 1 FAIL\n");
        gfxExit();
        return 1;
    }
    ctr_log("backend    : %s, max texture %d\n", api.GetName(), api.GetMaxTextureSize());

    const Fast::GfxClipParameters clip = api.GetClipParameters();
    ctr_log("clip       : z_0_to_1=%d invertY=%d\n", clip.z_is_from_0_to_1, clip.invertY);

    const int fb = api.CreateFramebuffer();
    api.UpdateFramebufferParameters(fb, kW, kH, 0, false, true, true, false);
    api.SetDepthTestAndMask(false, false);
    api.SetUseAlpha(false);
    api.SetViewport(0, 0, kW, kH);
    api.SetScissor(0, 0, kW, kH);

    std::vector<uint16_t> px(static_cast<size_t>(kW) * kH, 0);
    auto render = [&](Fast::ShaderProgram* prg, const std::vector<float>& vbo, size_t tris) {
        api.LoadShader(prg);
        api.StartFrame();
        api.StartDrawToFramebuffer(fb, 0.0f);
        api.DrawTriangles(const_cast<float*>(vbo.data()), vbo.size(), tris);
        api.EndFrame();
        api.ReadFramebufferToCPU(fb, kW, kH, px.data());
    };
    auto at = [&](int x, int y) { return Decode(px[static_cast<size_t>(y) * kW + x]); };

    // --- combiner decode -----------------------------------------------------
    Fast::ShaderProgram* prg = api.CreateAndLoadNewShader(kFlatColourId, 0x0);
    if (prg == nullptr) {
        Fail("CreateAndLoadNewShader returned null");
    } else {
        uint8_t numInputs = 0;
        bool usedTextures[2] = { false, false };
        api.ShaderGetInfo(prg, &numInputs, usedTextures);
        ctr_log("program    : inputs=%u tex0=%d tex1=%d stride=%u floats\n", numInputs, usedTextures[0],
                usedTextures[1], prg->strideFloats);
        if (prg->strideFloats != 7) {
            Fail("expected stride 7 floats, got %u", prg->strideFloats);
        }
        if (api.LookupShader(kFlatColourId, 0x0) != prg) {
            Fail("LookupShader did not return the cached program");
        }

        // A Plan straight out of lower_channel() carries placeholder sources;
        // forwarding those to the GPU samples textures the combiner never named.
        pica::Plan plan = pica::lower_channel(prg->combiner[0][0]);
        pica::bind_sources(plan, prg->combiner[0][0], 0, pica::SHADER_INPUT_1);
        ctr_log("lowered    : %d stage(s), stage0 func=%d src0=%d op0=%d\n", plan.count, (int)plan.stage[0].func,
                (int)plan.stage[0].src[0], (int)plan.stage[0].op[0]);
        if (plan.stage[0].src[0] != pica::Src::PrimaryColor) {
            Fail("expected D bound to PrimaryColor, got src=%d", (int)plan.stage[0].src[0]);
        }
    }

    // --- flat colour ---------------------------------------------------------
    auto flatTri = [](float r, float g, float b) {
        std::vector<float> v;
        for (int i = 0; i < 3; ++i) {
            v.insert(v.end(), { kTriX[i], kTriY[i], 0.5f, 1.0f, r, g, b });
        }
        return v;
    };

    render(prg, flatTri(1.0f, 0.0f, 0.0f), 1);

    size_t nonBlack = 0, red = 0;
    for (uint32_t y = 0; y < kH; ++y) {
        for (uint32_t x = 0; x < kW; ++x) {
            const Rgb c = at(x, y);
            if (c.r + c.g + c.b > 0) {
                ++nonBlack;
            }
            if (c.r > 0x80 && c.g < 0x40 && c.b < 0x40) {
                ++red;
            }
        }
    }
    const size_t total = static_cast<size_t>(kW) * kH;
    ctr_log("coverage   : %u non-black, %u red, of %u\n", (unsigned)nonBlack, (unsigned)red, (unsigned)total);
    // The triangle spans 1.6x1.6 of a 2x2 clip square: 0.8*0.8/2 = 32%.
    if (red < total / 4 || red > (total * 2) / 5) {
        Fail("red coverage %u outside 25-40%% of %u", (unsigned)red, (unsigned)total);
    }

    struct Probe {
        const char* name;
        int x, y;
        bool covered;
    };
    const auto centre = ClipToPixel(0.0f, -0.2f);
    const Probe probes[] = {
        { "centre", centre.first, centre.second, true },
        { "top-left", 4, 4, false },
        { "top-right", (int)kW - 5, 4, false },
        { "above apex", (int)kW / 2, 4, false },
        { "below base", (int)kW / 2, (int)kH - 3, false },
    };
    for (const auto& p : probes) {
        const Rgb c = at(p.x, p.y);
        const bool covered = c.r > 0x80;
        ctr_log("  %-11s (%3d,%3d) = %02x%02x%02x %s\n", p.name, p.x, p.y, c.r, c.g, c.b,
                covered ? "covered" : "clear");
        if (covered != p.covered) {
            Fail("%s expected %s", p.name, p.covered ? "covered" : "clear");
        }
    }

    // A second colour proves the vertex path is real, not a stuck constant.
    render(prg, flatTri(0.0f, 1.0f, 0.0f), 1);
    const Rgb g = at(centre.first, centre.second);
    ctr_log("green pass : centre = %02x%02x%02x\n", g.r, g.g, g.b);
    if (g.g < 0x80 || g.r > 0x40) {
        Fail("expected green centre, got %02x%02x%02x", g.r, g.g, g.b);
    }

    // --- two shading inputs --------------------------------------------------
    // PICA has one colour attribute, so one input rides it per-vertex and the
    // other must resolve into the stage's constant register. A single-input
    // test cannot reach that split, and getting it wrong is invisible on flat
    // geometry - hence three distinct vertex colours, probed near each vertex.
    Fast::ShaderProgram* prg2 = api.CreateAndLoadNewShader(kMultiplyId, 0x0);
    uint8_t n2 = 0;
    bool tex2[2] = { false, false };
    api.ShaderGetInfo(prg2, &n2, tex2);
    ctr_log("two-input  : inputs=%u stride=%u floats\n", n2, prg2->strideFloats);
    if (n2 != 2 || prg2->strideFloats != 10) {
        Fail("expected 2 inputs and stride 10, got %u/%u", n2, prg2->strideFloats);
    }

    const float vtxRgb[3][3] = { { 1, 0, 0 }, { 0, 1, 0 }, { 0, 0, 1 } };
    auto twoInputTri = [&](float c2r, float c2g, float c2b) {
        std::vector<float> v;
        for (int i = 0; i < 3; ++i) {
            v.insert(v.end(), { kTriX[i], kTriY[i], 0.5f, 1.0f });
            v.insert(v.end(), { vtxRgb[i][0], vtxRgb[i][1], vtxRgb[i][2] });
            v.insert(v.end(), { c2r, c2g, c2b });
        }
        return v;
    };

    // Probe points from the geometry: centroid pulled 60% toward each vertex,
    // so each is well inside the triangle and nearest exactly one vertex.
    auto probeFor = [&](int v) {
        const float cx = (kTriX[0] + kTriX[1] + kTriX[2]) / 3.0f;
        const float cy = (kTriY[0] + kTriY[1] + kTriY[2]) / 3.0f;
        return ClipToPixel(cx + 0.6f * (kTriX[v] - cx), cy + 0.6f * (kTriY[v] - cy));
    };
    const auto p0 = probeFor(0), p1 = probeFor(1), p2 = probeFor(2);

    render(prg2, twoInputTri(1.0f, 1.0f, 1.0f), 1); // white multiplier: raw gradient
    const struct {
        const char* name;
        std::pair<int, int> p;
        int ch;
    } corners[] = { { "near v0 (red)", p0, 0 }, { "near v1 (green)", p1, 1 }, { "near v2 (blue)", p2, 2 } };

    for (const auto& cn : corners) {
        const Rgb c = at(cn.p.first, cn.p.second);
        const int ch[3] = { c.r, c.g, c.b };
        ctr_log("  %-16s (%3d,%3d) = %02x%02x%02x\n", cn.name, cn.p.first, cn.p.second, c.r, c.g, c.b);
        for (int k = 0; k < 3; ++k) {
            if (k != cn.ch && ch[k] >= ch[cn.ch]) {
                Fail("%s expected channel %d to dominate", cn.name, cn.ch);
                break;
            }
        }
    }

    // Red multiplier must zero G and B everywhere: only true if INPUT_2 really
    // reached the stage constant.
    render(prg2, twoInputTri(1.0f, 0.0f, 0.0f), 1);
    const Rgb nearRed = at(p0.first, p0.second);
    const Rgb nearGreen = at(p1.first, p1.second);
    ctr_log("  x red const: v0 = %02x%02x%02x, v1 = %02x%02x%02x\n", nearRed.r, nearRed.g, nearRed.b, nearGreen.r,
            nearGreen.g, nearGreen.b);
    if (nearRed.g > 0x20 || nearRed.b > 0x20) {
        Fail("red multiplier should zero G and B, got %02x%02x%02x", nearRed.r, nearRed.g, nearRed.b);
    }
    if (nearRed.r < 0x80) {
        Fail("red channel should survive the multiply, got %02x", nearRed.r);
    }
    if (nearGreen.r > 0x40) {
        Fail("green vertex x red constant should be near black, got %02x", nearGreen.r);
    }

    ctr_log(gOk ? "PHASE 1 PASS\n" : "PHASE 1 FAIL\n");

    for (int frame = 0; frame < 90 && aptMainLoop(); ++frame) {
        hidScanInput();
        if (hidKeysDown() & KEY_START) {
            break;
        }
        gspWaitForVBlank();
    }

    // Tear the backend down before gfxExit(). As a plain local it would be
    // destroyed after main returns, i.e. after the graphics subsystem it
    // depends on is already gone - which faults at pc=0 on exit.
    api.Shutdown();
    gfxExit();
    return gOk ? 0 : 1;
}
