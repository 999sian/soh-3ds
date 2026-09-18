#!/usr/bin/env python3
"""Compare optimized texture-row counts with integer division at boundaries."""
import os, subprocess, tempfile
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory() as d:
    p=Path(d)
    (p/'test.cpp').write_text(r'''
#include "fast/texture_rows_3ds.h"
#include <cassert>
#include <cstdint>
int main() {
 assert(Fast::TextureRows3DS(1024,0)==1024);
 assert(Fast::TextureRows3DS(1024,32)==32);
 assert(Fast::TextureRows3DS(1024,30)==34);
 const uint32_t sizes[]={0,1,31,32,33,1023,65535,0x80000000u,0xffffffffu};
 for(auto bytes:sizes) {
  for(uint32_t stride=1;stride<4097;++stride) assert(Fast::TextureRows3DS(bytes,stride)==bytes/stride);
  assert(Fast::TextureRows3DS(bytes,0x80000000u)==bytes/0x80000000u);
 }
}
''')
    subprocess.run([os.environ.get('CXX','g++'),'-std=c++20','-O2','-I'+str(ROOT/'third_party/libultraship/include'),str(p/'test.cpp'),'-o',str(p/'test')],check=True)
    subprocess.run([str(p/'test')],check=True)
