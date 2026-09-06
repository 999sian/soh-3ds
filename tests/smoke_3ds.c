// Phase 0 link smoke test.
//
// Purpose: prove that libctru + citro3d + the from-source SDL2 all link into one
// 3DS executable, and that the dual-screen render targets and the SDL subsystems
// SoH's ControlDeck depends on actually initialise. This is the dependency-stack
// half of SPEC.md gate A2 — it does not build libultraship, only the substrate
// libultraship needs.
//
// Not a graphics test. Nothing is drawn. It prints what it found and exits, so it
// can be run under an emulator or on hardware and read off the top screen.

#include <3ds.h>
#include <citro3d.h>
#include <SDL2/SDL.h>
#include <stdio.h>

#include "ctr_log.h"

#define CLEAR_COLOUR 0x101018FF

// SDL2main defines `main` -> `SDL_main` with an (int, char**) signature, and on
// 3DS it is also what initialises romfs. Match the signature it expects.
int main(int argc, char** argv) {
    (void)argc;
    (void)argv;
    gfxInitDefault();
    ctr_log_init();

    LOG("SoH-3DS Phase 0 link smoke test\n\n");

    // --- model / clock -------------------------------------------------------
    bool isNew3ds = false;
    APT_CheckNew3DS(&isNew3ds);
    LOG("model        : %s\n", isNew3ds ? "New 3DS" : "Old 3DS");
    // osSetSpeedupEnable goes through ptm:sysm ConfigureNew3DSCPU, which Azahar
    // leaves unimplemented -- the call never returns and the title hangs. Build
    // with -DSOH3DS_EMULATOR_SAFE=ON to skip it (and any other service call the
    // emulator stubs) so the same source runs under an emulator and on hardware.
#ifdef SOH3DS_EMULATOR_SAFE
    LOG("speedup      : SKIPPED (emulator-safe build)\n");
#else
    if (isNew3ds) {
        osSetSpeedupEnable(true);
        LOG("speedup      : enabled (804MHz + L2)\n");
    } else {
        LOG("speedup      : N/A -- SPEC.md targets New 3DS only\n");
    }
#endif
    LOG("app memtype  : %u\n", (unsigned)osGetApplicationMemType());
    LOG("APPLICATION  : %lu KiB\n", (unsigned long)(osGetMemRegionSize(MEMREGION_APPLICATION) >> 10));
    LOG("linear free  : %lu KiB\n", (unsigned long)(linearSpaceFree() >> 10));
    LOG("vram free    : %lu KiB\n", (unsigned long)(vramSpaceFree() >> 10));

    // --- citro3d + both screens ---------------------------------------------
    if (!C3D_Init(C3D_DEFAULT_CMDBUF_SIZE)) {
        LOG("\nC3D_Init     : FAILED\n");
    } else {
        LOG("\nC3D_Init     : ok\n");

        // Targets are created rotated: (height, width). SPEC.md section 2.
        C3D_RenderTarget* top =
            C3D_RenderTargetCreate(240, 400, GPU_RB_RGBA8, GPU_RB_DEPTH24_STENCIL8);
        C3D_RenderTarget* bottom =
            C3D_RenderTargetCreate(240, 320, GPU_RB_RGBA8, GPU_RB_DEPTH16);
        LOG("top target   : %s (240x400)\n", top ? "ok" : "FAILED");
        LOG("bottom target: %s (240x320)\n", bottom ? "ok" : "FAILED");

        if (top && bottom) {
            // GFX_BOTTOM output is the dual-screen seam (SPEC.md section 5.1).
            C3D_RenderTargetSetOutput(top, GFX_TOP, GFX_LEFT, GX_TRANSFER_IN_FORMAT(GX_TRANSFER_FMT_RGBA8) |
                                                                  GX_TRANSFER_OUT_FORMAT(GX_TRANSFER_FMT_RGB8));
            C3D_RenderTargetSetOutput(bottom, GFX_BOTTOM, GFX_LEFT, GX_TRANSFER_IN_FORMAT(GX_TRANSFER_FMT_RGBA8) |
                                                                        GX_TRANSFER_OUT_FORMAT(GX_TRANSFER_FMT_RGB8));
            LOG("dual output  : both screens bound\n");
        }

        // A render-to-texture target in VRAM -- what every Fast3D framebuffer id
        // becomes (SPEC.md section 4.6).
        C3D_Tex fbTex;
        if (C3D_TexInitVRAM(&fbTex, 512, 256, GPU_RGBA8)) {
            C3D_RenderTarget* rtt =
                C3D_RenderTargetCreateFromTex(&fbTex, GPU_TEXFACE_2D, 0, GPU_RB_DEPTH16);
            LOG("rtt (512x256): %s\n", rtt ? "ok" : "FAILED");
            if (rtt) {
                C3D_RenderTargetDelete(rtt);
            }
            C3D_TexDelete(&fbTex);
        } else {
            LOG("rtt          : C3D_TexInitVRAM FAILED\n");
        }
        LOG("vram after   : %lu KiB\n", (unsigned long)(vramSpaceFree() >> 10));
    }

    // --- SDL2: the subsystems LUS's ControlDeck and audio actually use -------
    LOG("\nSDL_Init(JOYSTICK|GAMECONTROLLER|AUDIO|SENSOR|EVENTS|TIMER)\n");
    if (SDL_Init(SDL_INIT_JOYSTICK | SDL_INIT_GAMECONTROLLER | SDL_INIT_AUDIO | SDL_INIT_SENSOR |
                 SDL_INIT_EVENTS | SDL_INIT_TIMER) != 0) {
        LOG("  FAILED: %s\n", SDL_GetError());
    } else {
        SDL_version v;
        SDL_GetVersion(&v);
        LOG("  SDL       : %u.%u.%u\n", v.major, v.minor, v.patch);
        LOG("  video drv : %s\n", SDL_GetCurrentVideoDriver() ? SDL_GetCurrentVideoDriver() : "(none)");
        LOG("  audio drv : %s\n", SDL_GetCurrentAudioDriver() ? SDL_GetCurrentAudioDriver() : "(none)");
        LOG("  joysticks : %d\n", SDL_NumJoysticks());

        // The decisive one for LUS: does the 3DS present as a *GameController*?
        // LUS's mapping layer is built on SDL_GameController, not raw joysticks.
        for (int i = 0; i < SDL_NumJoysticks(); ++i) {
            LOG("  [%d] gamecontroller=%s", i, SDL_IsGameController(i) ? "YES" : "no");
            SDL_GameController* gc = SDL_GameControllerOpen(i);
            if (gc) {
                LOG(" name=\"%s\"", SDL_GameControllerName(gc) ? SDL_GameControllerName(gc) : "?");
                LOG(" rumble=%s", SDL_GameControllerHasRumble(gc) ? "y" : "n");
                LOG(" gyro=%s",
                       SDL_GameControllerHasSensor(gc, SDL_SENSOR_GYRO) ? "y" : "n");
                SDL_GameControllerClose(gc);
            }
            LOG("\n");
        }
        LOG("  sensors   : %d\n", SDL_NumSensors());
    }

    LOG("\nSTART to exit.\n");

    while (aptMainLoop()) {
        hidScanInput();
        if (hidKeysDown() & KEY_START) {
            break;
        }
        gspWaitForVBlank();
        gfxSwapBuffers();
    }

    SDL_Quit();
    C3D_Fini();
    gfxExit();
    return 0;
}
