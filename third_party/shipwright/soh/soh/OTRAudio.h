#pragma once
#include <thread>
#include <condition_variable>
#include <atomic>

// SoH-3DS: this struct was `static`, giving every including translation unit
// its own PRIVATE copy - savestates.cpp locked a phantom mutex (not the audio
// thread's) and then rewrote the audio heap under the live core-2 synthesis
// thread. One shared definition lives in OTRGlobals.cpp.
struct OTRAudioSync {
    std::thread thread;
    std::condition_variable cv_to_thread;
    std::mutex mutex;
    std::atomic_bool running;
    std::atomic_bool processing;
};
extern OTRAudioSync audio;
