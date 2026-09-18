#!/usr/bin/env python3
"""Wake graphics without taking the synthesis lock; shutdown cannot miss wake."""
from pathlib import Path
import subprocess,tempfile
ROOT=Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory() as d:
 p=Path(d);(p/'test.cpp').write_text(r'''
#include "soh/OTRAudio.h"
#include <cassert>
#include <future>
#include <chrono>
using namespace std::chrono_literals;
int main(){
 OTRAudioSync audio{};audio.running=true;
 std::unique_lock<std::mutex> synthesis(audio.mutex);
 auto notify=std::async(std::launch::async,[&]{audio.NotifyProcessing();});
 // A graphics wake must complete even while synthesis/savestate owns mutex.
 assert(notify.wait_for(1s)==std::future_status::ready);notify.get();
 bool primed=false;assert(audio.WaitForWork(primed));assert(primed);assert(!audio.processing);
 synthesis.unlock();
 for(int i=0;i<100;i++){
  audio.running=true;audio.processing=false;bool first=false;
  auto waiter=std::async(std::launch::async,[&]{return audio.WaitForWork(first);});
  audio.Stop();assert(waiter.wait_for(1s)==std::future_status::ready);assert(!waiter.get());
 }
}
''')
 subprocess.run(['g++','-std=c++20','-pthread','-I'+str(ROOT/'third_party/shipwright/soh'),str(p/'test.cpp'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True,timeout=10)
print('PASS: graphics wake independent of synthesis; 100 shutdown/wait races')
