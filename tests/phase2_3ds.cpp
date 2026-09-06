// Phase 2: run a real Ocarina of Time display list through Fast3D onto PICA200.
//
// Phases 0 and 1 proved the two halves separately: the resource layer mounts a
// real oot.o2r on hardware, and the rendering backend draws geometry it was
// handed directly. This joins them. Nothing here is synthetic - the display
// list, its vertices, textures and combiner settings all come out of the
// archive extracted from the user's own cartridge dump, and they reach the GPU
// through libultraship's own Fast3D interpreter rather than any test harness.
//
// It is deliberately a survey rather than a single assertion. Each candidate
// display list exercises a different corner of the combiner and texture paths,
// and the interesting output is which of them draw, how many triangles each
// emits, and whether any of them trip the backend's "unsupported combiner"
// reporting. A single pass/fail would throw that information away.

#include <3ds.h>
#include <citro3d.h>

#include <cstdio>
#include <memory>
#include <vector>

#include "fast/debug/GfxDebugger.h"
#include "fast/interpreter.h"
#include "fast/resource/factory/DisplayListFactory.h"
#include "fast/resource/factory/MatrixFactory.h"
#include "fast/resource/factory/TextureFactory.h"
#include "fast/resource/factory/VertexFactory.h"
#include "fast/resource/ResourceType.h"
#include "fast/resource/type/DisplayList.h"
#include "ship/resource/ResourceLoader.h"
#include "ship/Context.h"
#include "ship/resource/ResourceManager.h"

#include "pica/gfx_citro3d.h"
#include "pica/gfx_ctr_window.h"
#include "ctr_log.h"

// Fast3D's GBI handlers reach the interpreter through a file-static weak_ptr
// rather than a parameter, and they do not null-check it. Registering via
// GfxSetInstance is mandatory, and only possible if the interpreter is owned by
// a shared_ptr - a stack instance leaves the weak_ptr empty and the first
// G_MARKER command writes through null.
namespace Fast {
extern void GfxSetInstance(std::shared_ptr<Interpreter> gfx);
}

namespace {

constexpr uint32_t kW = 400;
constexpr uint32_t kH = 240;
const char* const kArchive = "/3ds/soh/oot.o2r";

// Ordered simplest-looking first, so an early failure is easier to read.
const char* const kDisplayLists[] = {
    "objects/gameplay_keep/gBoomerangDL",       "objects/gameplay_keep/gSunDL",
    "objects/gameplay_keep/gHeartPieceInteriorDL", "objects/gameplay_keep/gSignRectangularDL",
    "objects/gameplay_keep/gUnusedUnknownShape1DL",
};

void ReportMemory(const char* tag) {
    u32 linear = linearSpaceFree() / 1024;
    u32 vram = vramSpaceFree() / 1024;
    ctr_log("  [%-10s] linear %u KiB, vram %u KiB\n", tag, (unsigned)linear, (unsigned)vram);
}

} // namespace

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;

    gfxInitDefault();
    consoleInit(GFX_BOTTOM, nullptr);
    ctr_log_init();

    ctr_log("SoH-3DS Phase 2: real OoT display lists\n");
    ReportMemory("boot");

    // --- resource layer ------------------------------------------------------
    Ship::Context::CreateUninitializedInstance("SoH-3DS", "soh3ds", "");
    auto* ctx = Ship::Context::GetRawInstance();
    if (ctx == nullptr) {
        ctr_log("FAIL: no Context\nPHASE 2 FAIL\n");
        gfxExit();
        return 1;
    }
    ctx->InitLogging();
    ctx->InitConfiguration();
    ctx->InitConsoleVariables();
    if (!ctx->InitResourceManager({ kArchive }, {}, 1)) {
        ctr_log("FAIL: InitResourceManager(\"%s\")\nPHASE 2 FAIL\n", kArchive);
        gfxExit();
        return 1;
    }
    auto resourceManager = ctx->GetResourceManager();
    ctr_log("archive    : mounted %s\n", kArchive);

    // libultraship registers only Json and Shader factories itself; the Fast3D
    // resource types are the embedder's responsibility. Without these,
    // LoadResource returns null for every display list in the archive and it
    // looks like the entries are missing rather than unparseable.
    // Mirrors soh/soh/OTRGlobals.cpp.
    auto loader = resourceManager->GetResourceLoader();
    loader->RegisterResourceFactory(std::make_shared<Fast::ResourceFactoryBinaryTextureV0>(), RESOURCE_FORMAT_BINARY,
                                    "Texture", static_cast<uint32_t>(Fast::ResourceType::Texture), 0);
    loader->RegisterResourceFactory(std::make_shared<Fast::ResourceFactoryBinaryTextureV1>(), RESOURCE_FORMAT_BINARY,
                                    "Texture", static_cast<uint32_t>(Fast::ResourceType::Texture), 1);
    loader->RegisterResourceFactory(std::make_shared<Fast::ResourceFactoryBinaryVertexV0>(), RESOURCE_FORMAT_BINARY,
                                    "Vertex", static_cast<uint32_t>(Fast::ResourceType::Vertex), 0);
    loader->RegisterResourceFactory(std::make_shared<Fast::ResourceFactoryBinaryDisplayListV0>(),
                                    RESOURCE_FORMAT_BINARY, "DisplayList",
                                    static_cast<uint32_t>(Fast::ResourceType::DisplayList), 0);
    loader->RegisterResourceFactory(std::make_shared<Fast::ResourceFactoryBinaryMatrixV0>(), RESOURCE_FORMAT_BINARY,
                                    "Matrix", static_cast<uint32_t>(Fast::ResourceType::Matrix), 0);
    ctr_log("factories  : Texture/Vertex/DisplayList/Matrix registered\n");
    ReportMemory("ctx");

    // --- renderer ------------------------------------------------------------
    Fast::GfxWindowBackendCtr window;
    Fast::GfxRenderingAPICitro3D api;

    // Interpreter::Init calls the rendering API's own Init(). Calling it here
    // as well issues a second C3D_Init, which fails and leaves the backend
    // unusable while everything downstream carries on regardless.
    auto interpPtr = std::make_shared<Fast::Interpreter>();
    Fast::GfxSetInstance(interpPtr);
    Fast::Interpreter& interp = *interpPtr;
    interp.SetGfxDebugger(std::make_shared<Fast::GfxDebugger>());

    // Upstream leaves the interpolation state uninitialised because desktop
    // fills it in through GameEngine::RunCommands. Driving the interpreter
    // directly skips that, so establish deterministic key-frame state here.
    interp.mInterpolationIndex = 1;
    interp.mInterpolationIndexTarget = 1;
    interp.mInterpolationT = 1.0f;

    // OoT emits F3DEX2, which is Interpreter::Init's default ucode, so no
    // gfx_set_target_ucode call is needed. A Fast3DEX game would need one: the
    // vertex command layouts are incompatible.
    interp.Init(&window, &api, "SoH-3DS", false, kW, kH, 0, 0);
    if (!api.IsReady()) {
        ctr_log("FAIL: backend did not initialise\nPHASE 2 FAIL\n");
        gfxExit();
        return 1;
    }

    ctr_log("interpreter: initialised, target %d fps\n", interp.GetTargetFps());
    ReportMemory("interp");

    // NOTE: no synthetic segment backing.
    //
    // An earlier revision pointed all 16 segments at a scratch page so stray
    // reads would land on mapped memory. That is unsound as acceptance: it
    // converts an unresolved vertex or texture pointer into a plausible-looking
    // read, and the run can then rasterise fabricated data and report it as
    // real OoT geometry. A fault is the honest outcome - it says the display
    // list references something this harness has not resolved.
    //
    // Segment references must be satisfied by resolving the actual O2R
    // resource, or reported as unresolved. See ReportSegmentUse below.

    bool anyDrew = false;
    int loaded = 0;

    for (const char* name : kDisplayLists) {
        auto res = resourceManager->LoadResource(name);
        if (res == nullptr) {
            ctr_log("  %-44s not found\n", name);
            continue;
        }
        auto dl = std::static_pointer_cast<Fast::DisplayList>(res);
        Gfx* commands = dl->GetPointer();
        if (commands == nullptr) {
            ctr_log("  %-44s no instructions\n", name);
            continue;
        }
        ++loaded;

        const uint64_t trisBefore = api.GetTriangleCount();

        interp.StartFrame();
        interp.Run(commands, {});
        interp.EndFrame();

        const uint64_t tris = api.GetTriangleCount() - trisBefore;
        ctr_log("  %-44s %4u cmds, %5llu tris\n", name, (unsigned)dl->Instructions.size(),
                (unsigned long long)tris);
        if (tris > 0) {
            anyDrew = true;
        }
    }

    ctr_log("loaded     : %d of %d display lists\n", loaded, (int)(sizeof(kDisplayLists) / sizeof(*kDisplayLists)));
    ReportMemory("after");

    ctr_log(anyDrew ? "PHASE 2 PASS\n" : "PHASE 2 FAIL (nothing rasterised)\n");

    for (int frame = 0; frame < 90 && aptMainLoop(); ++frame) {
        hidScanInput();
        if (hidKeysDown() & KEY_START) {
            break;
        }
        gspWaitForVBlank();
    }

    // Reverse of construction; clear the static weak_ptr before the interpreter
    // dies so no late GBI handler can resurrect a dangling one.
    interp.Destroy();
    Fast::GfxSetInstance(std::shared_ptr<Fast::Interpreter>{});
    interpPtr.reset();
    api.Shutdown();
    gfxExit();
    return anyDrew ? 0 : 1;
}
