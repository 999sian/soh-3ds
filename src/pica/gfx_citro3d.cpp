#include "gfx_citro3d.h"
#include "combiner_lower.hpp"

#include <3ds.h>
#include <citro2d.h>
#include <malloc.h>

#include <citro3d.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <map>
#include <vector>

#include "fast/interpreter.h" // CCFeatures, gfx_cc_get_features, SHADER_* tokens

// Assembled from src/pica/fast3d.v.pica by picasso, embedded via bin2s.
extern "C" {
extern const uint8_t fast3d_shbin[];
extern const uint32_t fast3d_shbin_size;
}

namespace Fast {

namespace {

constexpr int kMaxTextures = 2048;
constexpr int kTopWidth = 400;
constexpr int kTopHeight = 240;
constexpr int kBottomWidth = 320;
constexpr int kBottomHeight = 240;
constexpr size_t kVertexCapacity = 32 * 1024;

// Packed GPU vertex. Colour is 4 unsigned bytes rather than 4 floats: a quarter
// the bytes over the bus, and the shader scales by 1/255.
struct PackedVertex {
    float pos[4];
    float uv0[2];
    float uv1[2];
    uint8_t rgba[4];
};

// pica::Src / pica::Func mirror the libctru GPU_TEVSRC / GPU_COMBINEFUNC values
// exactly, so these are numeric identities rather than translations.
inline GPU_TEVSRC ToTevSrc(pica::Src s) {
    return static_cast<GPU_TEVSRC>(static_cast<uint8_t>(s));
}
inline GPU_COMBINEFUNC ToTevFunc(pica::Func f) {
    return static_cast<GPU_COMBINEFUNC>(static_cast<uint8_t>(f));
}
// pica::Op mirrors GPU_TEVOP_RGB. The alpha operand enum is a different set:
// there is no colour-channel selection, so anything not a one-minus is SrcAlpha.
inline GPU_TEVOP_RGB ToTevOpRgb(pica::Op o) {
    return static_cast<GPU_TEVOP_RGB>(static_cast<uint8_t>(o));
}
inline GPU_TEVOP_A ToTevOpAlpha(pica::Op o) {
    return (o == pica::Op::OneMinusSrcColor || o == pica::Op::OneMinusSrcAlpha) ? GPU_TEVOP_A_ONE_MINUS_SRC_ALPHA
                                                                                : GPU_TEVOP_A_SRC_ALPHA;
}

// Which source row belongs at row `dst` of the power-of-two backing store.
//
// The V axis is flipped relative to the source image, and the flip is anchored
// to the backing height because that is what the sampler indexes.
constexpr uint32_t SourceRowForBackingRow(uint32_t backingHeight, uint32_t sourceHeight, uint32_t dst) {
    return (backingHeight - 1u - dst) % sourceHeight;
}
// The NPOT cases that a power-of-two-only test would never reach.
static_assert(SourceRowForBackingRow(256, 240, 255) == 0);
static_assert(SourceRowForBackingRow(256, 240, 240) == 15);
static_assert(SourceRowForBackingRow(256, 240, 239) == 16);
static_assert(SourceRowForBackingRow(16, 12, 15) == 0);
static_assert(SourceRowForBackingRow(16, 12, 4) == 11);
// POT: an exact mirror.
static_assert(SourceRowForBackingRow(64, 64, 0) == 63);
static_assert(SourceRowForBackingRow(64, 64, 63) == 0);

// PICA200 texture dimensions are powers of two from 8 to 1024 inclusive.
// Which backend instance called gfxInitDefault(), so only that instance calls
// gfxExit(). libctru offers no "already initialised" query and double-init is
// not safe.
const void* sGfxOwner = nullptr;

uint16_t NextPow2(uint32_t v) {
    constexpr uint32_t kMinDim = 8;    // PICA minimum texture dimension
    constexpr uint32_t kMaxDim = 1024; // PICA maximum texture dimension
    if (v >= kMaxDim) {
        // Clamping matters for termination, not just validity: the loop below
        // shifted without a ceiling, so a large or garbage dimension walked p to
        // 0x80000000, then to 0, and span forever - a silent hang inside display
        // list execution with no fault to point at it.
        return static_cast<uint16_t>(kMaxDim);
    }
    uint32_t p = kMinDim;
    while (p < v) {
        p <<= 1;
    }
    return static_cast<uint16_t>(p);
}

// 8x8 Morton (Z-order) offset within a tile: x contributes even bit positions,
// y the odd ones. Tables generated rather than transcribed (DESIGN section 4.1).
struct MortonTables {
    uint32_t x[8];
    uint32_t y[8];
    constexpr MortonTables() : x(), y() {
        for (uint32_t i = 0; i < 8; ++i) {
            uint32_t xv = 0, yv = 0;
            for (uint32_t b = 0; b < 3; ++b) {
                const uint32_t bit = (i >> b) & 1u;
                xv |= bit << (2 * b);
                yv |= bit << (2 * b + 1);
            }
            x[i] = xv;
            y[i] = yv;
        }
    }
};
constexpr MortonTables kMorton{};

} // namespace

struct GfxRenderingAPICitro3D::Impl {
    // --- shader ---
    DVLB_s* dvlb = nullptr;
    shaderProgram_s program{};
    int8_t uLocProjection = -1;

    // --- targets ---
    C3D_RenderTarget* topTarget = nullptr;
    C3D_RenderTarget* bottomTarget = nullptr;
    C3D_RenderTarget* activeTarget = nullptr;
    bool frameActive = false;
    bool gfxUp = false; // this backend called gfxInitDefault(): it must gfxExit()
    bool c2dUp = false;              // citro2d initialised for the bottom-screen UI
    C2D_TextBuf textBuf = nullptr;   // reused every frame for the bottom screen
    // Set when a citro2d batch was submitted, so the next frame knows to restore
    // this backend's pipeline state and that C3D_FrameEnd cannot take the
    // narrower GX_CMDLIST_FLUSH path - citro2d writes its own linear buffers.
    bool c2dDrewLastFrame = false;
    bool c3dUp = false; // C3D_Init succeeded: teardown required
    bool ready = false; // full setup succeeded: backend usable

    // --- framebuffers ---
    struct FramebufferSlot {
        C3D_Tex texture{};
        C3D_RenderTarget* target = nullptr;
        bool initialized = false;
        bool needsClear = true;
        uint16_t logicalW = 0;
        uint16_t logicalH = 0;
        // When set, this framebuffer id *is* the bottom screen: draws to it go
        // straight to the touch screen with no intermediate texture or blit.
        bool isBottomScreen = false;
    };
    std::vector<std::unique_ptr<FramebufferSlot>> framebuffers;
    int bottomScreenFb = -1;

    // --- textures ---
    struct TextureSlot {
        C3D_Tex tex{};
        bool initialized = false;
        float scaleS = 1.0f;
        float scaleT = 1.0f;
        bool linearFilter = true;
    };
    std::vector<TextureSlot> textures;
    uint32_t boundTexture[2] = { 0, 0 };
    int activeTile = 0;
    FilteringMode filterMode = FILTER_LINEAR;

    // --- shader program cache, keyed exactly like every other backend ---
    std::map<std::pair<uint64_t, uint64_t>, ShaderProgram> programs;
    ShaderProgram* current = nullptr;

    // Which shading input each hardware stage's constant register must hold
    // (-1 = the register stays zero). Filled by ConfigureTexEnv, consumed per
    // draw once the input's colour is known.
    int8_t stageConstToken[6] = { -1, -1, -1, -1, -1, -1 };

    // Linear staging for framebuffer readback, grown on demand. The transfer
    // engine cannot write into the app heap, and VRAM is not CPU-readable.
    uint16_t* readback = nullptr;
    size_t readbackBytes = 0;
    int activeStages = 0;
    uint64_t triangleCount = 0;

    // Combiners the hardware cannot express are reported once each rather than
    // rendered wrong in silence.
    void ReportUnsupported(const ShaderProgram* prg, const char* why) {
        std::fprintf(stderr, "gfx_citro3d: unsupported combiner %016llx/%016llx: %s\n",
                     (unsigned long long)prg->shaderId0, (unsigned long long)prg->shaderId1, why);
    }

    // --- vertex staging (linear heap: the GPU cannot read the app heap) ---
    PackedVertex* vertices = nullptr;
    size_t vertexCount = 0;
    size_t flushedUpTo = 0;

    // linearAlloc memory is cached, and the GPU reads it directly. Every point
    // where queued commands can begin executing - a mid-frame C3D_FrameSplit as
    // well as C3D_FrameEnd - must be preceded by a flush of the vertices written
    // since the last one, or the GPU reads stale data.
    void FlushVertices() {
        if (vertexCount > flushedUpTo) {
            GSPGPU_FlushDataCache(vertices + flushedUpTo, (vertexCount - flushedUpTo) * sizeof(PackedVertex));
            flushedUpTo = vertexCount;
        }
    }

    // --- render state ---
    bool depthTest = false;
    bool depthMask = false;
    bool zmodeDecal = false;
    bool useAlpha = false;
    float primDepth = 0.0f;
    C3D_Mtx projScreen{};    // physical screens: rotated
    C3D_Mtx projOffscreen{}; // render-to-texture: upright
    const C3D_Mtx* activeProjection = &projScreen;

    // Viewport and scissor are transposed for the screens for the same reason
    // the projection is rotated, so they are orientation-dependent too. Fast3D
    // may set them either side of a target switch, so keep the requested rects
    // and re-apply on every bind rather than converting at call time.
    struct Rect {
        int x = 0, y = 0, w = 0, h = 0;
    };
    Rect viewport;
    Rect scissor;
    bool activeIsScreen = true;

    void ApplyViewportScissor() {
        if (viewport.w > 0 && viewport.h > 0) {
            if (activeIsScreen) {
                C3D_SetViewport(viewport.y, viewport.x, viewport.h, viewport.w);
            } else {
                C3D_SetViewport(viewport.x, viewport.y, viewport.w, viewport.h);
            }
        }
        if (scissor.w > 0 && scissor.h > 0) {
            if (activeIsScreen) {
                C3D_SetScissor(GPU_SCISSOR_NORMAL, scissor.y, scissor.x, scissor.y + scissor.h,
                               scissor.x + scissor.w);
            } else {
                C3D_SetScissor(GPU_SCISSOR_NORMAL, scissor.x, scissor.y, scissor.x + scissor.w,
                               scissor.y + scissor.h);
            }
        }
    }

    // Clip-space fixup, folded into the matrix so the shader stays 4 dp4s.
    //
    // Both variants apply the depth remap - Fast3D emits z in [0,w] and PICA
    // clips to [-w,0], so z' = z - w. Only the physical screens also need the
    // -90 rotation, because their framebuffers are stored portrait. Applying
    // that rotation to an offscreen target would render every framebuffer
    // effect sideways.
    void BuildProjection() {
        Mtx_Zeros(&projScreen);
        projScreen.r[0].y = 1.0f;  // x' =  y
        projScreen.r[1].x = -1.0f; // y' = -x
        projScreen.r[2].z = 1.0f;
        projScreen.r[2].w = -1.0f; // z' =  z - w
        projScreen.r[3].w = 1.0f;

        Mtx_Zeros(&projOffscreen);
        projOffscreen.r[0].x = 1.0f;
        projOffscreen.r[1].y = 1.0f;
        projOffscreen.r[2].z = 1.0f;
        projOffscreen.r[2].w = -1.0f;
        projOffscreen.r[3].w = 1.0f;
    }
};

GfxRenderingAPICitro3D::GfxRenderingAPICitro3D() : mImpl(std::make_unique<Impl>()) {
}

GfxRenderingAPICitro3D::~GfxRenderingAPICitro3D() {
    Shutdown();
}

// Idempotent, so callers may release GPU resources at a point they control and
// the destructor still does the right thing if they do not.
void GfxRenderingAPICitro3D::Shutdown() {
    if (!mImpl->c3dUp) {
        return;
    }

    // Order matters. citro3d holds references to the render targets and to the
    // vertex buffer, and C3D_Fini touches GSP, so everything must come down
    // before the graphics subsystem the caller owns. Getting this wrong shows
    // up as a null-pointer execute fault on exit rather than anywhere near the
    // real cause.
    for (auto& fb : mImpl->framebuffers) {
        if (fb->target != nullptr) {
            C3D_RenderTargetDelete(fb->target);
            fb->target = nullptr;
        }
        if (fb->texture.data != nullptr) {
            C3D_TexDelete(&fb->texture);
        }
    }
    mImpl->framebuffers.clear();

    for (auto& t : mImpl->textures) {
        if (t.tex.data != nullptr) {
            C3D_TexDelete(&t.tex);
        }
    }
    mImpl->textures.clear();

    if (mImpl->topTarget != nullptr) {
        C3D_RenderTargetDelete(mImpl->topTarget);
    }
    if (mImpl->bottomTarget != nullptr) {
        C3D_RenderTargetDelete(mImpl->bottomTarget);
    }

    if (mImpl->dvlb != nullptr) {
        shaderProgramFree(&mImpl->program);
        DVLB_Free(mImpl->dvlb);
        mImpl->dvlb = nullptr;
    }

    C3D_Fini();

    // Only safe once citro3d can no longer reference it.
    if (mImpl->vertices != nullptr) {
        linearFree(mImpl->vertices);
        mImpl->vertices = nullptr;
    }
    if (mImpl->readback != nullptr) {
        linearFree(mImpl->readback);
        mImpl->readback = nullptr;
        mImpl->readbackBytes = 0;
    }

    // After C3D_Fini, since citro3d's teardown still touches GSP.
    if (mImpl->gfxUp) {
        gfxExit();
        mImpl->gfxUp = false;
        sGfxOwner = nullptr;
    }

    mImpl->c3dUp = false;
    mImpl->ready = false;
}

const char* GfxRenderingAPICitro3D::GetName() {
    return "citro3d";
}

int GfxRenderingAPICitro3D::GetMaxTextureSize() {
    return 1024; // PICA hardware limit
}

GfxClipParameters GfxRenderingAPICitro3D::GetClipParameters() {
    // z in [0,w] keeps the depth remap to PICA's [-w,0] a single subtraction.
    return { true, false };
}

// ---------------------------------------------------------------------------
// Setup
// ---------------------------------------------------------------------------

void GfxRenderingAPICitro3D::Init() {
    // citro3d draws through libctru's graphics subsystem: gfxInitDefault() is
    // what creates the GSP framebuffers, configures both screens, and starts the
    // GSP event thread that signals frame completion. Without it C3D_Init still
    // reports success and textures still upload - they are plain memory - but
    // the first C3D_FrameBegin(C3D_FRAME_SYNCDRAW) waits on a GSP sync that can
    // never arrive, so display list execution stalls on frame one with no fault.
    //
    // libctru exposes no query for "are graphics up", and gfxInitDefault() is not
    // safe to call twice, so ownership is tracked here. Nothing else in this
    // program initialises graphics; this backend is the only owner.
    if (sGfxOwner == nullptr) {
        gfxInitDefault();
        sGfxOwner = this;
        mImpl->gfxUp = true;
    } else {
        mImpl->gfxUp = false;
    }

    // Every later step, and Shutdown's right to call C3D_Fini, depends on this
    // succeeding. Setting the flag unconditionally would make teardown run C3D
    // calls against state that was never brought up.
    if (!C3D_Init(C3D_DEFAULT_CMDBUF_SIZE)) {
        std::fprintf(stderr, "gfx_citro3d: C3D_Init failed\n");
        if (mImpl->gfxUp) {
            gfxExit();
            mImpl->gfxUp = false;
        }
        return;
    }

    mImpl->dvlb = DVLB_ParseFile((u32*)fast3d_shbin, fast3d_shbin_size);
    if (mImpl->dvlb == nullptr || mImpl->dvlb->numDVLE == 0) {
        std::fprintf(stderr, "gfx_citro3d: vertex shader failed to parse\n");
        C3D_Fini();
        if (mImpl->gfxUp) {
            gfxExit();
            mImpl->gfxUp = false;
            sGfxOwner = nullptr;
        }
        return;
    }
    shaderProgramInit(&mImpl->program);
    shaderProgramSetVsh(&mImpl->program, &mImpl->dvlb->DVLE[0]);
    C3D_BindProgram(&mImpl->program);
    mImpl->uLocProjection = shaderInstanceGetUniformLocation(mImpl->program.vertexShader, "projection");

    C3D_AttrInfo* attr = C3D_GetAttrInfo();
    AttrInfo_Init(attr);
    AttrInfo_AddLoader(attr, 0, GPU_FLOAT, 4);         // position
    AttrInfo_AddLoader(attr, 1, GPU_FLOAT, 2);         // texcoord0
    AttrInfo_AddLoader(attr, 2, GPU_FLOAT, 2);         // texcoord1
    AttrInfo_AddLoader(attr, 3, GPU_UNSIGNED_BYTE, 4); // colour

    mImpl->vertices = static_cast<PackedVertex*>(linearAlloc(kVertexCapacity * sizeof(PackedVertex)));
    if (mImpl->vertices == nullptr) {
        std::fprintf(stderr, "gfx_citro3d: vertex staging allocation failed\n");
        shaderProgramFree(&mImpl->program);
        DVLB_Free(mImpl->dvlb);
        mImpl->dvlb = nullptr;
        C3D_Fini();
        return;
    }

    // citro3d is up, so teardown is now required and safe. This is separate
    // from "the backend is usable" - setup can still fail below, and Shutdown
    // is the single cleanup path for every one of those failures.
    mImpl->c3dUp = true;

    C3D_BufInfo* buf = C3D_GetBufInfo();
    BufInfo_Init(buf);
    BufInfo_Add(buf, mImpl->vertices, sizeof(PackedVertex), 4, 0x3210);

    mImpl->textures.resize(kMaxTextures);
    mImpl->framebuffers.clear();
    mImpl->framebuffers.emplace_back(std::make_unique<Impl::FramebufferSlot>()); // id 0 = screen

    mImpl->topTarget = C3D_RenderTargetCreate(kTopHeight, kTopWidth, GPU_RB_RGBA8, GPU_RB_DEPTH24_STENCIL8);
    mImpl->bottomTarget = C3D_RenderTargetCreate(kBottomHeight, kBottomWidth, GPU_RB_RGBA8, GPU_RB_DEPTH16);
    if (mImpl->topTarget == nullptr || mImpl->bottomTarget == nullptr) {
        std::fprintf(stderr, "gfx_citro3d: render target allocation failed (VRAM exhausted?)\n");
        Shutdown();
        return;
    }

    C3D_RenderTargetSetOutput(mImpl->topTarget, GFX_TOP, GFX_LEFT,
                              GX_TRANSFER_IN_FORMAT(GX_TRANSFER_FMT_RGBA8) |
                                  GX_TRANSFER_OUT_FORMAT(GX_TRANSFER_FMT_RGB8));
    C3D_RenderTargetSetOutput(mImpl->bottomTarget, GFX_BOTTOM, GFX_LEFT,
                              GX_TRANSFER_IN_FORMAT(GX_TRANSFER_FMT_RGBA8) |
                                  GX_TRANSFER_OUT_FORMAT(GX_TRANSFER_FMT_RGB8));

    mImpl->BuildProjection();
    C3D_CullFace(GPU_CULL_NONE); // N64 culling is handled by Fast3D on the CPU

    mImpl->ready = true;
    std::fprintf(stderr, "soh-3ds gfx: citro3d Init OK\n");
}

bool GfxRenderingAPICitro3D::IsReady() const {
    return mImpl->ready;
}

uint64_t GfxRenderingAPICitro3D::GetTriangleCount() const {
    return mImpl->triangleCount;
}

void GfxRenderingAPICitro3D::OnResize() {
}

void GfxRenderingAPICitro3D::SetBottomScreenFramebuffer(int fbId) {
    mImpl->bottomScreenFb = fbId;
    if (fbId > 0 && fbId < static_cast<int>(mImpl->framebuffers.size())) {
        mImpl->framebuffers[fbId]->isBottomScreen = true;
    }
}

int GfxRenderingAPICitro3D::GetBottomScreenFramebuffer() const {
    return mImpl->bottomScreenFb;
}

// ---------------------------------------------------------------------------
// Programs: CCFeatures -> TexEnv
// ---------------------------------------------------------------------------

ShaderProgram* GfxRenderingAPICitro3D::CreateAndLoadNewShader(uint64_t shaderId0, uint64_t shaderId1) {
    CCFeatures cc{};
    gfx_cc_get_features(shaderId0, shaderId1, &cc);

    ShaderProgram& p = mImpl->programs[{ shaderId0, shaderId1 }];
    p.shaderId0 = shaderId0;
    p.shaderId1 = shaderId1;
    std::memcpy(p.combiner, cc.c, sizeof(p.combiner));
    p.twoCycle = cc.opt_2cyc;
    p.useAlpha = cc.opt_alpha;
    p.textureEdge = cc.opt_texture_edge;
    p.alphaThreshold = cc.opt_alpha_threshold;
    p.invisible = cc.opt_invisible;
    p.fog = cc.opt_fog;
    p.grayscale = cc.opt_grayscale;
    p.usedTextures[0] = cc.usedTextures[0];
    p.usedTextures[1] = cc.usedTextures[1];
    std::memcpy(p.clamp, cc.clamp, sizeof(p.clamp));
    p.numInputs = static_cast<uint8_t>(cc.numInputs);

    // Mirror Fast3D's CPU-side vertex packing so DrawTriangles can find each
    // component. Order matches gfx_pc's buf_vbo layout.
    uint8_t off = 4; // xyzw
    for (int i = 0; i < 2; ++i) {
        if (cc.usedTextures[i]) {
            p.texOffset[i] = off;
            off += 2;
            if (cc.clamp[i][0]) {
                off += 1;
            }
            if (cc.clamp[i][1]) {
                off += 1;
            }
        }
    }
    if (cc.opt_fog) {
        p.fogOffset = off;
        off += 4;
    }
    for (int i = 0; i < cc.numInputs; ++i) {
        p.inputOffset[i] = off;
        off += cc.opt_alpha ? 4 : 3;
    }
    if (cc.opt_grayscale) {
        p.grayscaleOffset = off;
        off += 4;
    }
    p.strideFloats = off;

    // One shading input can ride the vertex-colour attribute; prefer the first.
    p.varyingInput = cc.numInputs > 0 ? SHADER_INPUT_1 : -1;

    // Classify inputs that are pure multiplicative factors, so DrawTriangles may
    // fold several varying ones into the single colour attribute.
    //
    // An input qualifies only if *every* occurrence of it, across both cycles and
    // both channels, sits in a channel of the form A * C (B == 0 && D == 0) and
    // occupies the A or C slot. lower_channel() lowers exactly that case to a
    // single MULTIPLY stage, which is what makes the factors commute.
    //
    // Anything else disqualifies the input: as D it *is* the result rather than a
    // factor, and as the C of a mix (B == D) or of the general form it scales a
    // difference, so hoisting it out of the channel would change the value.
    uint8_t pureFactors = 0xFF; // start optimistic, clear on first bad use
    for (int cyc = 0; cyc < 2; ++cyc) {
        for (int ch = 0; ch < 2; ++ch) {
            const int* t = cc.c[cyc][ch];
            const bool isProduct = (t[1] == SHADER_0 && t[3] == SHADER_0 && t[2] != SHADER_0);
            for (int slot = 0; slot < 4; ++slot) {
                const int token = t[slot];
                if (token < SHADER_INPUT_1 || token > SHADER_INPUT_7) {
                    continue;
                }
                const bool factorSlot = isProduct && (slot == 0 || slot == 2);
                if (!factorSlot) {
                    pureFactors &= static_cast<uint8_t>(~(1u << (token - SHADER_INPUT_1)));
                }
            }
        }
    }
    p.pureFactorInputs = pureFactors;

    return &p;
}

ShaderProgram* GfxRenderingAPICitro3D::LookupShader(uint64_t shaderId0, uint64_t shaderId1) {
    auto it = mImpl->programs.find({ shaderId0, shaderId1 });
    return it == mImpl->programs.end() ? nullptr : &it->second;
}

void GfxRenderingAPICitro3D::UnloadShader(ShaderProgram*) {
}

void GfxRenderingAPICitro3D::ClearShaderCache() {
    mImpl->programs.clear();
    mImpl->current = nullptr;
}

void GfxRenderingAPICitro3D::ShaderGetInfo(ShaderProgram* prg, uint8_t* numInputs, bool usedTextures[2]) {
    if (prg == nullptr) {
        return;
    }
    *numInputs = prg->numInputs;
    usedTextures[0] = prg->usedTextures[0];
    usedTextures[1] = prg->usedTextures[1];
}

void GfxRenderingAPICitro3D::ConfigureTexEnv(ShaderProgram* prg, int varying) {
    prg->boundVarying = varying;

    // Buffer state is global and persists across draws: a program that used it
    // would otherwise corrupt the next one that does not (DESIGN section 3.3
    // rule 3).
    C3D_TexEnvBufUpdate(C3D_Both, 0);
    C3D_TexEnvBufColor(0xFFFFFFFF);

    int stage = 0;
    const int cycles = prg->twoCycle ? 2 : 1;

    for (int cycle = 0; cycle < cycles && stage < 6; ++cycle) {
        // lower_channel() picks the shape with placeholder operands;
        // bind_sources() resolves them to real hardware sources. Skipping the
        // second step configures the GPU to sample textures the combiner never
        // named.
        pica::Plan rgb = pica::lower_channel(prg->combiner[cycle][0]);
        pica::bind_sources(rgb, prg->combiner[cycle][0], cycle, varying);

        pica::Plan alpha;
        if (prg->useAlpha) {
            alpha = pica::lower_channel(prg->combiner[cycle][1]);
            pica::bind_sources(alpha, prg->combiner[cycle][1], cycle, varying);
        }

        // A cycle costs max(rgb, alpha) stages: the two channels share a stage.
        const int count = std::max(rgb.count, prg->useAlpha ? alpha.count : 1);
        for (int i = 0; i < count && stage < 6; ++i, ++stage) {
            C3D_TexEnv* env = C3D_GetTexEnv(stage);
            C3D_TexEnvInit(env);

            const bool haveRgb = i < rgb.count;
            const pica::Stage& rs = haveRgb ? rgb.stage[i] : rgb.stage[rgb.count - 1];
            if (haveRgb) {
                C3D_TexEnvSrc(env, C3D_RGB, ToTevSrc(rs.src[0]), ToTevSrc(rs.src[1]), ToTevSrc(rs.src[2]));
                C3D_TexEnvOpRgb(env, ToTevOpRgb(rs.op[0]), ToTevOpRgb(rs.op[1]), ToTevOpRgb(rs.op[2]));
                C3D_TexEnvFunc(env, C3D_RGB, ToTevFunc(rs.func));
            } else {
                C3D_TexEnvSrc(env, C3D_RGB, GPU_PREVIOUS, GPU_PREVIOUS, GPU_PREVIOUS);
                C3D_TexEnvFunc(env, C3D_RGB, GPU_REPLACE);
            }

            if (prg->useAlpha) {
                const bool haveA = i < alpha.count;
                const pica::Stage& as = haveA ? alpha.stage[i] : alpha.stage[alpha.count - 1];
                if (haveA) {
                    C3D_TexEnvSrc(env, C3D_Alpha, ToTevSrc(as.src[0]), ToTevSrc(as.src[1]), ToTevSrc(as.src[2]));
                    C3D_TexEnvOpAlpha(env, ToTevOpAlpha(as.op[0]), ToTevOpAlpha(as.op[1]), ToTevOpAlpha(as.op[2]));
                    C3D_TexEnvFunc(env, C3D_Alpha, ToTevFunc(as.func));
                } else {
                    C3D_TexEnvSrc(env, C3D_Alpha, GPU_PREVIOUS, GPU_PREVIOUS, GPU_PREVIOUS);
                    C3D_TexEnvFunc(env, C3D_Alpha, GPU_REPLACE);
                }
            } else {
                // Opaque. The constant register is zero, so one-minus reads 1.
                C3D_TexEnvSrc(env, C3D_Alpha, GPU_CONSTANT, GPU_CONSTANT, GPU_CONSTANT);
                C3D_TexEnvOpAlpha(env, GPU_TEVOP_A_ONE_MINUS_SRC_ALPHA, GPU_TEVOP_A_ONE_MINUS_SRC_ALPHA,
                                  GPU_TEVOP_A_ONE_MINUS_SRC_ALPHA);
                C3D_TexEnvFunc(env, C3D_Alpha, GPU_REPLACE);
            }

            // The constant register is shared by this stage's colour and alpha
            // halves, so both plans constrain it. Zero is the default: SHADER_0
            // reads 0 from it and SHADER_1 reads 1 via the one-minus operand.
            // A stage that instead needs a shading input's colour gets it
            // loaded per draw, once vertex data is available.
            C3D_TexEnvColor(env, 0x00000000);

            pica::ConstantNeed need;
            if (haveRgb) {
                need.add(rs);
            }
            if (prg->useAlpha && i < alpha.count) {
                need.add(alpha.stage[i]);
            }
            mImpl->stageConstToken[stage] = static_cast<int8_t>(need.token);

            if (need.conflict && !prg->unsupportedReported) {
                prg->unsupportedReported = true;
                mImpl->ReportUnsupported(prg, "stage needs two different TexEnv constants");
            }
        }
    }

    mImpl->activeStages = stage;
    for (int i = stage; i < 6; ++i) {
        C3D_TexEnvInit(C3D_GetTexEnv(i));
        mImpl->stageConstToken[i] = -1;
    }

    // Fixed-function alpha test; costs no TexEnv stage.
    if (prg->textureEdge || prg->alphaThreshold) {
        C3D_AlphaTest(true, GPU_GREATER, prg->textureEdge ? 0x30 : 0x08);
    } else {
        C3D_AlphaTest(false, GPU_ALWAYS, 0);
    }
}

void GfxRenderingAPICitro3D::LoadShader(ShaderProgram* newPrg) {
    mImpl->current = newPrg;
    if (newPrg == nullptr) {
        return;
    }
    // Configure with the varying decided by the previous draw of this program.
    // First use guesses INPUT_1; DrawTriangles corrects it from real data.
    ConfigureTexEnv(newPrg, newPrg->boundVarying == -2 ? newPrg->varyingInput : newPrg->boundVarying);
}

// ---------------------------------------------------------------------------
// Textures
// ---------------------------------------------------------------------------

uint32_t GfxRenderingAPICitro3D::NewTexture() {
    for (uint32_t i = 1; i < mImpl->textures.size(); ++i) {
        if (!mImpl->textures[i].initialized) {
            mImpl->textures[i].initialized = true;
            return i;
        }
    }
    return 0;
}

void GfxRenderingAPICitro3D::SelectTexture(int tile, uint32_t textureId) {
    mImpl->activeTile = tile & 1;
    mImpl->boundTexture[mImpl->activeTile] = textureId;
    if (textureId >= mImpl->textures.size()) {
        // Every other entry point bounds-checks; this one indexed straight in.
        // NewTexture() also returns 0 when the pool is full, so an exhausted
        // pool would land on the reserved slot rather than out of range.
        return;
    }
    auto& slot = mImpl->textures[textureId];
    if (slot.tex.data != nullptr) {
        C3D_TexBind(mImpl->activeTile, &slot.tex);
    }
}

void GfxRenderingAPICitro3D::UploadTexture(const uint8_t* rgba32Buf, uint32_t width, uint32_t height) {
    const uint32_t id = mImpl->boundTexture[mImpl->activeTile];
    if (id == 0 || id >= mImpl->textures.size() || width == 0 || height == 0) {
        return;
    }
    auto& slot = mImpl->textures[id];

    const uint16_t tw = NextPow2(width);
    const uint16_t th = NextPow2(height);

    if (slot.tex.data == nullptr || slot.tex.width != tw || slot.tex.height != th) {
        if (slot.tex.data != nullptr) {
            C3D_TexDelete(&slot.tex);
        }
        if (!C3D_TexInit(&slot.tex, tw, th, GPU_RGBA8)) {
            return;
        }
    }

    // NPOT textures are padded to POT by *wrapping* the source, not zero-filling:
    // zero padding bleeds transparent texels into bilinear samples at the edge.
    // The residual scale is folded into UVs at draw time.
    slot.scaleS = static_cast<float>(width) / static_cast<float>(tw);
    slot.scaleT = static_cast<float>(height) / static_cast<float>(th);

    uint32_t* dst = static_cast<uint32_t*>(slot.tex.data);
    const uint32_t* src = reinterpret_cast<const uint32_t*>(rgba32Buf);

    for (uint16_t y = 0; y < th; ++y) {
        // PICA samples V=0 from the last row of the power-of-two *backing*
        // store, not of the logical image, so the flip must be anchored to th
        // rather than height. Getting this wrong is invisible on POT textures
        // and wraps visibly on NPOT ones - a 240-row image in a 256-row texture
        // would start 16 rows out. Technique from mario-kart-64-3ds
        // (platform/3ds/source/gfx_citro3d.cpp, SourceRowForBackingRow).
        const uint32_t sy = SourceRowForBackingRow(th, height, y);
        for (uint16_t x = 0; x < tw; ++x) {
            const uint32_t sx = x % width; // wrap, so filtering cannot sample padding
            const uint32_t texel = src[sy * width + sx];
            const uint32_t tileIndex = kMorton.x[x & 7] + kMorton.y[y & 7];
            const uint32_t offset = tileIndex + ((x & ~7u) * 8) + ((y & ~7u) * tw);
            // PICA wants A-B-G-R for RGBA8.
            dst[offset] = __builtin_bswap32(texel);
        }
    }

    C3D_TexFlush(&slot.tex);

    // Fast3D caches sampler state and skips redundant SetSamplerParameters
    // calls, so a freshly uploaded texture must start from the same defaults
    // the interpreter assumes on a cache miss, and be bound.
    C3D_TexSetFilter(&slot.tex, GPU_NEAREST, GPU_NEAREST);
    C3D_TexSetWrap(&slot.tex, GPU_REPEAT, GPU_REPEAT);
    C3D_TexBind(mImpl->activeTile, &slot.tex);
}

void GfxRenderingAPICitro3D::SetSamplerParameters(int sampler, bool linearFilter, uint32_t cms, uint32_t cmt) {
    const uint32_t id = mImpl->boundTexture[sampler & 1];
    if (id == 0 || id >= mImpl->textures.size()) {
        return;
    }
    auto& slot = mImpl->textures[id];
    if (slot.tex.data == nullptr) {
        return;
    }

    const GPU_TEXTURE_FILTER_PARAM f = linearFilter ? GPU_LINEAR : GPU_NEAREST;
    C3D_TexSetFilter(&slot.tex, f, f);

    auto wrap = [](uint32_t cm) -> GPU_TEXTURE_WRAP_PARAM {
        if (cm & 2u) {
            return GPU_CLAMP_TO_EDGE;
        }
        return (cm & 1u) ? GPU_MIRRORED_REPEAT : GPU_REPEAT;
    };
    C3D_TexSetWrap(&slot.tex, wrap(cms), wrap(cmt));
}

void GfxRenderingAPICitro3D::DeleteTexture(uint32_t texId) {
    if (texId == 0 || texId >= mImpl->textures.size()) {
        return;
    }
    auto& slot = mImpl->textures[texId];
    if (slot.tex.data != nullptr) {
        C3D_TexDelete(&slot.tex);
    }
    slot = Impl::TextureSlot{};
}

void GfxRenderingAPICitro3D::SetTextureFilter(FilteringMode mode) {
    // FILTER_THREE_POINT needs dependent fragment fetches; PICA cannot. Treat it
    // as linear, which is what the desktop default is anyway.
    mImpl->filterMode = (mode == FILTER_NONE) ? FILTER_NONE : FILTER_LINEAR;
}

FilteringMode GfxRenderingAPICitro3D::GetTextureFilter() {
    return mImpl->filterMode;
}

// ---------------------------------------------------------------------------
// Render state
// ---------------------------------------------------------------------------

void GfxRenderingAPICitro3D::SetDepthTestAndMask(bool depthTest, bool zUpd) {
    mImpl->depthTest = depthTest;
    mImpl->depthMask = zUpd;
    C3D_DepthTest(depthTest, GPU_LEQUAL, zUpd ? GPU_WRITE_ALL : GPU_WRITE_COLOR);
}

void GfxRenderingAPICitro3D::SetZmodeDecal(bool decal) {
    mImpl->zmodeDecal = decal;
    // No polygon-offset register on PICA. A small constant depth bias is the
    // right emulation because N64 decals are coplanar with their base surface.
    // The magnitude is a hardware calibration constant (DESIGN section 5.2).
    C3D_DepthMap(true, -1.0f, decal ? -0.0005f : 0.0f);
}

void GfxRenderingAPICitro3D::SetViewport(int x, int y, int width, int height) {
    mImpl->viewport = { x, y, width, height };
    mImpl->ApplyViewportScissor();
}

void GfxRenderingAPICitro3D::SetScissor(int x, int y, int width, int height) {
    mImpl->scissor = { x, y, width, height };
    mImpl->ApplyViewportScissor();
}

void GfxRenderingAPICitro3D::SetUseAlpha(bool useAlpha) {
    mImpl->useAlpha = useAlpha;
    if (useAlpha) {
        C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_SRC_ALPHA, GPU_ONE_MINUS_SRC_ALPHA, GPU_SRC_ALPHA,
                       GPU_ONE_MINUS_SRC_ALPHA);
    } else {
        C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_ONE, GPU_ZERO, GPU_ONE, GPU_ZERO);
    }
}

void GfxRenderingAPICitro3D::SetSrgbMode() {
}

void GfxRenderingAPICitro3D::SetCurrentPrimDepth(float depth) {
    mImpl->primDepth = depth;
}

// ---------------------------------------------------------------------------
// Drawing
// ---------------------------------------------------------------------------

void GfxRenderingAPICitro3D::DrawTriangles(float bufVbo[], size_t bufVboLen, size_t bufVboNumTris) {
    ShaderProgram* p = mImpl->current;
    if (p == nullptr || p->strideFloats == 0 || !mImpl->frameActive) {
        return;
    }

    const size_t verts = bufVboNumTris * 3;
    if (mImpl->vertexCount + verts > kVertexCapacity) {
        return; // buffer full for this frame; dropping beats corrupting
    }
    (void)bufVboLen;

    // Decide which shading input rides the vertex-colour attribute.
    //
    // It is not always INPUT_1: Fast3D assigns SHADE to whichever slot PRIM/ENV
    // left free, so the per-vertex input can be any of them. Measure it rather
    // than assume - treating a per-vertex input as a draw constant corrupts
    // ordinary shaded triangles, and the corruption is invisible in a flat-lit
    // test scene.
    int varying = -1;
    uint8_t varyingMask = 0;
    int varyingCount = 0;
    for (int n = 0; n < p->numInputs; ++n) {
        const float* first = &bufVbo[p->inputOffset[n]];
        const int comps = p->useAlpha ? 4 : 3;
        bool varies = false;
        for (size_t v = 1; v < verts && !varies; ++v) {
            const float* cur = &bufVbo[v * p->strideFloats + p->inputOffset[n]];
            for (int k = 0; k < comps; ++k) {
                if (cur[k] != first[k]) {
                    varies = true;
                    break;
                }
            }
        }
        if (varies) {
            varyingMask |= static_cast<uint8_t>(1u << n);
            ++varyingCount;
            if (varying < 0) {
                varying = pica::SHADER_INPUT_1 + n;
            }
        }
    }

    // Two or more varying inputs exceed PICA's single colour attribute. When all
    // of them are pure multiplicative factors their product is what the combiner
    // actually needs, and multiplication commutes - so send the per-vertex
    // product through the attribute and give the surplus factors a constant of
    // 1, turning their stages into identity passes. Exact, not approximate.
    const bool foldable = varyingCount > 1 && (varyingMask & ~p->pureFactorInputs) == 0;
    const uint8_t foldMask = foldable ? varyingMask : uint8_t{ 0 };
    if (varyingCount > 1 && !foldable && !p->unsupportedReported) {
        p->unsupportedReported = true;
        char why[160];
        std::snprintf(why, sizeof(why),
                      "%d varying shading inputs, varying=0x%02X pureFactors=0x%02X numInputs=%u; PICA has one "
                      "colour attribute and these do not all commute",
                      varyingCount, (unsigned)varyingMask, (unsigned)p->pureFactorInputs, (unsigned)p->numInputs);
        mImpl->ReportUnsupported(p, why);
    }
    // With nothing varying, any input can take the attribute slot; keeping the
    // existing binding avoids a pointless TexEnv reconfigure between draws.
    if (varying < 0 && p->numInputs > 0 && p->boundVarying != -2) {
        varying = p->boundVarying;
    }
    if (varying != p->boundVarying) {
        ConfigureTexEnv(p, varying);
    }

    // Load each stage's constant from the inputs that are genuinely uniform
    // across this draw.
    for (int s = 0; s < mImpl->activeStages; ++s) {
        const int8_t token = mImpl->stageConstToken[s];
        if (token < pica::SHADER_INPUT_1) {
            continue;
        }
        const int n = token - pica::SHADER_INPUT_1;
        if (n >= p->numInputs) {
            continue;
        }
        if (foldMask & (1u << n)) {
            // Folded into the attribute's product. A constant of 1 makes this
            // stage an identity pass, so the factor is applied exactly once.
            C3D_TexEnvColor(C3D_GetTexEnv(s), 0xFFFFFFFFu);
            continue;
        }
        const float* c = &bufVbo[p->inputOffset[n]];
        const uint8_t r = static_cast<uint8_t>(std::clamp(c[0], 0.0f, 1.0f) * 255.0f);
        const uint8_t g = static_cast<uint8_t>(std::clamp(c[1], 0.0f, 1.0f) * 255.0f);
        const uint8_t b = static_cast<uint8_t>(std::clamp(c[2], 0.0f, 1.0f) * 255.0f);
        const uint8_t a = p->useAlpha ? static_cast<uint8_t>(std::clamp(c[3], 0.0f, 1.0f) * 255.0f) : 0xFF;
        C3D_TexEnvColor(C3D_GetTexEnv(s), (static_cast<uint32_t>(a) << 24) | (static_cast<uint32_t>(b) << 16) |
                                              (static_cast<uint32_t>(g) << 8) | r);
    }

    const size_t base = mImpl->vertexCount;
    for (size_t v = 0; v < verts; ++v) {
        const float* in = &bufVbo[v * p->strideFloats];
        PackedVertex& out = mImpl->vertices[base + v];

        out.pos[0] = in[0];
        out.pos[1] = in[1];
        out.pos[2] = in[2];
        out.pos[3] = in[3];

        for (int t = 0; t < 2; ++t) {
            if (p->usedTextures[t]) {
                const float* uv = &in[p->texOffset[t]];
                const auto& slot = mImpl->textures[mImpl->boundTexture[t]];
                // Fold the NPOT padding scale into the coordinates.
                float* dstUv = (t == 0) ? out.uv0 : out.uv1;
                dstUv[0] = uv[0] * slot.scaleS;
                dstUv[1] = uv[1] * slot.scaleT;
            } else {
                float* dstUv = (t == 0) ? out.uv0 : out.uv1;
                dstUv[0] = 0.0f;
                dstUv[1] = 0.0f;
            }
        }

        // The attribute carries the input the TexEnv was configured to read as
        // PRIMARY_COLOR, which is `varying` - not blindly input 0. When several
        // varying inputs were folded, it carries their componentwise product;
        // the surplus factors were bound to a constant 1 above.
        if (varying >= pica::SHADER_INPUT_1) {
            float rgba[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
            for (int n = 0; n < p->numInputs; ++n) {
                const bool isCarrier = (n == varying - pica::SHADER_INPUT_1);
                if (!isCarrier && (foldMask & (1u << n)) == 0) {
                    continue;
                }
                const float* c = &in[p->inputOffset[n]];
                rgba[0] *= c[0];
                rgba[1] *= c[1];
                rgba[2] *= c[2];
                rgba[3] *= p->useAlpha ? c[3] : 1.0f;
            }
            out.rgba[0] = static_cast<uint8_t>(std::clamp(rgba[0], 0.0f, 1.0f) * 255.0f);
            out.rgba[1] = static_cast<uint8_t>(std::clamp(rgba[1], 0.0f, 1.0f) * 255.0f);
            out.rgba[2] = static_cast<uint8_t>(std::clamp(rgba[2], 0.0f, 1.0f) * 255.0f);
            out.rgba[3] = static_cast<uint8_t>(std::clamp(rgba[3], 0.0f, 1.0f) * 255.0f);
        } else {
            out.rgba[0] = out.rgba[1] = out.rgba[2] = out.rgba[3] = 255;
        }
    }
    mImpl->vertexCount += verts;
    mImpl->triangleCount += bufVboNumTris;

    C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, mImpl->uLocProjection, mImpl->activeProjection);
    C3D_DrawArrays(GPU_TRIANGLES, static_cast<int>(base), static_cast<int>(verts));
}

// ---------------------------------------------------------------------------
// Frame lifecycle
// ---------------------------------------------------------------------------

// Re-establish this backend's pipeline state.
//
// citro2d owns the shader, attribute layout, buffer bindings and most render
// state while it draws, so anything it touched has to be put back before the
// game's display lists run again. Restoring at the top of every frame rather
// than after each C2D batch is the ordering mario-kart-64-3ds uses
// (platform/3ds/source/gfx_citro3d.cpp, RestoreFast3DState) and it is the
// cheaper place: once per frame instead of once per UI pass.
void GfxRenderingAPICitro3D::RestoreFast3DState() {
    C3D_BindProgram(&mImpl->program);

    C3D_AttrInfo* attr = C3D_GetAttrInfo();
    AttrInfo_Init(attr);
    AttrInfo_AddLoader(attr, 0, GPU_FLOAT, 4);         // position
    AttrInfo_AddLoader(attr, 1, GPU_FLOAT, 2);         // texcoord0
    AttrInfo_AddLoader(attr, 2, GPU_FLOAT, 2);         // texcoord1
    AttrInfo_AddLoader(attr, 3, GPU_UNSIGNED_BYTE, 4); // colour

    C3D_BufInfo* buf = C3D_GetBufInfo();
    BufInfo_Init(buf);
    BufInfo_Add(buf, mImpl->vertices, sizeof(PackedVertex), 4, 0x3210);

    // Force the next draw to reconfigure TexEnv: the cached decision is about
    // this backend's stages, which citro2d has since overwritten.
    if (mImpl->current != nullptr) {
        mImpl->current->boundVarying = -2;
    }
    C3D_CullFace(GPU_CULL_NONE);
    mImpl->ApplyViewportScissor();
}

void GfxRenderingAPICitro3D::StartFrame() {
    if (mImpl->frameActive) {
        // Defensive, not the observed failure: on this tree only Interpreter::Run
        // calls into here, and Interpreter::StartFrame does not. But a second
        // C3D_FrameBegin without an intervening FrameEnd would be a real fault,
        // so re-entry re-targets the top screen rather than opening a frame.
        C3D_FrameDrawOn(mImpl->topTarget);
        mImpl->activeTarget = mImpl->topTarget;
        return;
    }
    C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
    mImpl->frameActive = true;
    mImpl->vertexCount = 0;
    mImpl->flushedUpTo = 0;

    C3D_RenderTargetClear(mImpl->topTarget, C3D_CLEAR_ALL, 0x000000FF, 0);
    C3D_RenderTargetClear(mImpl->bottomTarget, C3D_CLEAR_ALL, 0x000000FF, 0);

    C3D_FrameDrawOn(mImpl->topTarget);
    mImpl->activeTarget = mImpl->topTarget;
    mImpl->activeProjection = &mImpl->projScreen;
    mImpl->activeIsScreen = true;
    if (mImpl->c2dDrewLastFrame) {
        RestoreFast3DState();
        mImpl->c2dDrewLastFrame = false;
    }

    for (auto& fb : mImpl->framebuffers) {
        fb->needsClear = true;
    }
}

void GfxRenderingAPICitro3D::EndFrame() {
    if (!mImpl->frameActive) {
        return;
    }
    mImpl->FlushVertices();

    // citro2d writes its own linear vertex/index buffers, so a frame that
    // submitted a C2D batch needs citro3d's broad linear-heap coherency pass
    // rather than the narrower command-list flush. Same reasoning as
    // mario-kart-64-3ds (platform/3ds/source/gfx_citro3d.cpp, EndFrame).
    C3D_FrameEnd(mImpl->c2dDrewLastFrame ? 0 : GX_CMDLIST_FLUSH);
    mImpl->frameActive = false;

    // Report presentation periodically. "The game is running" and "the game is
    // drawing" are different claims, and only draw counts settle the second.
    static uint32_t sFrames = 0;
    static uint64_t sDrawsAtLastReport = 0;
    if ((++sFrames % 60) == 0) {
        const uint64_t tris = mImpl->triangleCount;
        std::fprintf(stderr, "soh-3ds frame: %u presented, %llu tris (+%llu since last)\n", sFrames,
                     (unsigned long long)tris, (unsigned long long)(tris - sDrawsAtLastReport));
        sDrawsAtLastReport = tris;
    }
}

void GfxRenderingAPICitro3D::FinishRender() {
}

// ---------------------------------------------------------------------------
// Framebuffers
// ---------------------------------------------------------------------------

int GfxRenderingAPICitro3D::CreateFramebuffer() {
    mImpl->framebuffers.emplace_back(std::make_unique<Impl::FramebufferSlot>());
    return static_cast<int>(mImpl->framebuffers.size() - 1);
}

void GfxRenderingAPICitro3D::UpdateFramebufferParameters(int fbId, uint32_t width, uint32_t height, uint32_t msaaLevel,
                                                         bool openglInvertY, bool renderTarget, bool hasDepthBuffer,
                                                         bool canExtractDepth) {
    (void)msaaLevel;
    (void)openglInvertY;
    (void)renderTarget;
    (void)canExtractDepth;

    if (fbId <= 0 || fbId >= static_cast<int>(mImpl->framebuffers.size()) || width == 0 || height == 0) {
        return;
    }
    auto& slot = *mImpl->framebuffers[fbId];

    // The bottom-screen framebuffer is the physical touch screen, already
    // created in Init(); it needs no texture of its own.
    if (fbId == mImpl->bottomScreenFb) {
        slot.isBottomScreen = true;
        slot.logicalW = kBottomWidth;
        slot.logicalH = kBottomHeight;
        slot.initialized = true;
        return;
    }

    const uint16_t tw = NextPow2(width);
    const uint16_t th = NextPow2(height);
    if (slot.initialized && slot.logicalW == width && slot.logicalH == height) {
        return;
    }

    if (slot.target != nullptr) {
        C3D_RenderTargetDelete(slot.target);
        slot.target = nullptr;
    }
    if (slot.texture.data != nullptr) {
        C3D_TexDelete(&slot.texture);
    }
    if (!C3D_TexInitVRAM(&slot.texture, tw, th, GPU_RGBA8)) {
        return;
    }
    slot.target = C3D_RenderTargetCreateFromTex(&slot.texture, GPU_TEXFACE_2D, 0,
                                                hasDepthBuffer ? C3D_DEPTHTYPE(GPU_RB_DEPTH16) : C3D_DEPTHTYPE(-1));
    if (slot.target == nullptr) {
        C3D_TexDelete(&slot.texture);
        return;
    }
    slot.initialized = true;
    slot.needsClear = true;
    slot.logicalW = static_cast<uint16_t>(width);
    slot.logicalH = static_cast<uint16_t>(height);
    C3D_TexSetFilter(&slot.texture, GPU_LINEAR, GPU_LINEAR);
    C3D_TexSetWrap(&slot.texture, GPU_CLAMP_TO_EDGE, GPU_CLAMP_TO_EDGE);
}

void GfxRenderingAPICitro3D::StartDrawToFramebuffer(int fbId, float noiseScale) {
    (void)noiseScale;
    if (!mImpl->frameActive) {
        return;
    }

    C3D_RenderTarget* target = mImpl->topTarget;
    Impl::FramebufferSlot* slot = nullptr;
    mImpl->activeProjection = &mImpl->projScreen;
    mImpl->activeIsScreen = true;

    if (fbId > 0 && fbId < static_cast<int>(mImpl->framebuffers.size())) {
        slot = mImpl->framebuffers[fbId].get();
        if (slot->isBottomScreen) {
            target = mImpl->bottomTarget; // dual screen: straight to the touch screen
        } else if (slot->initialized && slot->target != nullptr) {
            target = slot->target;
            // MEASURED by tests/orient_3ds.cpp on 2026-08-30: a marker drawn in
            // the top-left clip quadrant lands in the target's top-left, so
            // render-to-texture surfaces are upright and must NOT take the
            // screens' -90 rotation.
            mImpl->activeProjection = &mImpl->projOffscreen;
            mImpl->activeIsScreen = false;
        }
    }

    if (mImpl->activeTarget != target) {
        // The GPU has no hazard tracking; a target must be split before its
        // texture can be sampled. The split may start executing queued draws,
        // so staged vertices have to be visible to the GPU first.
        mImpl->FlushVertices();
        C3D_FrameSplit(GX_CMDLIST_FLUSH);
        C3D_FrameDrawOn(target);
        mImpl->activeTarget = target;
    }
    mImpl->ApplyViewportScissor();

    if (slot != nullptr && slot->needsClear && !slot->isBottomScreen) {
        C3D_RenderTargetClear(target, C3D_CLEAR_ALL, 0x00000000, 0);
        slot->needsClear = false;
    }
}

void GfxRenderingAPICitro3D::CopyFramebuffer(int fbDstId, int fbSrcId, int srcX0, int srcY0, int srcX1, int srcY1,
                                             int dstX0, int dstY0, int dstX1, int dstY1) {
    (void)fbDstId;
    (void)fbSrcId;
    (void)srcX0;
    (void)srcY0;
    (void)srcX1;
    (void)srcY1;
    (void)dstX0;
    (void)dstY0;
    (void)dstX1;
    (void)dstY1;
}

void GfxRenderingAPICitro3D::ClearFramebuffer(bool color, bool depth) {
    if (!mImpl->frameActive || mImpl->activeTarget == nullptr) {
        return;
    }
    C3D_ClearBits bits = static_cast<C3D_ClearBits>((color ? C3D_CLEAR_COLOR : 0) | (depth ? C3D_CLEAR_DEPTH : 0));
    if (bits != 0) {
        C3D_RenderTargetClear(mImpl->activeTarget, bits, 0x00000000, 0);
    }
}

void GfxRenderingAPICitro3D::ClearDepthRegion(int x, int y, int w, int h) {
    (void)x;
    (void)y;
    (void)w;
    (void)h;
    ClearFramebuffer(false, true);
}

void GfxRenderingAPICitro3D::ReadFramebufferToCPU(int fbId, uint32_t width, uint32_t height, uint16_t* rgba16Buf) {
    if (rgba16Buf == nullptr || width == 0 || height == 0) {
        return;
    }
    const size_t pixels = static_cast<size_t>(width) * height;

    if (fbId <= 0 || fbId >= static_cast<int>(mImpl->framebuffers.size())) {
        std::fill_n(rgba16Buf, pixels, uint16_t{ 0 });
        return;
    }
    auto& slot = *mImpl->framebuffers[fbId];
    if (!slot.initialized || slot.isBottomScreen || slot.texture.data == nullptr) {
        std::fill_n(rgba16Buf, pixels, uint16_t{ 0 });
        return;
    }

    const uint16_t tw = slot.texture.width;
    const uint16_t th = slot.texture.height;

    // Hand the de-tiling to the transfer engine rather than walking Morton
    // order on the CPU: the surface is tiled RGBA8 in VRAM, which the CPU
    // cannot read coherently anyway, and the engine converts format in the
    // same pass.
    const size_t stagingBytes = static_cast<size_t>(tw) * th * 2;
    if (mImpl->readbackBytes < stagingBytes) {
        if (mImpl->readback != nullptr) {
            linearFree(mImpl->readback);
        }
        mImpl->readback = static_cast<uint16_t*>(linearAlloc(stagingBytes));
        mImpl->readbackBytes = mImpl->readback != nullptr ? stagingBytes : 0;
    }
    if (mImpl->readback == nullptr) {
        std::fill_n(rgba16Buf, pixels, uint16_t{ 0 });
        return;
    }

    // Any queued drawing into this target has to complete first.
    if (mImpl->frameActive) {
        mImpl->FlushVertices();
        C3D_FrameSplit(GX_CMDLIST_FLUSH);
    }

    C3D_SyncDisplayTransfer(static_cast<u32*>(slot.texture.data), GX_BUFFER_DIM(tw, th),
                            reinterpret_cast<u32*>(mImpl->readback), GX_BUFFER_DIM(tw, th),
                            GX_TRANSFER_IN_FORMAT(GX_TRANSFER_FMT_RGBA8) |
                                GX_TRANSFER_OUT_FORMAT(GX_TRANSFER_FMT_RGB5A1) | GX_TRANSFER_OUT_TILED(0));

    GSPGPU_InvalidateDataCache(mImpl->readback, static_cast<u32>(stagingBytes));

    // Row order is measured, not assumed: tests/orient_3ds.cpp renders an
    // asymmetric marker and reports where it lands. The display-transfer engine
    // already emits rows in raster order for a CreateFromTex surface, so an
    // extra flip here would invert every readback - which is exactly what the
    // probe caught. Re-run that probe if this ever looks upside down.
    for (uint32_t y = 0; y < height; ++y) {
        const uint16_t* src = mImpl->readback + static_cast<size_t>(y) * tw;
        std::copy_n(src, std::min<uint32_t>(width, tw), rgba16Buf + static_cast<size_t>(y) * width);
    }
}

void GfxRenderingAPICitro3D::ResolveMSAAColorBuffer(int, int) {
}

std::unordered_map<std::pair<float, float>, uint16_t, hash_pair_ff>
GfxRenderingAPICitro3D::GetPixelDepth(int fbId, const std::set<std::pair<float, float>>& coordinates) {
    (void)fbId;
    // Returning 0 rather than a stub map keeps callers well-defined; a real
    // implementation copies only the 8x8 tiles covering these coordinates via
    // C3D_SyncTextureCopy (DESIGN section 7.2).
    std::unordered_map<std::pair<float, float>, uint16_t, hash_pair_ff> out;
    for (const auto& c : coordinates) {
        out[c] = 0;
    }
    return out;
}

void* GfxRenderingAPICitro3D::GetFramebufferTextureId(int fbId) {
    if (fbId <= 0 || fbId >= static_cast<int>(mImpl->framebuffers.size())) {
        return nullptr;
    }
    auto& slot = *mImpl->framebuffers[fbId];
    return slot.initialized && !slot.isBottomScreen ? &slot.texture : nullptr;
}

void GfxRenderingAPICitro3D::SelectTextureFb(int fbId) {
    if (fbId <= 0 || fbId >= static_cast<int>(mImpl->framebuffers.size())) {
        return;
    }
    auto& slot = *mImpl->framebuffers[fbId];
    if (slot.initialized && !slot.isBottomScreen) {
        C3D_TexBind(0, &slot.texture);
    }
}

ImTextureID GfxRenderingAPICitro3D::GetTextureById(int id) {
    if (id <= 0 || id >= static_cast<int>(mImpl->textures.size())) {
        return nullptr;
    }
    return reinterpret_cast<ImTextureID>(&mImpl->textures[id].tex);
}

} // namespace Fast

namespace Fast {

// Declared by Fast3dWindow.cpp; defined here so libultraship needs no include
// dependency on citro3d.
GfxRenderingAPI* CreateCitro3DRenderingAPI() {
    std::fprintf(stderr, "soh-3ds gfx: CreateCitro3DRenderingAPI\n");
    return new GfxRenderingAPICitro3D();
}

} // namespace Fast
