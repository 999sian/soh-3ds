#!/usr/bin/env python3
"""Run production shader-transition and UV derivation code against hardware failure cases."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / 'third_party/libultraship/src/fast/interpreter.cpp').read_text()
start = source.index('ShaderProgram* Interpreter::LookupOrCreateShaderProgram(')
transition = source[start:source.index('\n}', start) + 2]
start = source.index('    for (int t = 0; t < 2; t++) {', source.index('// Per-vertex UV path'))
uv = source[start:source.index('\n}\n', start)]
cases = {
'shader': r'''
#include <cassert>
#include <cstdint>
struct ShaderProgram { int stride; };
struct Api {
 ShaderProgram old{12}, fresh{13}, *active=&old;
 bool cached=false;
 ShaderProgram* LookupShader(uint64_t,uint64_t){return cached ? &fresh : nullptr;}
 void UnloadShader(ShaderProgram*){}
 ShaderProgram* CreateAndLoadNewShader(uint64_t,uint64_t){return active=&fresh;}
};
struct Interpreter {
 Api api, *mRapi=&api;
 struct {ShaderProgram* mShaderProgram;} mRenderingState{&api.old};
 int pending=3, submittedStride=0, flushes=0;
 void Flush(){if(pending){submittedStride=api.active->stride;pending=0;++flushes;}}
 ShaderProgram* LookupOrCreateShaderProgram(uint64_t,uint64_t);
};
''' + transition + r'''
int main(){
 Interpreter i; auto p=i.LookupOrCreateShaderProgram(1,2);
 assert(i.submittedStride==12 && i.pending==0 && i.flushes==1);
 assert(p==&i.api.fresh && i.api.active==p);
 Interpreter cached; cached.api.cached=true;
 cached.LookupOrCreateShaderProgram(1,2);
 assert(cached.pending==3 && cached.api.active==&cached.api.old);
}
''',
'uv': r'''
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
struct Tile {unsigned shifts=0,shiftt=0;float uls=0,ult=0;};
struct Rdp {Tile texture_tile[2];};
struct Comb {bool usedTextures[2]={true,false};};
struct State {
 bool usedTextures[2]={true,true}, linear_filter=true;
 uint32_t effective_tile[2];
 float uMul[2],vMul[2],uAdd[2],vAdd[2],halfU[2],halfV[2],clampS[2],clampT[2];
};
void derive(State& ts, Comb* comb, uint32_t width, uint32_t height){
 Rdp rdp, *mRdp=&rdp;
 uint32_t effective_tile[2]={0,1};
 // Poison unused dimensions: this slot was never imported by the combiner.
 uint32_t tex_width[2]={width,0},tex_height[2]={height,0};
 uint32_t tex_width2[2]={16,0},tex_height2[2]={16,0};
''' + uv + r'''
}
int main(){
 State ts; Comb comb; derive(ts,&comb,16,16);
 assert(ts.uMul[0]==1.0f/512 && ts.halfU[0]==1.0f/32);
 assert(ts.uMul[1]==0 && ts.vMul[1]==0 && ts.uAdd[1]==0 && ts.vAdd[1]==0);
 assert(ts.halfU[1]==0 && ts.halfV[1]==0);
 // A used but empty tile must also never send NaNs/infinities to PICA.
 derive(ts,&comb,0,0);
 assert(std::isfinite(ts.uMul[0]) && std::isfinite(ts.vMul[0]));
 assert(std::isfinite(ts.uAdd[0]) && std::isfinite(ts.vAdd[0]));
}
'''
}
failed = []
with tempfile.TemporaryDirectory() as temp:
    for name, code in cases.items():
        path = Path(temp) / name
        path.with_suffix('.cpp').write_text(code)
        subprocess.run(['c++', '-std=c++17', str(path.with_suffix('.cpp')), '-o', str(path)], check=True)
        result = subprocess.run([str(path)])
        if result.returncode:
            failed.append(name)
assert not failed, f'Failed GPU regressions: {failed}'
print('GPU shader transition and unused/empty texture UV regressions passed')
