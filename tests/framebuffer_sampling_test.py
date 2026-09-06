#!/usr/bin/env python3
"""Exercise framebuffer allocation metadata and production UV packing on the host.

The fixture models PICA's texture/rasterizer Y inversion at the GPU boundary.
Asymmetric pixels must round-trip through render-to-texture and GX-copy storage,
including a slot reused for both without reallocation. No game-model offset is
involved; the 400x240 pause portrait has a 256x512 rotated backing.
"""
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
source = (ROOT / 'platform/3ds/source/gfx_citro3d.cpp').read_text()


def block(signature, text=source):
    start = text.index(signature)
    opening = text.index('{', start)
    depth, end = 1, opening + 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[start:end]


slot = block('    struct FramebufferSlot {') + ';'
ensure = block('bool GfxRenderingAPICitro3D::EnsureFramebufferStorage(')
packing_start = source.index('    std::array<float, 2> textureScaleU =')
packing_end = source.index('    Soh3dsProfileScope stateProfile', packing_start)
packing = source[packing_start:packing_end]
# Exercise the entire real dispatch/fallback and all 36 output bytes. This
# includes the original rotated UV implementation after helper extraction.
pack_helpers = source[source.index('#ifndef SOH3DS_EXPERIMENT_COMMON_PACK'):
                      source.index('constexpr uint32_t kDisplayTransferFlags')]
program = block('struct ShaderProgram {', (ROOT / 'platform/3ds/include/gfx_citro3d.h').read_text()) + ';'
packed_vertex = block('struct PackedVertex {') + ';'

cpp = r'''
#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <vector>
#include "fast/backends/gfx_profile_3ds.h"
struct DepthSnapshot3DS {};
struct C3D_Tex { uint16_t width=0,height=0; };
struct C3D_RenderTarget {};
constexpr int GPU_RGBA8=0,GPU_LINEAR=1,GPU_CLAMP_TO_EDGE=2,GPU_TEXFACE_2D=0,GPU_RB_DEPTH24_STENCIL8=1;
#define C3D_DEPTHTYPE(x) (x)
bool C3D_TexInitVRAM(C3D_Tex* tex,uint16_t width,uint16_t height,int) {*tex={width,height};return true;}
void C3D_TexSetFilter(C3D_Tex*,int,int) {}
void C3D_TexSetWrap(C3D_Tex*,int,int) {}
C3D_RenderTarget* C3D_RenderTargetCreateFromTex(C3D_Tex*,int,int,int) {static C3D_RenderTarget target;return &target;}
size_t vramSpaceFree() {return 1024*1024;}
uint16_t NextPowerOfTwo(uint16_t value) {uint16_t n=8;while(n<value)n*=2;return n;}
''' + program + packed_vertex + block('uint8_t FloatColorToByte(') + '\n' + pack_helpers + r'''
struct TextureSlot {bool initialized=false;uint16_t sourceWidth=0,sourceHeight=0;C3D_Tex texture;};
''' + slot + r'''
struct State {
 std::vector<std::unique_ptr<FramebufferSlot>> framebuffers;
 std::array<int,2> selectedFramebuffers={1,2};
 std::array<uint32_t,2> selectedTextures={};
 std::vector<TextureSlot> textures=std::vector<TextureSlot>(1);
 std::vector<PackedVertex> vertexStorage=std::vector<PackedVertex>(1);
 PackedVertex* packedVertices=vertexStorage.data();
 bool frameActive=false;
 std::vector<int> pendingFramebufferReleases;
};
struct GfxRenderingAPICitro3D {
 State state;State* mImpl=&state;
 bool EnsureFramebufferStorage(int,uint16_t,uint16_t,bool,bool);
 void ReleaseFramebufferStorage(int id) {auto& slot=*state.framebuffers[id];slot.initialized=false;slot.target=nullptr;}
 void Pack(const float* drawVertices,bool singleInput=false) {
  ShaderProgram p;p.usedTextures[0]=p.usedTextures[1]=true;p.textureOffsets[0]=4;p.textureOffsets[1]=6;
  p.numInputs=singleInput?1:0;p.inputOffsets[0]=8;p.strideFloats=singleInput?11:8;
  ShaderProgram* program=&p;const size_t vertexCount=1,firstVertex=0;const float coverScale=1;
  const int varyingInput=singleInput?0:-1;
''' + packing + r'''
 }
};
''' + ensure + r'''
// PICA rasterizer window Y is inverted about the entire backing height;
// a GX copy from the unpadded top LCD starts at the destination's first row.
// These memory fixtures intentionally do not consult allocation metadata.
std::vector<uint32_t> pixels(bool rendered) {
 std::vector<uint32_t> image(256*512,0);
 for(int y=0;y<240;++y) for(int x=0;x<400;++x)
  image[(rendered ? 112+x : x)*256+(239-y)]=1+y*400+x;
 return image;
}
uint32_t sample(const std::vector<uint32_t>& data,const float* uv) {
 int x=static_cast<int>(std::floor(uv[0]*256));
 int y=static_cast<int>(std::floor((1-uv[1])*512));
 if(x<0 || y<0 || x>=256 || y>=512)return 0;
 return data[y*256+x];
}
int main() {
 GfxRenderingAPICitro3D renderer;
 for(int i=0;i<3;++i)renderer.state.framebuffers.push_back(std::make_unique<FramebufferSlot>());
 const int coordinates[][2]={{0,0},{399,239},{100,60},{300,180},{200,120}};
 // Reuse exact same allocation across copy -> draw -> copy, on both units.
 for(bool rendered : {false,true,false}) {
  auto image=pixels(rendered);
  for(int id=1;id<=2;++id) {
   assert(renderer.EnsureFramebufferStorage(id,400,240,true,rendered));
   assert(renderer.state.framebuffers[id]->texture.width==256);
   assert(renderer.state.framebuffers[id]->texture.height==512);
  }
  for(const auto& pos : coordinates) {
   float u=(pos[0]+0.5f)/400,v=(pos[1]+0.5f)/240;
   const float vertex[]={0,0,0,1,u,v,u,v};renderer.Pack(vertex);
   auto& packed=renderer.state.packedVertices[0];
   for(const float* uv : {packed.texcoord0,packed.texcoord1}) {
    uint32_t expected=1+pos[1]*400+pos[0];uint32_t got=sample(image,uv);
    if(got!=expected) {
     std::fprintf(stderr,"%s portrait (%d,%d): expected pixel %u, got %u; UV=(%g,%g)\n",
                  rendered?"rendered":"copied",pos[0],pos[1],expected,got,uv[0],uv[1]);return 1;
    }
   }
  }
 }
 // A POT viewport has no leading padding: draw and copy must agree.
 for(bool rendered : {false,true}) {
  assert(renderer.EnsureFramebufferStorage(1,512,256,true,rendered));
  const float vertex[]={0,0,0,1,0.25f,0.75f,0.25f,0.75f};renderer.Pack(vertex);
  auto& p=renderer.state.packedVertices[0];
  assert(p.texcoord0[0]==0.25f && p.texcoord0[1]==0.75f);
 }
 // The unrotated postprocess copy path keeps its existing scaling.
 assert(renderer.EnsureFramebufferStorage(1,400,240,false,false));
 const float vertex[]={0,0,0,1,0.25f,0.75f,0.25f,0.75f};renderer.Pack(vertex);
 auto& p=renderer.state.packedVertices[0];
 assert(p.texcoord0[0]==100.0f/512 && p.texcoord0[1]==180.0f/256);
 // The same actual scale derivation also reaches an admitted one-input layout.
 assert(renderer.EnsureFramebufferStorage(2,320,180,false,false));
 const float colored[]={2,-3,0,1,0.25f,0.75f,0.5f,0.25f,-0.5f,0.5f,1.5f};
 renderer.Pack(colored,true);
 assert(p.texcoord0[0]==100.0f/512 && p.texcoord0[1]==180.0f/256);
 assert(p.texcoord1[0]==160.0f/512 && p.texcoord1[1]==45.0f/256);
 assert(p.position[0]==2 && p.position[1]==-3 && p.position[2]==0 && p.position[3]==1);
 assert(p.color[0]==0 && p.color[1]==128 && p.color[2]==255 && p.color[3]==255);
 std::printf("framebuffer sampling: asymmetric portrait pixels, both units, copy/draw reuse, POT and common packing pass (switch=%d)\n",
             SOH3DS_EXPERIMENT_COMMON_PACK);
}
'''
with tempfile.TemporaryDirectory(prefix='soh-framebuffer-sampling-') as temporary:
    temporary = Path(temporary)
    path = temporary / 'test.cpp'
    path.write_text(cpp)
    for enabled in (0, 1):
        executable = temporary / ('test-' + str(enabled))
        subprocess.run([os.environ.get('CXX', 'g++'), '-std=c++17', '-O2', '-Wall', '-Wextra',
                        '-fno-fast-math', '-ffp-contract=off', '-DSOH3DS_EXPERIMENT_COMMON_PACK=' + str(enabled),
                        '-I' + str(ROOT / 'third_party/libultraship/include'), str(path), '-o', str(executable)], check=True)
        subprocess.run([str(executable)], check=True)
