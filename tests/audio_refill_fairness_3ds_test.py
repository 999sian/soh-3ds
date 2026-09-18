#!/usr/bin/env python3
"""Execute the production refill loop under permanent backend starvation."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / 'third_party/shipwright/soh/soh/OTRGlobals.cpp').read_text()
start = source.index('#ifdef SOH3DS_OLD_AUDIO_CORE0\n    extern int32_t svcGetProcessorID',
                     source.index('void OTRAudio_Thread()'))
end = source.index('\n}\n// Scoped to OTRAudio_Thread', start)
loop = source[start:end]
# Use deterministic thread/mutex fakes so an endless high-priority refill loop
# fails in a bounded time and the timed pause must release the synthesis lock.
code = r'''
#include <cassert>
#include <chrono>
#include <cstdint>
#define SAMPLES_MID 544
#define UPDATES_PER_BATCH 1
int batches, pauses, queued, core, queries;
bool expectPause, stopInPause;
int32_t svcGetProcessorID(void) __asm__("svcGetProcessorID");
int32_t svcGetProcessorID(void){++queries;return core;}
struct Mutex {bool locked=false;void lock(){assert(!locked);locked=true;}void unlock(){assert(locked);locked=false;}};
struct Audio {
 bool running=true;Mutex mutex;int waits=0;
 bool WaitForWork(bool& primed){primed=true;return running&&waits++==0;}
} audio;
namespace std {
 using mutex=Mutex;
 template<class M> struct unique_lock {
  M& m;bool owns=true;unique_lock(M& value):m(value){m.lock();}
  void unlock(){m.unlock();owns=false;}
  ~unique_lock(){if(owns)m.unlock();}
 };
 namespace this_thread {
  void sleep_for(chrono::milliseconds duration){
   assert(expectPause&&core==0&&duration.count()==1&&!audio.mutex.locked);
   assert(batches==4*(pauses+1));
   ++pauses;
   if(stopInPause)audio.running=false;
  }
 }
}
int AudioPlayer_Buffered(){return queued;}
int AudioPlayer_GetDesiredBuffered(){return 2048;}
void run(bool full){
 batches=pauses=queries=0;queued=full?2048:0;audio.running=true;audio.waits=0;
 auto produce_next_batch=[&]{
  assert(audio.mutex.locked);++batches;
  if(expectPause&&batches==5)assert(pauses==1);
  if(batches==9)audio.running=false;
 };
''' + loop + r'''
 assert(!audio.mutex.locked);
#ifdef SOH3DS_OLD_AUDIO_CORE0
 assert(queries==1);
#else
 assert(queries==0);
#endif
 if(full)assert(batches==0&&pauses==0);
 else if(expectPause&&stopInPause)assert(batches==4&&pauses==1);
 else assert(batches==9&&pauses==(expectPause?2:0));
}
int main(){
 for(core=0;core<=2;core++){
#ifdef SOH3DS_OLD_AUDIO_CORE0
  expectPause=core==0;
#else
  expectPause=false;
#endif
  stopInPause=false;run(false);run(true);
  stopInPause=true;run(false);
 }
}
'''
with tempfile.TemporaryDirectory() as directory:
    path = Path(directory)
    (path/'test.cpp').write_text(code)
    for label, flags in [('default', []), ('core0', ['-DSOH3DS_OLD_AUDIO_CORE0'])]:
        subprocess.run(['g++', '-std=c++20', *flags, str(path/'test.cpp'), '-o', str(path/label)], check=True)
        subprocess.run([str(path/label)], check=True, timeout=5)
        print(f'PASS {label}: overloaded refill yields without lock only on experimental core0; full queue and stop exit')
