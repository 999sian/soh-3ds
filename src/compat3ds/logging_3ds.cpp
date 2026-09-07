#include "ship/utils/logging_3ds.h"
#include <3ds.h>
#include <sys/iosupport.h>
#include <libultraship/bridge/consolevariablebridge.h>
#include <spdlog/spdlog.h>
#include <atomic>

namespace {
std::atomic<unsigned> sLoggingFlags{0};
}
extern "C" void Soh3dsConfigureCrashLogging(bool enabled) __attribute__((weak));

extern "C" unsigned Soh3dsLoggingFlags(void) {
    return sLoggingFlags.load(std::memory_order_relaxed);
}

extern "C" void Soh3dsConfigureDebugOutput(void) {
    // Preserve a visible setup/boot console; only follow stderr when stdout
    // already points to the debug sink. No file operations on this path.
    const bool followsDebug = devoptab_list[STD_OUT] == devoptab_list[STD_ERR];
    consoleDebugInit(Soh3dsLoggingEnabled(SOH3DS_LOG_GENERAL) ? debugDevice_SVC : debugDevice_NULL);
    if (followsDebug) devoptab_list[STD_OUT] = devoptab_list[STD_ERR];
}

extern "C" void Soh3dsApplyLoggingSettings(void) {
    unsigned flags = 0;
    if (CVarGetInteger("gDeveloperTools.DebugLogging", 0)) {
        flags = SOH3DS_LOG_GENERAL;
        if (CVarGetInteger("gDeveloperTools.FrameLogging", 0)) flags |= SOH3DS_LOG_FRAMES;
        if (CVarGetInteger("gDeveloperTools.ProfileLogging", 0)) flags |= SOH3DS_LOG_PROFILE;
        if (CVarGetInteger("gDeveloperTools.TextureLogging", 0)) flags |= SOH3DS_LOG_TEXTURES;
    }
    const unsigned oldFlags = sLoggingFlags.exchange(flags, std::memory_order_relaxed);
    Soh3dsConfigureDebugOutput();
    spdlog::set_level(flags ? spdlog::level::info : spdlog::level::off);
    if ((oldFlags & SOH3DS_LOG_GENERAL) != (flags & SOH3DS_LOG_GENERAL) && Soh3dsConfigureCrashLogging) {
        Soh3dsConfigureCrashLogging(flags != 0);
    }
}
