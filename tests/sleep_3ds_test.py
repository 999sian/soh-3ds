#!/usr/bin/env python3
"""Host checks for actual sleep coordinator, audio gate and graphics adapters."""
from pathlib import Path
import subprocess
import tempfile
root = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='soh-sleep-') as temporary:
    out = Path(temporary)
    def run(name, sources, flags=()):
        executable = out / name
        subprocess.run(['c++', '-std=c++17', '-pthread', *flags, *map(str, sources), '-o', str(executable)], check=True)
        subprocess.run([str(executable)], check=True, timeout=10)
    run('lifecycle', [root/'tests/sleep_lifecycle_test.cpp', root/'src/compat3ds/sleep_3ds.cpp'], ['-I'+str(root/'tests/sleep_stubs')])
    source = (root/'third_party/libultraship/src/ship/audio/NdspAudioPlayer.cpp').read_text()
    gate = source[source.index('static bool sSleepLockHeld'):source.index('int32_t NdspAudioPlayer::Buffered()')]
    (out/'audio.cpp').write_text('''
#include <mutex>
#include <future>
#include <cassert>
#include <chrono>
std::mutex sBufferLock;
bool sInitialized = false;
void LightLock_Lock(std::mutex* lock) { lock->lock(); }
void LightLock_Unlock(std::mutex* lock) { lock->unlock(); }
''' + gate + '''
int main() {
    Soh3dsAudioSleep(true); Soh3dsAudioSleep(false); // pre-audio initialization
    assert(sBufferLock.try_lock()); sBufferLock.unlock();
    sInitialized = true;
    Soh3dsAudioSleep(true); Soh3dsAudioSleep(true);
    std::promise<void> started;
    auto producer = std::async(std::launch::async, [&] {
        started.set_value();
        std::lock_guard<std::mutex> lock(sBufferLock);
        return true;
    });
    started.get_future().wait();
    assert(producer.wait_for(std::chrono::milliseconds(30)) == std::future_status::timeout);
    Soh3dsAudioSleep(false);
    assert(producer.wait_for(std::chrono::seconds(1)) == std::future_status::ready);
    assert(producer.get());
    Soh3dsAudioSleep(false);
    assert(sBufferLock.try_lock()); sBufferLock.unlock();
}
''')
    run('audio', [out/'audio.cpp'])
    source = (root/'platform/3ds/source/gfx_citro3d.cpp').read_text()
    adapters = source[source.index('extern "C" void C3Di_RenderQueueWaitDone();'):source.index('void GfxRenderingAPICitro3D::EndFrame()')]
    (out/'graphics.cpp').write_text('''
#include <cstdint>
#include <cassert>
bool* sActiveFrame = nullptr;
uint32_t sPaceVblank = 99, sPacePeriod = 3, sSwapReadyVblank = 0;
int drains = 0;
extern "C" void C3Di_RenderQueueWaitDone() { ++drains; }
uint32_t C3D_FrameCounter(int screen) { return 100 + screen; }
''' + adapters + '''
int main() {
    Soh3dsGraphicsSleep(); assert(drains == 0);
    bool frame = true; sActiveFrame = &frame;
    Soh3dsGraphicsSleep(); assert(drains == 0); // queued open-frame commands retained
    frame = false; Soh3dsGraphicsSleep(); assert(drains == 1);
    Soh3dsGraphicsWake();
    assert(sPaceVblank == 0 && sPacePeriod == 0);
    assert(sSwapReadyVblank == 101);
}
''')
    run('graphics', [out/'graphics.cpp'])
print('Sleep lifecycle, audio blocking and open-frame preservation checks passed.')
