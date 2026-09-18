#pragma once
#include <thread>
#include <condition_variable>
#include <atomic>
#include <chrono>

// SoH-3DS: this struct was `static`, giving every including translation unit
// its own PRIVATE copy - savestates.cpp locked a phantom mutex (not the audio
// thread's) and then rewrote the audio heap under the live core-2 synthesis
// thread. One shared definition lives in OTRGlobals.cpp.
struct OTRAudioSync {
    std::thread thread;
    std::condition_variable cv_to_thread;
    std::mutex mutex;
    std::atomic_bool running{false};
    std::atomic_bool processing{false};
    // Wake-up signalling never waits for synthesis or savestate serialization.
    std::mutex wakeMutex;

    void NotifyProcessing() {
        {
            std::lock_guard<std::mutex> lock(wakeMutex);
            processing = true;
        }
        cv_to_thread.notify_one();
    }
    void Stop() {
        {
            std::lock_guard<std::mutex> lock(wakeMutex);
            running = false;
        }
        cv_to_thread.notify_all();
    }
    bool WaitForWork(bool& primed) {
        std::unique_lock<std::mutex> lock(wakeMutex);
        auto ready = [&] { return processing || !running; };
        if (!primed) {
            cv_to_thread.wait(lock, ready); // engine must initialize first
        } else {
            cv_to_thread.wait_for(lock, std::chrono::milliseconds(5), ready);
        }
        if (!running) return false;
        primed = true;
        processing = false;
        return true;
    }
};
extern OTRAudioSync audio;
