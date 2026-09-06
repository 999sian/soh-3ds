#pragma once

#include "ship/audio/AudioPlayer.h"

namespace Ship {

/**
 * @brief Direct NDSP audio output for the Nintendo 3DS.
 *
 * SoH-3DS: modeled on mario-kart-64-3ds's audio backend. SDL2's 3DS audio
 * driver also sits on NDSP, but adds its own mixer thread, ring buffer and an
 * extra copy, and SDL_GetQueuedAudioSize cannot see how much of the playing
 * wave NDSP has already consumed - so pacing ran on stale numbers. This player
 * queues the game's stereo PCM16 batches straight into a linear-memory wave
 * pool and reports buffered depth from NDSP's own playback position.
 *
 * All NDSP state lives in the translation unit, keeping <3ds.h> (whose u32/s32
 * typedefs collide with libultra's) out of this header.
 */
class NdspAudioPlayer final : public AudioPlayer {
  public:
    using AudioPlayer::AudioPlayer;
    ~NdspAudioPlayer();

    /** @brief Frames still unplayed: queued waves plus the remainder of the playing wave. */
    int32_t Buffered() override;

  protected:
    /** @brief ndspInit (requires sdmc:/3ds/dspfirm.cdc), wave pool allocation, channel setup. */
    bool DoInit() override;
    /** @brief Stops the channel and releases the wave pool. */
    void DoClose() override;
    /** @brief Copies one stereo PCM16 batch into a free wave and queues it. */
    void DoPlay(const uint8_t* buf, size_t len) override;
};

} // namespace Ship
