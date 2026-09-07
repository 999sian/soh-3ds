#!/usr/bin/env python3
"""Exercise real primitive setter: PICA has only one varying colour input."""
from pathlib import Path
import subprocess,tempfile
root=Path(__file__).resolve().parents[1]
s=(root/'third_party/libultraship/src/fast/interpreter.cpp').read_text()
start=s.index('void Interpreter::GfxDpSetPrimColor(');end=s.index('\n}',start)+2
code=r'''
#include <cstdint>
#include <cassert>
struct Color {uint8_t r,g,b,a;};
struct Rdp {Color prim_color;uint8_t prim_lod_fraction;};
struct Interpreter {
 Rdp state{{255,255,255,255},0}, *mRdp=&state;
 int flushes=0;Color submitted{};
 void Flush(){++flushes;submitted=mRdp->prim_color;}
 void GfxDpSetPrimColor(uint8_t,uint8_t,uint8_t,uint8_t,uint8_t,uint8_t);
};
'''+s[start:end]+r'''
int main(){
 Interpreter i;
 i.GfxDpSetPrimColor(0,0,255,255,255,255);
 assert(i.flushes==0);
 // Mirror face is white; following body changes to red without changing shader.
 i.GfxDpSetPrimColor(0,0,215,0,0,255);
 assert(i.flushes==1 && i.submitted.r==255 && i.submitted.g==255);
 assert(i.state.prim_color.r==215 && i.state.prim_color.g==0);
 i.GfxDpSetPrimColor(0,0,215,0,0,255);assert(i.flushes==1);
 i.GfxDpSetPrimColor(0,42,215,0,0,255);assert(i.flushes==2);
 i.GfxDpSetPrimColor(0,42,215,0,0,128);assert(i.flushes==3);
}
'''
with tempfile.TemporaryDirectory() as t:
 p=Path(t);(p/'test.cpp').write_text(code)
 subprocess.run(['c++','-std=c++17','-D__3DS__',str(p/'test.cpp'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
print('PICA primitive batches: mirror/body colour separation, unchanged colour, LOD and alpha pass')
