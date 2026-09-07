#pragma once

#ifdef __cplusplus
extern "C" {
#endif
enum {
    SOH3DS_LOG_GENERAL = 1,
    SOH3DS_LOG_FRAMES = 2,
    SOH3DS_LOG_PROFILE = 4,
    SOH3DS_LOG_TEXTURES = 8,
};
unsigned Soh3dsLoggingFlags(void) __attribute__((weak));
void Soh3dsApplyLoggingSettings(void) __attribute__((weak));
void Soh3dsConfigureDebugOutput(void) __attribute__((weak));
static inline int Soh3dsLoggingEnabled(unsigned mode) {
    return Soh3dsLoggingFlags != 0 && (Soh3dsLoggingFlags() & mode) == mode;
}
#ifdef __cplusplus
}
#endif
