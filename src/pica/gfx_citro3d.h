// Fast3D PICA200 backend for the 3DS.
//
// Implements Fast::GfxRenderingAPI against citro3d. See DESIGN-PICA-BACKEND.md
// for the contract; the short version is that PICA200 has no programmable
// fragment stage, so a "shader program" here is a cached TexEnv register
// configuration built from CCFeatures rather than compiled code.

#pragma once

#include <cstdint>
#include <memory>
#include <set>
#include <unordered_map>
#include <utility>

#include "fast/backends/gfx_rendering_api.h"

namespace Fast {

// Fast3D forward-declares this and lets the backend define it. Ours holds the
// decoded combiner plus the CPU-side vertex layout Fast3D packs to.
struct ShaderProgram {
    uint64_t shaderId0 = 0;
    uint64_t shaderId1 = 0;

    // Decoded N64 combiner: [cycle][channel][A,B,C,D].
    int combiner[2][2][4] = {};
    bool twoCycle = false;
    bool useAlpha = false;
    bool textureEdge = false;
    bool alphaThreshold = false;
    bool invisible = false;
    bool fog = false;
    bool grayscale = false;
    bool usedTextures[2] = {};
    bool clamp[2][2] = {};

    // Vertex layout Fast3D produces for this program, in floats.
    uint8_t numInputs = 0;
    uint8_t strideFloats = 0;
    uint8_t texOffset[2] = {};
    uint8_t inputOffset[7] = {};
    uint8_t fogOffset = 0;
    uint8_t grayscaleOffset = 0;

    // Which shading input rides the vertex-colour attribute; the rest become
    // per-stage TexEnv constants. -1 means none.
    //
    // This cannot be decided when the program is created. Fast3D assigns SHADE
    // to whichever input slot PRIM/ENV left free, so the per-vertex input is not
    // always INPUT_1 - it is whichever one actually varies across a draw's
    // vertices. DrawTriangles measures that and reconfigures if it changed.
    int varyingInput = -1;
    int boundVarying = -2; // -2 = TexEnv never configured for this program

    // Inputs that appear *only* as pure multiplicative factors of their channel
    // result, as a bitmask over input index 0..6.
    //
    // PICA interpolates one colour attribute, so a draw whose vertices vary in
    // two inputs cannot hand both to the hardware. When every varying input is a
    // pure factor the product is associative, so their per-vertex product can
    // ride the single attribute and the surplus factors bind to a constant 1 -
    // making those stages identity passes. That is exact, not an approximation.
    // Inputs used as mix factors or addends do not qualify.
    uint8_t pureFactorInputs = 0;

    // Set once when this program needs something the hardware cannot express,
    // so the failure is reported rather than silently mis-rendered.
    bool unsupportedReported = false;
};

class GfxRenderingAPICitro3D final : public GfxRenderingAPI {
  public:
    GfxRenderingAPICitro3D();
    ~GfxRenderingAPICitro3D() override;

    // Release every GPU resource and shut citro3d down. Must run before the
    // owner calls gfxExit(); the destructor alone is too late for a local.
    // Safe to call more than once.
    void Shutdown();

    // False if Init() failed. Callers must not draw through a backend that
    // never finished coming up.
    bool IsReady() const;

    // Triangles submitted since startup. Lets a caller tell "the display list
    // executed but rasterised nothing" apart from "it drew".
    uint64_t GetTriangleCount() const;

    // Bind a framebuffer id to the physical bottom screen. Everything drawn to
    // that id then lands on the touch screen. This is the dual-screen seam:
    // SoH redirects HUD draws with gsSPSetFB, and Fast3D routes them here.
    void SetBottomScreenFramebuffer(int fbId);
    int GetBottomScreenFramebuffer() const;

    const char* GetName() override;
    int GetMaxTextureSize() override;
    GfxClipParameters GetClipParameters() override;

    void UnloadShader(ShaderProgram* oldPrg) override;
    void LoadShader(ShaderProgram* newPrg) override;
    void ClearShaderCache() override;
    ShaderProgram* CreateAndLoadNewShader(uint64_t shaderId0, uint64_t shaderId1) override;
    ShaderProgram* LookupShader(uint64_t shaderId0, uint64_t shaderId1) override;
    void ShaderGetInfo(ShaderProgram* prg, uint8_t* numInputs, bool usedTextures[2]) override;

    uint32_t NewTexture() override;
    void SelectTexture(int tile, uint32_t textureId) override;
    void UploadTexture(const uint8_t* rgba32Buf, uint32_t width, uint32_t height) override;
    void SetSamplerParameters(int sampler, bool linearFilter, uint32_t cms, uint32_t cmt) override;
    void DeleteTexture(uint32_t texId) override;
    void SetTextureFilter(FilteringMode mode) override;
    FilteringMode GetTextureFilter() override;

    void SetDepthTestAndMask(bool depthTest, bool zUpd) override;
    void SetZmodeDecal(bool decal) override;
    void SetViewport(int x, int y, int width, int height) override;
    void SetScissor(int x, int y, int width, int height) override;
    void SetUseAlpha(bool useAlpha) override;
    void SetSrgbMode() override;
    void SetCurrentPrimDepth(float depth) override;

    void DrawTriangles(float bufVbo[], size_t bufVboLen, size_t bufVboNumTris) override;

    void Init() override;
    void OnResize() override;
    void StartFrame() override;
    void EndFrame() override;
    void FinishRender() override;

    int CreateFramebuffer() override;
    void UpdateFramebufferParameters(int fbId, uint32_t width, uint32_t height, uint32_t msaaLevel,
                                     bool openglInvertY, bool renderTarget, bool hasDepthBuffer,
                                     bool canExtractDepth) override;
    void StartDrawToFramebuffer(int fbId, float noiseScale) override;
    void CopyFramebuffer(int fbDstId, int fbSrcId, int srcX0, int srcY0, int srcX1, int srcY1, int dstX0, int dstY0,
                         int dstX1, int dstY1) override;
    void ClearFramebuffer(bool color, bool depth) override;
    void ClearDepthRegion(int x, int y, int w, int h) override;
    void ReadFramebufferToCPU(int fbId, uint32_t width, uint32_t height, uint16_t* rgba16Buf) override;
    void ResolveMSAAColorBuffer(int fbIdTarget, int fbIdSrc) override;
    std::unordered_map<std::pair<float, float>, uint16_t, hash_pair_ff>
    GetPixelDepth(int fbId, const std::set<std::pair<float, float>>& coordinates) override;
    void* GetFramebufferTextureId(int fbId) override;
    void SelectTextureFb(int fbId) override;

    ImTextureID GetTextureById(int id) override;

    /**
     * @brief Re-establishes this backend's pipeline state after citro2d ran.
     *
     * The bottom-screen UI draws with citro2d, which replaces the shader,
     * attribute layout, buffer bindings and render state. Public so the UI pass
     * can hand control back explicitly.
     */
    void RestoreFast3DState();


  private:
    // Configure the six TexEnv stages for `prg`, routing shading input
    // `varying` through the vertex-colour attribute. Separate from LoadShader
    // because the varying input is only known once vertex data is in hand.
    void ConfigureTexEnv(ShaderProgram* prg, int varying);

    struct Impl;
    std::unique_ptr<Impl> mImpl;
};


} // namespace Fast
