#include "ship/utils/audio_profile_3ds.h"
#include "ship/utils/audio_diagnostics_3ds.h"
#include <3ds.h>
#include <algorithm>
#include <cstdio>
#include <cstring>

bool gSoh3dsAudioProfileActive = false;
uint32_t gSoh3dsAudioProfileOps[SOH3DS_AUDIO_OP_COUNT] = {};
namespace {
constexpr uint64_t kTicksPerSecond = 268111856;
constexpr unsigned kSamplePeriod = 17; // Move across sequencer/batch phases.
uint32_t sOrdinal = 0, sSamples = 0;
uint64_t sFrames = 0, sBegin = 0, sLast = 0, sTotal = 0, sMax = 0;
uint64_t sStages[SOH3DS_AUDIO_STAGE_COUNT] = {};
bool sEnabled = false, sRuntimeReported = false;

uint64_t Microseconds(uint64_t ticks) { return ticks * 1000000u / kTicksPerSecond; }
void ResetWindow() {
    sSamples = 0;
    sFrames = sTotal = sMax = 0;
    std::memset(sStages, 0, sizeof(sStages));
    std::memset(gSoh3dsAudioProfileOps, 0, sizeof(gSoh3dsAudioProfileOps));
}
}

void Soh3dsAudioProfileBegin(uint32_t frames, bool enabled) {
    gSoh3dsAudioProfileActive = false;
    if (!enabled) {
        if (sEnabled) ResetWindow();
        sEnabled = false;
        sRuntimeReported = false;
        sOrdinal = 0;
        return;
    }
    sEnabled = true;
    if (sOrdinal++ % kSamplePeriod != 0) return;
    gSoh3dsAudioProfileActive = true;
    ++sSamples;
    sFrames += frames;
    sBegin = sLast = svcGetSystemTick();
}

void Soh3dsAudioProfileRecord(unsigned stage) {
    if (!gSoh3dsAudioProfileActive || stage >= SOH3DS_AUDIO_STAGE_COUNT) return;
    const uint64_t now = svcGetSystemTick();
    sStages[stage] += now - sLast;
    sLast = now;
}

void Soh3dsAudioProfileEnd() {
    if (!gSoh3dsAudioProfileActive) return;
    Soh3dsAudioProfileRecord(SOH3DS_AUDIO_TAIL);
    sTotal += sLast - sBegin;
    sMax = std::max(sMax, sLast - sBegin);
    gSoh3dsAudioProfileActive = false;
}

void Soh3dsAudioProfileReport() {
    if (!sEnabled || sSamples == 0 || !Soh3dsLoggingEnabled(SOH3DS_LOG_GENERAL)) return;
    char record[512];
    if (!sRuntimeReported) {
        u32 granted = 0;
        s32 priority = -1;
        const auto grantResult = APT_GetAppCpuTimeLimit(&granted);
        const auto priorityResult = svcGetThreadPriority(&priority, CUR_THREAD_HANDLE);
        std::snprintf(record, sizeof(record),
            "audio runtime: core=%d priority=%ld priorityResult=%ld grantedCpu=%lu grantResult=%ld sampleEvery=%u\n",
            svcGetProcessorID(), (long)priority, (long)priorityResult, (unsigned long)granted,
            (long)grantResult, kSamplePeriod);
        Soh3dsWriteAudioDiagnostic(record);
        sRuntimeReported = true;
    }
    // Stage wall times include preemption/internal waits. Only sampled batches
    // contribute to frames/budget and op counts; no extrapolated CPU claim.
    std::snprintf(record, sizeof(record),
        "audio stages: samples=%lu frames=%llu loadsUs=%llu commandsUs=%llu sequenceUs=%llu mixingUs=%llu tailUs=%llu "
        "synthUs=%llu budgetUs=%llu maxUs=%llu adpcm=%lu resample=%lu env=%lu gain=%lu filter=%lu opus=%lu\n",
        (unsigned long)sSamples, (unsigned long long)sFrames,
        (unsigned long long)Microseconds(sStages[SOH3DS_AUDIO_LOADS]),
        (unsigned long long)Microseconds(sStages[SOH3DS_AUDIO_COMMANDS]),
        (unsigned long long)Microseconds(sStages[SOH3DS_AUDIO_SEQUENCE]),
        (unsigned long long)Microseconds(sStages[SOH3DS_AUDIO_MIXING]),
        (unsigned long long)Microseconds(sStages[SOH3DS_AUDIO_TAIL]),
        (unsigned long long)Microseconds(sTotal), (unsigned long long)(sFrames * 1000000u / 32000u),
        (unsigned long long)Microseconds(sMax),
        (unsigned long)gSoh3dsAudioProfileOps[SOH3DS_AUDIO_ADPCM],
        (unsigned long)gSoh3dsAudioProfileOps[SOH3DS_AUDIO_RESAMPLE],
        (unsigned long)gSoh3dsAudioProfileOps[SOH3DS_AUDIO_ENVELOPE],
        (unsigned long)gSoh3dsAudioProfileOps[SOH3DS_AUDIO_GAIN],
        (unsigned long)gSoh3dsAudioProfileOps[SOH3DS_AUDIO_FILTER],
        (unsigned long)gSoh3dsAudioProfileOps[SOH3DS_AUDIO_OPUS]);
    Soh3dsWriteAudioDiagnostic(record);
    ResetWindow();
}
