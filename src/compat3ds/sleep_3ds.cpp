// SOH's lid-sleep boundary. HOME transitions remain owned by Citro3D.
#include <3ds.h>

extern "C" void Soh3dsAudioSleep(bool sleeping);
extern "C" void Soh3dsGraphicsSleep() __attribute__((weak));
extern "C" void Soh3dsGraphicsWake() __attribute__((weak));
extern "C" void Soh3dsLifecycleMark(const char* stage);

namespace {
aptHookCookie cookie;
bool installed = false;
bool sleeping = false;

// APTHOOK_ONEXIT may be called by aptExit after archiveUnmountAll/fsExit.
// Do not use SD logging there: newlib can retain an fd whose devoptab is gone.
// The earlier normal-exit atexit marker records orderly shutdown instead.
void OnAptEvent(APT_HookType event, void*) {
    if (event == APTHOOK_ONSLEEP && !sleeping) {
        Soh3dsLifecycleMark("sleep-enter");
        Soh3dsAudioSleep(true);
        if (Soh3dsGraphicsSleep) Soh3dsGraphicsSleep();
        sleeping = true;
        Soh3dsLifecycleMark("sleep-ready");
    } else if (event == APTHOOK_ONWAKEUP && sleeping) {
        Soh3dsLifecycleMark("wake-enter");
        if (Soh3dsGraphicsWake) Soh3dsGraphicsWake();
        Soh3dsAudioSleep(false);
        sleeping = false;
        Soh3dsLifecycleMark("wake-ready");

    }
}
}

extern "C" void Soh3dsSleepInit() {
    if (!installed) {
        aptHook(&cookie, OnAptEvent, nullptr);
        installed = true;
    }
}

extern "C" void Soh3dsSleepShutdown() {
    if (!installed) return;
    aptUnhook(&cookie);
    installed = false;
    if (sleeping) {
        Soh3dsAudioSleep(false);
        sleeping = false;
    }
}
