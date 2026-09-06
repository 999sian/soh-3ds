#!/usr/bin/env python3
"""Compile current backend methods with hardware-boundary doubles, then run them.

Tests execute production method bodies, never assertions about source spelling.
Citro3D/GSP cannot run on the host; fake registers and a deterministic clock
model those boundaries. Invoke with sampler, depth, or timing.
"""
import pathlib
import os
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[1]
source = (ROOT / 'platform/3ds/source/gfx_citro3d.cpp').read_text()


def method(name):
    start = source.index('GfxRenderingAPICitro3D::' + name + '(')
    start = source.rfind('\n', 0, start) + 1
    if name == 'GetPixelDepth':
        start = source.rfind('\n', 0, start - 1) + 1
    end = source.index('\n}', start) + 2
    return source[start:end]


def impl_method(name):
    start = source.index('    void ' + name + '(')
    opening = source.index('{', start)
    depth, end = 1, opening + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]


common = r'''
#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <set>
#include <unordered_map>
#include <utility>
#include <vector>
#include "fast/backends/gfx_profile_3ds.h"
using u32 = uint32_t;
enum GPU_TEXTURE_FILTER_PARAM {GPU_NEAREST,GPU_LINEAR};
enum GPU_TEXTURE_WRAP_PARAM {GPU_REPEAT,GPU_MIRRORED_REPEAT,GPU_CLAMP_TO_EDGE};
struct C3D_Tex { int filter=GPU_NEAREST; int wrap=GPU_REPEAT; };
std::array<C3D_Tex*,3> bound{};
std::array<int,3> hardwareFilter{};
std::array<bool,3> dirty{};
void C3D_TexSetFilter(C3D_Tex* t, GPU_TEXTURE_FILTER_PARAM f, GPU_TEXTURE_FILTER_PARAM) {t->filter=f;}
void C3D_TexSetWrap(C3D_Tex* t, GPU_TEXTURE_WRAP_PARAM w, GPU_TEXTURE_WRAP_PARAM) {t->wrap=w;}
void C3D_TexBind(int u, C3D_Tex* t) {assert(u>=0 && u<3);bound[u]=t;dirty[u]=true;}
void draw() {for(int u=0;u<3;++u) if(dirty[u]) {hardwareFilter[u]=bound[u]->filter;dirty[u]=false;}}
struct C3D_RenderTarget { struct {uint32_t width=8,height=8;void* depthBuf=nullptr;int depthFmt=1;} frameBuf;};
constexpr int GPU_RB_DEPTH24_STENCIL8=1;
void GSPGPU_InvalidateDataCache(const void*,size_t) {}
size_t MortonOffset8x8(uint32_t x,uint32_t y) {size_t v=0;for(unsigned i=0;i<3;i++)v|=((x>>i)&1)<<(2*i)|((y>>i)&1)<<(2*i+1);return v;}
constexpr uint32_t kTopLogicalWidth=400,kTopHeight=240;
struct hash_pair_ff {size_t operator()(const std::pair<float,float>& p)const{return std::hash<float>{}(p.first)^std::hash<float>{}(p.second);}};
using Map=std::unordered_map<std::pair<float,float>,uint16_t,hash_pair_ff>;
#if __has_include("depth_snapshot_3ds.h")
#include "depth_snapshot_3ds.h"
#define HAS_SNAPSHOT
#endif
struct Slot { C3D_Tex texture;bool initialized=true;C3D_RenderTarget* target=nullptr;bool rotated=false;uint16_t contentWidth=8,contentHeight=8;
#ifdef HAS_SNAPSHOT
DepthSnapshot3DS depthSnapshot;
#endif
};
struct State {
 std::array<uint32_t,6> selectedTextures={1};std::array<int,6> selectedFramebuffers={};
 std::vector<std::unique_ptr<Slot>> framebuffers;
 std::vector<Slot> textures=std::vector<Slot>(3);
 std::array<C3D_Tex*,3> boundTextures{};
 void BindTexture(int u,C3D_Tex* t) {if(u<0 || u>=3)return;boundTextures[u]=t;C3D_TexBind(u,t);}
 void RebindTexture(C3D_Tex* t) {for(int u=0;u<3;++u)if(boundTextures[u]==t)BindTexture(u,t);}
 int depthProbeFb=0;std::vector<std::pair<float,float>> depthProbeCoords;Map depthProbeResults;
 C3D_RenderTarget *gameTarget=nullptr,*topTarget=nullptr;uint32_t renderWidth=8,renderHeight=8;
 uint64_t sampleDepthQueryCount=0;
#ifdef HAS_SNAPSHOT
 DepthSnapshot3DS depthSnapshot;
#endif
};
struct GfxRenderingAPICitro3D {State state;State* mImpl=&state;void SetSamplerParameters(int,bool,uint32_t,uint32_t);Map GetPixelDepth(int,const std::set<std::pair<float,float>>&);void ResolveDepthProbe();};
'''

# Include the real binding/alias tracking too, rather than duplicate it in a fake.
common = common.replace(
    ' void BindTexture(int u,C3D_Tex* t) {if(u<0 || u>=3)return;boundTextures[u]=t;C3D_TexBind(u,t);}',
    impl_method('BindTexture'))
common = common.replace(
    ' void RebindTexture(C3D_Tex* t) {for(int u=0;u<3;++u)if(boundTextures[u]==t)BindTexture(u,t);}',
    impl_method('RebindTexture'))

sampler = r'''
int main() {
 GfxRenderingAPICitro3D r;auto& s=r.state;
 // The same texture is also bound as a fallback without selectedTextures[1].
 s.BindTexture(0,&s.textures[1].texture);s.BindTexture(1,&s.textures[1].texture);draw();
 r.SetSamplerParameters(0,true,2,2);draw();
 assert(hardwareFilter[0]==GPU_LINEAR && hardwareFilter[1]==GPU_LINEAR);
 r.SetSamplerParameters(0,false,0,0);draw();
 assert(hardwareFilter[0]==GPU_NEAREST && hardwareFilter[1]==GPU_NEAREST);
 s.framebuffers.emplace_back();s.framebuffers.emplace_back(std::make_unique<Slot>());
 s.selectedFramebuffers[0]=1;s.BindTexture(0,&s.framebuffers[1]->texture);draw();
 r.SetSamplerParameters(0,true,0,0);draw();assert(hardwareFilter[0]==GPU_LINEAR);
 r.SetSamplerParameters(-1,true,0,0);r.SetSamplerParameters(6,true,0,0);
 puts("sampler: same-texture filter transitions, alias/fallback units and framebuffer pass");
}
'''

depth = r'''
int main() {
 GfxRenderingAPICitro3D r;C3D_RenderTarget target;std::array<uint32_t,64> pixels;
 pixels.fill(0x00FFFFFF);target.frameBuf.depthBuf=pixels.data();r.state.gameTarget=&target;
 r.GetPixelDepth(0,{{100.f,100.f}});r.ResolveDepthProbe();
 assert(r.GetPixelDepth(0,{{100.f,100.f}}).at({100.f,100.f})==0);
 assert(r.GetPixelDepth(0,{{100.01f,100.f}}).at({100.01f,100.f})==0);
 assert(r.GetPixelDepth(0,{{250.f,100.f}}).at({250.f,100.f})==0);
 // A later batch must not invalidate an earlier batch, nor read GPU writes.
 pixels.fill(0);assert(r.GetPixelDepth(0,{{100.01f,100.f}}).at({100.01f,100.f})==0);
 r.ResolveDepthProbe();assert(r.GetPixelDepth(0,{{100.01f,100.f}}).at({100.01f,100.f})==0xFFFC);
 assert(r.GetPixelDepth(0,{{-100.f,100.f}}).at({-100.f,100.f})==0xFFFC);
 assert(r.GetPixelDepth(99,{{100.f,100.f}}).at({100.f,100.f})==0xFFFC);
 // Independent framebuffer requests must not replace the main snapshot.
 r.state.framebuffers.emplace_back();r.state.framebuffers.emplace_back(std::make_unique<Slot>());
 C3D_RenderTarget other;std::array<uint32_t,64> blocked;blocked.fill(0x00FFFFFF);
 other.frameBuf.depthBuf=blocked.data();r.state.framebuffers[1]->target=&other;
 r.GetPixelDepth(1,{{200.f,120.f}});r.ResolveDepthProbe();
 assert(r.GetPixelDepth(1,{{200.f,120.f}}).at({200.f,120.f})==0);
 assert(r.GetPixelDepth(0,{{200.f,120.f}}).at({200.f,120.f})==0xFFFC);
#ifdef HAS_SNAPSHOT
 DepthSnapshot3DS snap;std::array<uint32_t,64> layout{};
 // Hand-derived Morton offsets, with a nonzero stencil byte to mask out.
 layout[42]=0xCCFFFFFF;snap.Capture(layout.data(),8,8,8,8,false);assert(snap.Sample(0,0)==0);
 layout.fill(0);layout[0]=0x55FFFFFF;snap.Capture(layout.data(),8,8,8,8,true);assert(snap.Sample(0,0)==0);
 layout.fill(0);layout[42]=0xFFFFFFFF;snap.Capture(layout.data(),8,8,4,4,false);assert(snap.Sample(0,0)==0);
 // GL window (100,60), derived from projection/viewport and PICA's backing-row flip.
 // Upright512x256, viewport400x240 -> memory(100,195), Morton word99098.
 std::vector<uint32_t> padded(512*256);padded[99098]=0xFFFFFF;
 snap.Capture(padded.data(),512,256,400,240,false);assert(snap.Sample(100,60)==0);
 // Rotated240x400 -> memory(60,100), Morton word23536.
 padded.assign(240*400,0);padded[23536]=0xFFFFFF;
 snap.Capture(padded.data(),240,400,400,240,true);assert(snap.Sample(100,60)==0);
 // Rotated POT256x512 -> memory(60,212), Morton word53744.
 padded.assign(256*512,0);padded[53744]=0xFFFFFF;
 snap.Capture(padded.data(),256,512,400,240,true);assert(snap.Sample(100,60)==0);
 assert(snap.Sample(NAN,0)==0xFFFC && snap.Sample(INFINITY,0)==0xFFFC);
 assert(!snap.Capture(nullptr,8,8,8,8,false));assert(snap.Sample(0,0)==0xFFFC);
 snap.Reset();assert(!snap.requested && snap.Sample(0,0)==0xFFFC);
#endif
 puts("depth: moving/batched probes, immutable snapshot, clear and bounds pass");
}
'''

if sys.argv[1] == 'sampler':
    code = common + method('SetSamplerParameters') + sampler
elif sys.argv[1] == 'depth':
    code = common + method('GetPixelDepth') + method('ResolveDepthProbe') + depth
elif sys.argv[1] == 'timing':
    # Run the production submission/pacing path, stopping before diagnostic IO.
    body = method('EndFrame').split('    // SoH-3DS DIAGNOSTIC:')[0] + '\n}'
    code = r'''
#include <algorithm>
#include <cstdint>
uint64_t tick=108; uint32_t vblank=1;
uint64_t svcGetSystemTick(){return tick;}
uint32_t C3D_FrameCounter(int){return vblank;}
bool pendingBottom=false;
bool gspIsPresentPending(unsigned screen){return pendingBottom && screen==1 && vblank<3;}
void gspWaitForAnyEvent(){tick+=10;++vblank;}
int Soh3dsTargetFps(){return 30;}
void C3D_FrameEnd(int){tick+=4;}
void C3D_FrameSplit(int){}
void C3D_FrameDrawOn(void*){}
void C3D_RenderTargetClear(void*,int,int,int){}
constexpr int GX_CMDLIST_FLUSH=1,C3D_CLEAR_ALL=1;
uint32_t sPaceVblank=3,sPacePeriod=2,sSwapReadyVblank=2;bool sFrameBehind=false;
constexpr uint32_t kMaxPacingDebtVblanks=6;
struct State {bool frameActive=true,bottomDrawnLastFrame=false,bottomDrawnThisFrame=false,externalLinearBuffersDirty=false;
void* bottomTarget=nullptr;uint64_t busyTickAccumulator=200,waitTickAccumulator=0,frameStartTick=100;
uint64_t sampleFrameSplitCount=0,linearHeapFlushFrameCount=0;};
struct GfxRenderingAPICitro3D {State state;State* mImpl=&state;void EndFrame();
void PresentSceneToTopTarget(){tick+=3;}void FlushPackedVertices(){tick+=2;}};
''' + body + r'''
#include <cassert>
#include <cstdio>
int main(){GfxRenderingAPICitro3D r;r.EndFrame();
assert(tick==127);assert(r.state.busyTickAccumulator==217);assert(r.state.waitTickAccumulator==10);
// An inactive EndFrame must neither add work nor subtract earlier work.
r.state.frameActive=false;r.EndFrame();assert(r.state.busyTickAccumulator==217);
// A later bottom swap adds wait time without inflating CPU work or erasing
// the already accumulated work from earlier frames.
tick=108;vblank=1;sPaceVblank=3;pendingBottom=true;
GfxRenderingAPICitro3D late;late.EndFrame();
assert(tick==137);assert(late.state.busyTickAccumulator==217);assert(late.state.waitTickAccumulator==20);
puts("timing: CPU work preserved; pacing and late bottom-swap waits counted separately");}
'''
else:
    raise SystemExit('choose sampler, depth or timing')
with tempfile.TemporaryDirectory(prefix='soh-renderer-test-') as temp:
    cpp = pathlib.Path(temp) / 'test.cpp'
    cpp.write_text(code)
    exe = pathlib.Path(temp) / 'test'
    flags = ['-fsanitize=address,undefined', '-fno-omit-frame-pointer', '-no-pie'] if os.environ.get('RENDERER_SANITIZE') else []
    subprocess.run(['c++', '-std=c++20', '-O1', '-g', *flags, '-I'+str(ROOT/'platform/3ds/include'), '-I'+str(ROOT/'third_party/libultraship/include'), str(cpp), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
