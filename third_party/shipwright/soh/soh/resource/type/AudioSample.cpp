#include "AudioSample.h"
#include <atomic>
#include <stdexcept>

namespace SOH {
namespace {
#if defined(__3DS__)
// Shared by in-flight decodes and cached samples; released with the owning resource.
std::atomic<size_t> decodedBytes{0};
constexpr size_t MaxDecodedBytes = 16 * 1024 * 1024;
#endif
}

void AudioSample::AllocateSampleData(size_t bytes, bool custom) {
    if (bytes == 0 || bytes > MaxSampleBytes || sampleData) {
        throw std::runtime_error("Invalid or oversized audio sample");
    }
#if defined(__3DS__)
    if (custom) {
        size_t used = decodedBytes.load();
        do {
            if (bytes > MaxDecodedBytes - used) {
                throw std::runtime_error("Custom audio memory budget exhausted");
            }
        } while (!decodedBytes.compare_exchange_weak(used, used + bytes));
        decodedBudgetBytes = bytes;
    }
#endif
    try {
        sampleData = std::make_unique<uint8_t[]>(bytes);
    } catch (...) {
#if defined(__3DS__)
        decodedBytes.fetch_sub(decodedBudgetBytes);
        decodedBudgetBytes = 0;
#endif
        throw;
    }
    sample.sampleAddr = sampleData.get();
}

void AudioSample::AllocateBookData(size_t entries) {
    if (entries > 128 || bookData) {
        throw std::runtime_error("Invalid ADPCM book size");
    }
    bookData = std::make_unique<int16_t[]>(entries);
    book.book = bookData.get();
    sample.book = &book;
}

void AudioSample::MarkUnavailable() noexcept {
    sampleData.reset();
    sample.sampleAddr = nullptr;
    sample.size = 0;
    sample.fileSize = 0;
    // CODEC_S16_INMEMORY is the engine's silence path and does not dereference PCM/book data.
    sample.codec = 2;
    loop = {};
    loop.end = 1;
    sample.loop = &loop;
    tuning = -1.0f;
#if defined(__3DS__)
    decodedBytes.fetch_sub(decodedBudgetBytes);
    decodedBudgetBytes = 0;
#endif
}

AudioSample::~AudioSample() {
    sampleData.reset();
#if defined(__3DS__)
    decodedBytes.fetch_sub(decodedBudgetBytes);
#endif
}
Sample* AudioSample::GetPointer() {
    return &sample;
}

size_t AudioSample::GetPointerSize() {
    return sizeof(Sample);
}
} // namespace SOH