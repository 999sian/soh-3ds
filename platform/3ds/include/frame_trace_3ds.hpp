#pragma once

#include <array>
#include <cstdint>
#include <cstdio>

// v1 is little-endian on 3DS: a 16-byte magic then twelve uint64_t fields:
// frame, submission ticks, render ticks, FrameBegin wait ticks, pacing/swap
// wait ticks, loop ticks, cumulative uploads, cumulative drops, game ticks,
// target FPS, top vblank counter, previous trace-write ticks.
// Submission times are NOT physical LCD presentation times.
class Soh3dsFrameTrace {
  public:
    using Sample = std::array<uint64_t, 12>;
    explicit Soh3dsFrameTrace(FILE* file) : file_(file) {
        static constexpr char magic[16] = "SOH3DS-FRAME-v1";
        if (file_ && std::fwrite(magic, sizeof(magic), 1, file_) != 1) file_ = nullptr;
    }
    bool Enabled() const { return file_ != nullptr; }
    void Record(const Sample& sample) {
        if (!file_) return;
        samples_[used_++] = sample;
        if (used_ == samples_.size()) {
            // One bounded write per 60 submissions. Flush explicitly so ftpd
            // can retrieve complete batches after the game's immediate exit.
            if (std::fwrite(samples_.data(), sizeof(Sample), used_, file_) != used_ ||
                std::fflush(file_) != 0) file_ = nullptr;
            used_ = 0;
        }
    }
  private:
    FILE* file_;
    std::array<Sample, 60> samples_{};
    size_t used_ = 0;
};
