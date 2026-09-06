#include <3ds.h>
#include <cassert>
#include <string>
#include <vector>
static aptHookFn callback;
static std::vector<std::string> events;
void aptHook(aptHookCookie*, aptHookFn fn, void*) { assert(!callback); callback = fn; }
void aptUnhook(aptHookCookie*) { callback = nullptr; }
extern "C" void Soh3dsAudioSleep(bool sleeping) { events.push_back(sleeping ? "audio-stop" : "audio-resume"); }
extern "C" void Soh3dsGraphicsSleep() { events.push_back("gpu-drain"); }
extern "C" void Soh3dsGraphicsWake() { events.push_back("timing-reset"); }
extern "C" void Soh3dsLifecycleMark(const char* stage) { events.emplace_back(stage); }
extern "C" void Soh3dsSleepInit();
extern "C" void Soh3dsSleepShutdown();
int main() {
    Soh3dsSleepInit(); Soh3dsSleepInit();
    callback(APTHOOK_ONSLEEP, nullptr);
    assert((events == std::vector<std::string>{"sleep-enter", "audio-stop", "gpu-drain", "sleep-ready"}));
    events.clear(); callback(APTHOOK_ONSLEEP, nullptr); assert(events.empty());
    callback(APTHOOK_ONWAKEUP, nullptr);
    assert((events == std::vector<std::string>{"wake-enter", "timing-reset", "audio-resume", "wake-ready"}));
    events.clear(); callback(APTHOOK_ONWAKEUP, nullptr); assert(events.empty());
    callback(APTHOOK_ONSUSPEND, nullptr); callback(APTHOOK_ONRESTORE, nullptr);
    assert(events.empty()); // HOME is already handled by Citro3D.
    callback(APTHOOK_ONSLEEP, nullptr); events.clear();
    Soh3dsSleepShutdown();
    assert((events == std::vector<std::string>{"audio-resume"}));
    assert(!callback); Soh3dsSleepShutdown();
    Soh3dsSleepInit(); events.clear(); callback(APTHOOK_ONEXIT, nullptr);
    assert(events.empty()); // APT exit can run after SD devoptab teardown.
    Soh3dsSleepShutdown();
}
