// SoH-3DS: direct NDSP output, modeled on mario-kart-64-3ds
// (platform/3ds/source/audio_ndsp_3ds.cpp in the port tree). See the header
// for why this replaces SDL's 3DS audio driver.
#ifdef __3DS__

#include "ship/audio/NdspAudioPlayer.h"

#include <3ds.h>

#include <algorithm>
#include <array>
#include <cstring>

#include <spdlog/spdlog.h>

namespace {

constexpr int kChannel = 0;
// Eight waves: SoH's producer emits up to 1680-frame batches while up to
// ~4096 frames sit queued, and the pool must also hold the wave NDSP is
// draining. Six covered MK64's smaller batches; eight gives SoH's the same
// safety margin.
constexpr size_t kBufferCount = 8;
constexpr size_t kFramesPerBuffer = 2048;
constexpr size_t kChannels = 2;

struct AudioBuffer {
    ndspWaveBuf wave = {};
    int16_t* samples = nullptr;
};

std::array<AudioBuffer, kBufferCount> sBuffers;
LightLock sBufferLock;
bool sInitialized = false;
uint32_t sDroppedBatches = 0;
// SoH-3DS telemetry: high-water occupancy since the last pacing report. These
// establish real hardware queue use before any pool resize is considered.
uint32_t sPeakBufferedFrames = 0;
size_t sPeakLiveWaves = 0;

class BufferLockGuard {
  public:
    BufferLockGuard() {
        LightLock_Lock(&sBufferLock);
    }
    ~BufferLockGuard() {
        LightLock_Unlock(&sBufferLock);
    }
    BufferLockGuard(const BufferLockGuard&) = delete;
    BufferLockGuard& operator=(const BufferLockGuard&) = delete;
};

bool IsReusable(const ndspWaveBuf& wave) {
    return wave.status == NDSP_WBUF_FREE || wave.status == NDSP_WBUF_DONE;
}

} // namespace

namespace Ship {

NdspAudioPlayer::~NdspAudioPlayer() {
    SPDLOG_TRACE("destruct NDSP audio player");
    DoClose();
}

bool NdspAudioPlayer::DoInit() {
    if (sInitialized) {
        return true;
    }
    if (R_FAILED(ndspInit())) {
        // The DSP component is a per-console firmware blob; without it NDSP
        // cannot start and the Null player takes over. Name the fix.
        SPDLOG_ERROR("ndspInit failed - is sdmc:/3ds/dspfirm.cdc present? (dump it with the DSP1 homebrew)");
        return false;
    }
    LightLock_Init(&sBufferLock);
    sInitialized = true;

    for (auto& buffer : sBuffers) {
        buffer.samples = static_cast<int16_t*>(linearAlloc(kFramesPerBuffer * kChannels * sizeof(int16_t)));
        if (buffer.samples == nullptr) {
            SPDLOG_ERROR("NDSP wave pool allocation failed");
            DoClose();
            return false;
        }
        std::memset(buffer.samples, 0, kFramesPerBuffer * kChannels * sizeof(int16_t));
        std::memset(&buffer.wave, 0, sizeof(buffer.wave));
    }

    ndspSetOutputMode(NDSP_OUTPUT_STEREO);
    ndspChnReset(kChannel);
    ndspChnWaveBufClear(kChannel);
    ndspChnSetInterp(kChannel, NDSP_INTERP_LINEAR);
    ndspChnSetRate(kChannel, static_cast<float>(GetSampleRate()));
    ndspChnSetFormat(kChannel, NDSP_FORMAT_STEREO_PCM16);
    float mix[12] = {};
    mix[0] = 1.0f;
    mix[1] = 1.0f;
    ndspChnSetMix(kChannel, mix);
    ndspChnSetPaused(kChannel, false);
    SPDLOG_INFO("NDSP audio initialized: {} Hz stereo, {} x {}-frame waves", GetSampleRate(), kBufferCount,
                kFramesPerBuffer);
    return true;
}

void NdspAudioPlayer::DoClose() {
    // The whole teardown holds sBufferLock: DoPlay (core-2 synthesis thread)
    // checks sInitialized unlocked, then calls NDSP APIs and memcpys into the
    // wave pool under this lock - so pause/clear/ndspExit, the sInitialized
    // flip, and the frees must all be serialized behind it, or an
    // abnormal-teardown DoClose can exit NDSP / free samples mid-batch.
    BufferLockGuard lock;
    if (sInitialized) {
        ndspChnSetPaused(kChannel, true);
        ndspChnWaveBufClear(kChannel);
        // Stop libctru's NDSP worker and unregister its APT/DSP hooks before
        // releasing any wave memory.
        ndspExit();
        sInitialized = false;
    }
    for (auto& buffer : sBuffers) {
        if (buffer.samples != nullptr) {
            linearFree(buffer.samples);
            buffer.samples = nullptr;
        }
        std::memset(&buffer.wave, 0, sizeof(buffer.wave));
    }
}

// SoH-3DS: called from the FATAL/terminate path (tests/soh_3ds_main.cpp)
// before process teardown unmaps the heap. ndspExit joins libctru's NDSP
// worker thread, so it can no longer run on an unmapped stack (F1 class).
// Deliberately no frees: the process is dying, and leaving memory alone
// closes every teardown UAF window. Defined inside namespace Ship, so the
// statics resolve unqualified; extern "C" gives it unmangled linkage.
extern "C" void Soh3dsAudioQuiesce(void) {
    BufferLockGuard lock;
    if (sInitialized) {
        ndspChnSetPaused(kChannel, true);
        ndspChnWaveBufClear(kChannel);
        ndspExit();
        sInitialized = false;
    }
}

// The APT main-thread sleep hook holds the submission lock across sleep.
// A producer already inside DoPlay finishes before sleep is acknowledged;
// further Buffered/DoPlay calls block until DSP wake-up has completed.
// Do not stop/recreate NDSP here: libctru owns the DSP sleep lifecycle.
static bool sSleepLockHeld = false;
extern "C" void Soh3dsAudioSleep(bool sleeping) {
    if (sleeping && !sSleepLockHeld) {
        // Before audio initialization there is no producer or initialized lock.
        if (!sInitialized) return;
        LightLock_Lock(&sBufferLock);
        sSleepLockHeld = true;
    } else if (!sleeping && sSleepLockHeld) {
        sSleepLockHeld = false;
        LightLock_Unlock(&sBufferLock);
    }
}

int32_t NdspAudioPlayer::Buffered() {
    if (!sInitialized) {
        return 0;
    }

    BufferLockGuard lock;

    // A PLAYING wave has already been consumed up to the channel's current
    // sample position. Counting its full size would make the producer believe
    // it had almost one extra batch buffered and delay refills precisely when
    // a heavy frame needs the margin most. (Same accounting as MK64-3DS.)
    const u16 playingSequence = ndspChnGetWaveBufSeq(kChannel);
    const u32 playingPosition = ndspChnGetSamplePos(kChannel);
    uint32_t frames = 0;
    size_t liveWaves = 0;
    for (const auto& buffer : sBuffers) {
        if (buffer.wave.status == NDSP_WBUF_QUEUED) {
            frames += buffer.wave.nsamples;
            ++liveWaves;
        } else if (buffer.wave.status == NDSP_WBUF_PLAYING) {
            ++liveWaves;
            if (buffer.wave.sequence_id == playingSequence && playingPosition < buffer.wave.nsamples) {
                frames += buffer.wave.nsamples - playingPosition;
            } else {
                // Status and sequence are updated asynchronously by NDSP. If
                // they crossed between the two snapshots, conservatively
                // retain the wave instead of reporting a false underrun.
                frames += buffer.wave.nsamples;
            }
        }
    }
    // High-water marks are read and reset in DoPlay's pacing report under the
    // same lock, so ordinary Buffered() polling only ratchets them upward.
    sPeakBufferedFrames = std::max(sPeakBufferedFrames, frames);
    sPeakLiveWaves = std::max(sPeakLiveWaves, liveWaves);
    return static_cast<int32_t>(frames);
}

void NdspAudioPlayer::DoPlay(const uint8_t* buf, size_t len) {
    if (!sInitialized || buf == nullptr || len == 0) {
        return;
    }

    // SoH-3DS: temporary diagnostics - a refill arriving after the channel
    // fully drained means an audible gap. Gap-riddled output sounds both
    // choppy and quiet (silence drags the perceived level down), which is
    // indistinguishable by ear from latency.
    {
        static uint32_t sCalls = 0, sGaps = 0;
        static int32_t sMinBuffered = INT32_MAX;
        const int32_t buffered = Buffered();
        if (buffered == 0 && sCalls > 0) {
            ++sGaps;
        }
        if (buffered < sMinBuffered) {
            sMinBuffered = buffered;
        }
        if (++sCalls % 512 == 0) { // roughly every 25 s of audio
            uint32_t peakBuffered;
            size_t peakLiveWaves;
            {
                BufferLockGuard lock;
                peakBuffered = sPeakBufferedFrames;
                peakLiveWaves = sPeakLiveWaves;
                sPeakBufferedFrames = 0;
                sPeakLiveWaves = 0;
            }
            SPDLOG_INFO("NDSP pacing: {} refills, {} gaps, min buffered {} frames, peak buffered {} frames, "
                        "peak live waves {} of {}",
                        sCalls, sGaps, sMinBuffered, peakBuffered, peakLiveWaves, kBufferCount);
            sMinBuffered = INT32_MAX;
        }
    }

    size_t framesLeft = len / (kChannels * sizeof(int16_t));
    const int16_t* source = reinterpret_cast<const int16_t*>(buf);

    // SoH's batches (<= 1680 frames) fit one wave; the loop only matters if a
    // caller ever submits more than kFramesPerBuffer at once.
    while (framesLeft > 0) {
        const size_t frameCount = std::min(framesLeft, kFramesPerBuffer);

        BufferLockGuard lock;
        auto buffer = std::find_if(sBuffers.begin(), sBuffers.end(),
                                   [](const AudioBuffer& candidate) { return IsReusable(candidate.wave); });
        if (buffer == sBuffers.end()) {
            // Pool full: the producer guard normally prevents this. Dropping
            // is preferable to blocking the synthesis thread.
            if (++sDroppedBatches <= 4 || sDroppedBatches % 128 == 0) {
                SPDLOG_WARN("NDSP wave pool full, dropped batch ({} total)", sDroppedBatches);
            }
            return;
        }
        if (!sInitialized || buffer->samples == nullptr) {
            // Torn down between the unlocked entry check and this lock.
            return;
        }

        const size_t byteCount = frameCount * kChannels * sizeof(int16_t);
        std::memcpy(buffer->samples, source, byteCount);
        // svcFlushProcessDataCache provides the coherency NDSP needs at a
        // fraction of DSP_FlushDataCache's cost (MK64-3DS measurement).
        svcFlushProcessDataCache(CUR_PROCESS_HANDLE, reinterpret_cast<u32>(buffer->samples),
                                 static_cast<u32>(byteCount));
        std::memset(&buffer->wave, 0, sizeof(buffer->wave));
        buffer->wave.data_vaddr = buffer->samples;
        buffer->wave.nsamples = static_cast<u32>(frameCount);
        buffer->wave.looping = false;
        ndspChnWaveBufAdd(kChannel, &buffer->wave);

        source += frameCount * kChannels;
        framesLeft -= frameCount;
    }
}

} // namespace Ship

#endif // __3DS__
