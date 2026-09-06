#include "gfx_ctr_window.h"

#include "fast/Fast3dGui.h"
#include "ship/Context.h"
#include "ship/window/Window.h"

#include <3ds.h>

#include <SDL2/SDL.h>

#include <cstdio>

namespace Fast {

namespace {
constexpr uint32_t kTopWidth = 400;
constexpr uint32_t kTopHeight = 240;
} // namespace

void GfxWindowBackendCtr::Init(const char* gameName, const char* apiName, bool startFullScreen, uint32_t width,
                               uint32_t height, int32_t posX, int32_t posY) {
    // Every argument describes a window this platform does not have. The screen
    // is 400x240, always "fullscreen", and cannot move or resize.
    (void)gameName;
    (void)apiName;
    (void)startFullScreen;
    (void)width;
    (void)height;
    (void)posX;
    (void)posY;
    mRunning = true;

    // Bring up ImGui here.
    //
    // Fast3dGui::Init has no caller inside libultraship - the desktop SDL and
    // DXGI window backends call it once they own a window and a GL/D3D context.
    // Those backends are excluded on 3DS, so without this ImGui::CreateContext()
    // never runs: GetIO().Fonts stays null and every AddFont call faults reading
    // offset 0. That produced ~580 unmapped reads inside ImFontAtlas::AddFont.
    //
    // Like the Wii U's Gx2 case there is no handle to hand over, only the screen
    // size, because the renderer owns both screens.
    auto gui = Ship::Context::GetRawInstance()->GetWindow()->GetGui();
    if (gui != nullptr) {
        GuiWindowInitData impl{};
        impl.Backend = WindowBackend::FAST3D_CITRO3D;
        impl.Ctr.Width = kTopWidth;
        impl.Ctr.Height = kTopHeight;
        std::static_pointer_cast<Fast3dGui>(gui)->Init(impl);

        std::fprintf(stderr, "soh-3ds gfx: Fast3dGui::Init done (ImGui context up)\n");
    } else {
        std::fprintf(stderr, "soh-3ds gfx: no Gui on the window; ImGui will not initialise\n");
    }
}

void GfxWindowBackendCtr::Close() {
    mRunning = false;
}

void GfxWindowBackendCtr::Destroy() {
    mRunning = false;
}

// --- input plumbing --------------------------------------------------------
// Buttons and touch arrive through LUS's ControlDeck via SDL2's n3ds backend,
// not through these window-level hooks, so there is nothing to register.

void GfxWindowBackendCtr::SetKeyboardCallbacks(bool (*onKeyDown)(int), bool (*onKeyUp)(int), void (*onAllKeysUp)()) {
    (void)onKeyDown;
    (void)onKeyUp;
    (void)onAllKeysUp;
}

void GfxWindowBackendCtr::SetMouseCallbacks(bool (*onMouseButtonDown)(int), bool (*onMouseButtonUp)(int)) {
    (void)onMouseButtonDown;
    (void)onMouseButtonUp;
}

void GfxWindowBackendCtr::SetFullscreenChangedCallback(void (*onFullscreenChanged)(bool)) {
    (void)onFullscreenChanged;
}

// --- window state ----------------------------------------------------------

void GfxWindowBackendCtr::SetFullscreen(bool fullscreen) {
    (void)fullscreen;
}

bool GfxWindowBackendCtr::IsFullscreen() {
    return true; // the screen is the whole display, always
}

void GfxWindowBackendCtr::GetActiveWindowRefreshRate(uint32_t* refreshRate) {
    if (refreshRate != nullptr) {
        *refreshRate = 60; // both LCDs are fixed 60 Hz
    }
}

// --- pointer ---------------------------------------------------------------
// There is no cursor. Touch is a ControlDeck axis/button source, deliberately
// not surfaced as a mouse: reporting a fake pointer here would make ImGui and
// the interpreter believe a cursor exists and hover-track a stylus that is
// almost always lifted.

void GfxWindowBackendCtr::SetCursorVisibility(bool visible) {
    (void)visible;
}

void GfxWindowBackendCtr::SetMousePos(int32_t posX, int32_t posY) {
    (void)posX;
    (void)posY;
}

void GfxWindowBackendCtr::GetMousePos(int32_t* x, int32_t* y) {
    if (x != nullptr) {
        *x = 0;
    }
    if (y != nullptr) {
        *y = 0;
    }
}

void GfxWindowBackendCtr::GetMouseDelta(int32_t* x, int32_t* y) {
    if (x != nullptr) {
        *x = 0;
    }
    if (y != nullptr) {
        *y = 0;
    }
}

void GfxWindowBackendCtr::GetMouseWheel(float* x, float* y) {
    if (x != nullptr) {
        *x = 0.0f;
    }
    if (y != nullptr) {
        *y = 0.0f;
    }
}

bool GfxWindowBackendCtr::GetMouseState(uint32_t btn) {
    (void)btn;
    return false;
}

void GfxWindowBackendCtr::SetMouseCapture(bool capture) {
    (void)capture;
}

bool GfxWindowBackendCtr::IsMouseCaptured() {
    return false;
}

// --- geometry --------------------------------------------------------------

void GfxWindowBackendCtr::GetDimensions(uint32_t* width, uint32_t* height, int32_t* posX, int32_t* posY) {
    if (width != nullptr) {
        *width = kTopWidth;
    }
    if (height != nullptr) {
        *height = kTopHeight;
    }
    if (posX != nullptr) {
        *posX = 0;
    }
    if (posY != nullptr) {
        *posY = 0;
    }
}

void GfxWindowBackendCtr::SetDimensions(uint32_t width, uint32_t height, int32_t posX, int32_t posY) {
    (void)width;
    (void)height;
    (void)posX;
    (void)posY;
}

Ship::WindowRect GfxWindowBackendCtr::GetPrimaryMonitorRect() {
    return { 0, 0, static_cast<int32_t>(kTopWidth), static_cast<int32_t>(kTopHeight) };
}

// --- frame lifecycle -------------------------------------------------------

void GfxWindowBackendCtr::HandleEvents() {
    // APT drives suspend/resume and the HOME menu; when it says stop, stop.
    if (!aptMainLoop()) {
        mRunning = false;
    }

    // Refresh the pad, then let SDL rebuild joystick state from it.
    //
    // libultraship reads controllers through SDL, and SDL's 3DS joystick driver
    // reads hidKeysDown()/hidKeysUp()/hidCircleRead() - all of which only change
    // after hidScanInput(). SDL calls hidScanInput() from its *video* driver's
    // event pump (src/video/n3ds/SDL_n3dsevents.c), but Context::InitControlDeck
    // only brings up SDL_INIT_GAMECONTROLLER and this backend owns the screens
    // itself, so no SDL video driver exists to pump it. Without this the pad
    // reads as permanently idle and the game takes no input at all.
    //
    // Order matters: hidKeysDown() reports keys that went down on the most recent
    // scan, so the scan has to precede the joystick update that consumes it.
    hidScanInput();
    SDL_JoystickUpdate();

    // One-shot: report what SDL actually found. libultraship's device manager
    // only adopts a device when SDL_IsGameController() is true, which needs a
    // gamepad mapping for the pad's GUID - so "a joystick exists" and "the game
    // can be controlled" are different claims.
    static bool sReported = false;
    if (!sReported) {
        sReported = true;
        const int n = SDL_NumJoysticks();
        std::fprintf(stderr, "soh-3ds input: SDL_NumJoysticks=%d\n", n);
        for (int i = 0; i < n; ++i) {
            const char* name = SDL_JoystickNameForIndex(i);
            std::fprintf(stderr, "soh-3ds input:   [%d] \"%s\" isGameController=%d\n", i,
                         name != nullptr ? name : "(null)", (int)SDL_IsGameController(i));
        }
        if (n == 0) {
            std::fprintf(stderr, "soh-3ds input: no joystick - is SDL's joystick subsystem up?\n");
        }
    }
}

bool GfxWindowBackendCtr::IsFrameReady() {
    return true;
}

void GfxWindowBackendCtr::SwapBuffersBegin() {
    // The rendering backend owns presentation: C3D_FrameEnd performs the
    // display transfer and the swap. Doing it here as well would flip twice and
    // hand back the wrong buffer.
}

void GfxWindowBackendCtr::SwapBuffersEnd() {
}

double GfxWindowBackendCtr::GetTime() {
    // Not libctru's osGetTime(): libultraship defines an osGetTime() of its own
    // for the N64 libultra API, with different semantics and the same C symbol,
    // so referencing libctru's pulls in a colliding definition and the link
    // fails. svcGetSystemTick has no such clash and is a finer-grained clock
    // anyway - a fixed 268 MHz counter, unaffected by the New 3DS speed-up.
    return static_cast<double>(svcGetSystemTick()) / static_cast<double>(SYSCLOCK_ARM11);
}

int GfxWindowBackendCtr::GetTargetFps() {
    return mTargetFps;
}

void GfxWindowBackendCtr::SetTargetFps(int fps) {
    mTargetFps = fps;
}

void GfxWindowBackendCtr::SetMaxFrameLatency(int latency) {
    (void)latency; // no swapchain to queue against
}

const char* GfxWindowBackendCtr::GetKeyName(int scancode) {
    (void)scancode;
    return "";
}

bool GfxWindowBackendCtr::CanDisableVsync() {
    // Presentation is tied to the LCD's own 60 Hz scanout; there is no way to
    // hand a frame over outside it.
    return false;
}

bool GfxWindowBackendCtr::IsRunning() {
    return mRunning;
}

} // namespace Fast

// --- backend factories -----------------------------------------------------
// libultraship's Fast3dWindow owns backend selection but must not include
// citro3d headers, so it reaches these two by declaration only.

namespace Fast {

// The rendering-API factory lives in gfx_citro3d.cpp; this file only sees the
// window backend interface.
GfxWindowBackend* CreateCtrWindowBackend() {
    std::fprintf(stderr, "soh-3ds gfx: CreateCtrWindowBackend\n");
    return new GfxWindowBackendCtr();
}

} // namespace Fast
