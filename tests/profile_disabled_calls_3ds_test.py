#!/usr/bin/env python3
"""Disabled profiling must not enter out-of-line instrumentation hooks."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
code = r'''
#include "fast/backends/gfx_profile_3ds.h"
#include <cassert>
extern "C" { bool gSoh3dsRenderProfileEnabled = false; }
unsigned begins=0, ends=0, packs=0, vertices=0;
extern "C" uint64_t Soh3dsProfileBegin(unsigned) {
    ++begins; return gSoh3dsRenderProfileEnabled ? 123 : 0;
}
extern "C" void Soh3dsProfileEnd(unsigned, uint64_t t) { assert(t==123); ++ends; }
extern "C" void Soh3dsProfilePack(unsigned, uint32_t) { ++packs; }
extern "C" void Soh3dsProfileVertexPath(unsigned, uint32_t) { ++vertices; }
int main() {
    for (int i=0;i<100000;++i) {
        Soh3dsProfileScope scope(Soh3dsProfileSection::Triangle);
        Soh3dsProfilePackBatch(true,3);
        Soh3dsProfileVertexBatch(Soh3dsVertexPath::Arm11, 1);
    }
    assert(begins==0 && ends==0 && packs==0 && vertices==0);
    gSoh3dsRenderProfileEnabled=true;
    { Soh3dsProfileScope scope(Soh3dsProfileSection::Vertex);
      Soh3dsProfilePackBatch(true,3);
        Soh3dsProfileVertexBatch(Soh3dsVertexPath::Arm11, 1); }
    assert(begins==1 && ends==1 && packs==1 && vertices==1);
    { Soh3dsProfileScope scope(Soh3dsProfileSection::Draw);
      gSoh3dsRenderProfileEnabled=false; }
    assert(begins==2 && ends==2); // Finish a started sample even if switched off.
}
'''
missing = r'''
#include "fast/backends/gfx_profile_3ds.h"
int main() {
    Soh3dsProfileScope scope(Soh3dsProfileSection::Triangle);
    Soh3dsProfilePackBatch(true,3);
        Soh3dsProfileVertexBatch(Soh3dsVertexPath::Arm11, 1);
}
'''
with tempfile.TemporaryDirectory() as directory:
    p=Path(directory)
    for name, body in [('hooks',code),('missing',missing)]:
        (p/(name+'.cpp')).write_text(body)
        subprocess.run(['c++','-std=c++17','-O2','-D__3DS__',
                        '-I'+str(root/'third_party/libultraship/include'),
                        str(p/(name+'.cpp')),'-o',str(p/name)],check=True)
        subprocess.run([str(p/name)],check=True)
print('100,000 disabled scopes/pack probes make zero calls; enable/disable and absent hooks pass')
