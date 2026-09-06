// Fast3D window backend for the 3DS.
//
// On a handheld there is no window system: the screens are fixed at 400x240 and
// 320x240, there is no cursor, no resizing, no fullscreen toggle, and no
// keyboard. Most of GfxWindowBackend's surface is therefore genuinely
// inapplicable rather than unimplemented, and answers with the one value that
// is true on this hardware.
//
// The parts that do carry weight:
//   GetDimensions   - the interpreter sizes its viewport and framebuffers from
//                     this, so it must report the top screen.
//   GetTime         - drives Fast3D's frame pacing.
//   IsRunning       - aptMainLoop(), so a HOME-menu exit actually stops the game.
//   SwapBuffers*    - the frame boundary; the rendering backend owns the actual
//                     presentation via C3D_FrameBegin/End.

#pragma once

#include <cstdint>

#include "fast/backends/gfx_window_manager_api.h"

namespace Fast {

class GfxWindowBackendCtr final : public GfxWindowBackend {
  public:
    GfxWindowBackendCtr() = default;
    ~GfxWindowBackendCtr() override = default;

    void Init(const char* gameName, const char* apiName, bool startFullScreen, uint32_t width, uint32_t height,
              int32_t posX, int32_t posY) override;
    void Close() override;
    void Destroy() override;

    void SetKeyboardCallbacks(bool (*onKeyDown)(int scancode), bool (*onKeyUp)(int scancode),
                              void (*onAllKeysUp)()) override;
    void SetMouseCallbacks(bool (*onMouseButtonDown)(int btn), bool (*onMouseButtonUp)(int btn)) override;
    void SetFullscreenChangedCallback(void (*onFullscreenChanged)(bool isNowFullscreen)) override;

    void SetFullscreen(bool fullscreen) override;
    bool IsFullscreen() override;

    void GetActiveWindowRefreshRate(uint32_t* refreshRate) override;

    void SetCursorVisibility(bool visible) override;
    void SetMousePos(int32_t posX, int32_t posY) override;
    void GetMousePos(int32_t* x, int32_t* y) override;
    void GetMouseDelta(int32_t* x, int32_t* y) override;
    void GetMouseWheel(float* x, float* y) override;
    bool GetMouseState(uint32_t btn) override;
    void SetMouseCapture(bool capture) override;
    bool IsMouseCaptured() override;

    void GetDimensions(uint32_t* width, uint32_t* height, int32_t* posX, int32_t* posY) override;
    void SetDimensions(uint32_t width, uint32_t height, int32_t posX, int32_t posY) override;
    Ship::WindowRect GetPrimaryMonitorRect() override;

    void HandleEvents() override;
    bool IsFrameReady() override;
    void SwapBuffersBegin() override;
    void SwapBuffersEnd() override;

    double GetTime() override;
    int GetTargetFps() override;
    void SetTargetFps(int fps) override;
    void SetMaxFrameLatency(int latency) override;

    const char* GetKeyName(int scancode) override;
    bool CanDisableVsync() override;
    bool IsRunning() override;

  private:
    int mTargetFps = 20; // N64 OoT runs at 20 Hz; see SetTargetFps
    bool mRunning = true;
};

} // namespace Fast
