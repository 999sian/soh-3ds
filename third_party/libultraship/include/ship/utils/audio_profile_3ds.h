#pragma once
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif
enum Soh3dsAudioStage {
    SOH3DS_AUDIO_LOADS, SOH3DS_AUDIO_COMMANDS, SOH3DS_AUDIO_SEQUENCE,
    SOH3DS_AUDIO_MIXING, SOH3DS_AUDIO_TAIL, SOH3DS_AUDIO_STAGE_COUNT
};
enum Soh3dsAudioOperation {
    SOH3DS_AUDIO_ADPCM, SOH3DS_AUDIO_RESAMPLE, SOH3DS_AUDIO_ENVELOPE,
    SOH3DS_AUDIO_GAIN, SOH3DS_AUDIO_FILTER, SOH3DS_AUDIO_OPUS, SOH3DS_AUDIO_OP_COUNT
};
#ifdef SOH3DS_AUDIO_PROFILE
// Owned exclusively by the audio producer. No timers in individual mixer ops.
extern bool gSoh3dsAudioProfileActive;
extern uint32_t gSoh3dsAudioProfileOps[SOH3DS_AUDIO_OP_COUNT];
void Soh3dsAudioProfileBegin(uint32_t frames, bool enabled);
void Soh3dsAudioProfileRecord(unsigned stage);
void Soh3dsAudioProfileEnd(void);
void Soh3dsAudioProfileReport(void);
static inline void Soh3dsAudioProfileMark(unsigned stage) {
    if (gSoh3dsAudioProfileActive) Soh3dsAudioProfileRecord(stage);
}
static inline void Soh3dsAudioProfileCount(unsigned op, uint32_t count) {
    if (gSoh3dsAudioProfileActive && op < SOH3DS_AUDIO_OP_COUNT)
        gSoh3dsAudioProfileOps[op] += count;
}
#endif
#ifdef __cplusplus
}
#endif
