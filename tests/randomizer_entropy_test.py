#!/usr/bin/env python3
"""Check real ShipUtils RNG initialization with the 3DS library fallback."""
from pathlib import Path
import os, subprocess, tempfile
root = Path(__file__).resolve().parents[1]
s = (root/'third_party/shipwright/soh/soh/ShipUtils.cpp').read_text()
a = s.index('static bool default_init = false;')
b = s.index('// Returns a random floating point', a)
cpp = r'''
#include <array>
#include <bit>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <string>
#include <climits>
// devkitARM libstdc++ is built without _GLIBCXX_USE_DEV_RANDOM.
// Model its repeatable first result when a random_device is constructed.
namespace std { struct random_device { unsigned operator()() { return 3499211612U; } }; }
namespace ShipUtils {
 void RandInit(uint64_t, uint64_t* = nullptr);
 uint32_t next32(uint64_t* = nullptr);
 uint32_t Random(uint32_t, uint32_t, uint64_t* = nullptr);
}
uint64_t uptime=1000, walltime=1788640000000ULL;
int tickReads=0, wallReads=0;
extern "C" uint64_t svcGetSystemTick(void) {++tickReads;return uptime;}
extern "C" uint64_t osGetTime(void) {++wallReads;return walltime;}
''' + s[a:b] + r'''
std::string FreshBoot() {
 default_init=false;default_state=0;
 std::string seed;
 for(int i=0;i<10;++i)seed+=char('0'+ShipUtils::Random(0,10));
 return seed;
}
int main() {
 const auto first=FreshBoot();
 // Include the case where emulator uptime repeats across fresh launches.
 walltime+=1000;const auto second=FreshBoot();
 uptime+=268123480;const auto third=FreshBoot();
 assert(first!=second && second!=third);
 assert(tickReads==3 && wallReads==3); // Read clocks only once per boot.
 // Explicit deterministic states must remain reproducible across clocks.
 uint64_t left=0,right=0;ShipUtils::RandInit(1234,&left);ShipUtils::RandInit(1234,&right);
 for(int i=0;i<100;++i) {
  ++uptime;++walltime;
  assert(ShipUtils::Random(0,1000,&left)==ShipUtils::Random(0,1000,&right));
 }
 assert(tickReads==3 && wallReads==3);
 std::printf("3DS fresh boot seeds differ; explicit seeds reproduce; no per-draw clock reads\n");
}
'''
with tempfile.TemporaryDirectory(prefix='soh-rando-entropy-') as d:
 p=Path(d);(p/'test.cpp').write_text(cpp)
 subprocess.run([os.getenv('CXX','g++'),'-std=c++20','-O1','-D__3DS__',str(p/'test.cpp'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
