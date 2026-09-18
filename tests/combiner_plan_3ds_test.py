#!/usr/bin/env python3
"""Keep cached TEV operations equivalent across changing OoT colour inputs."""
import os, subprocess, tempfile
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
source=(ROOT/'platform/3ds/source/gfx_citro3d.cpp').read_text()
enums=source[source.index('enum CombinerSource'):source.index('enum ShaderOption')]
plans=source[source.index('std::array<float, 4> ConstantForSource'):source.index('// The one constant colour a stage reads:')]
code=r'''
#include <array>
#include <cstdint>
#include <cstdio>
#include <cassert>
#include <cmath>
#include "compiled_channel_3ds.h"
enum GPU_COMBINEFUNC {GPU_REPLACE,GPU_MODULATE,GPU_INTERPOLATE,GPU_MULTIPLY_ADD,GPU_ADD,GPU_SUBTRACT};
''' + enums + plans + r'''
int main() {
 std::array<std::array<float,4>,7> constants{};
 const int modulate[4]={SourceTexel0,SourceZero,SourceInput1,SourceZero};
 auto cached=CompileChannelPlan(modulate);assert(cached.valid);
 auto p=ResolveChannelPlan(cached,modulate,0,constants,false);
 assert(p.count==1 && p[0].function==GPU_MODULATE && p[0].source[0]==SourceTexel0 && p[0].source[1]==SourceInput1);
 const int folded[4]={SourceTexel0,1,2,3};
 cached=CompileChannelPlan(folded);assert(!cached.valid);
 constants[0].fill(0.5f);constants[1].fill(0.5f);constants[2].fill(0.75f);
 p=ResolveChannelPlan(cached,folded,-1,constants,false);
 assert(p.count==2 && p[1].function==GPU_ADD && p.folded[0]==0.5f);
 constants[2].fill(0.125f);
 p=ResolveChannelPlan(cached,folded,-1,constants,false);
 assert(p[0].function==GPU_SUBTRACT && p[1].function==GPU_MULTIPLY_ADD);
 const int text[4]={1,2,SourceTexel0,2};
 cached=CompileChannelPlan(text);assert(!cached.valid);
 p=ResolveChannelPlan(cached,text,-1,constants,false);assert(p.count==2 && p[1].oneMinusSecond);
 p=ResolveChannelPlan(cached,text,0,constants,false);assert(p.count==1 && p[0].function==GPU_INTERPOLATE);
 // Differential coverage of source combinations, both channels and varying slots.
 for(int a=0;a<15;a++) for(int b=0;b<15;b++) for(int c=0;c<15;c++) for(int d=0;d<15;d++) {
  const int f[4]={a,b,c,d};auto compiled=CompileChannelPlan(f);
  for(int varying=-1;varying<7;varying++) for(bool alpha:{false,true}) {
   auto expected=BuildChannelPlan(f,varying,constants,alpha);
   auto actual=ResolveChannelPlan(compiled,f,varying,constants,alpha);
   assert(actual.count==expected.count && actual.folded==expected.folded);
   for(size_t i=0;i<actual.size();i++) assert(actual[i].function==expected[i].function && actual[i].source==expected[i].source && actual[i].oneMinusSecond==expected[i].oneMinusSecond);
  }
 }
}
'''
with tempfile.TemporaryDirectory() as d:
    p=Path(d);(p/'test.cpp').write_text(code)
    subprocess.run([os.environ.get('CXX','g++'),'-std=c++20','-O2','-I'+str(ROOT/'platform/3ds/include'),str(p/'test.cpp'),'-o',str(p/'test')],check=True)
    subprocess.run([str(p/'test')],check=True)
