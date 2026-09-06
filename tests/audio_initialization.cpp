#include "soh/resource/type/AudioSample.h"
#include <cassert>
#include <cstring>
#include <new>
int main() {
    alignas(SOH::AudioSample) unsigned char storage[sizeof(SOH::AudioSample)];
    memset(storage, 0xa5, sizeof(storage));
    auto* sample = new (storage) SOH::AudioSample(std::make_shared<Ship::ResourceInitData>());
    assert(sample->sample.sampleAddr == nullptr);
    assert(sample->sample.book == nullptr);
    assert(sample->book.book == nullptr);
    assert(sample->loop.end == 0);
    sample->~AudioSample();
}
