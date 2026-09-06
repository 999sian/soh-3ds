#!/usr/bin/env python3
"""Execute real glow projection/checks and PICA depth snapshot on known scenes.

The matrix fixture uses near=10, far=12800 with +Z camera forward. Packed D24
values are independently calculated literals. GPU pixel fixtures model either
a broad occluder or one asymmetric occluder at the rendered glow location.
"""
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
CODE = ROOT / 'third_party/shipwright/soh/src/code'


def function(source, signature):
    start = source.index(signature)
    opening = source.index('{', start)
    depth, end = 1, opening + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]


lights = (CODE / 'z_lights.c').read_text()
actor = function((CODE / 'z_actor.c').read_text(), 'void Actor_ProjectPos(')
multiply = function((CODE / 'z_skin_matrix.c').read_text(), 'void SkinMatrix_Vec3fMtxFMultXYZW(')
prepare = function(lights, 'void Lights_GlowCheckPrepare(')
check = function(lights, 'void Lights_GlowCheck(')
interpreter = (ROOT / 'third_party/libultraship/src/fast/interpreter.cpp').read_text()
adjust = function(interpreter, 'void Interpreter::AdjustPixelDepthCoordinates(')
macros = interpreter[interpreter.index('#define HALF_SCREEN_WIDTH('):interpreter.index('#define TEXTURE_CACHE_MAX_SIZE')]

cpp = r'''
#include <array>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <map>
#include <utility>
#include <vector>
#include "depth_snapshot_3ds.h"
using f32=float;using s32=int32_t;using u32=uint32_t;
constexpr int SCREEN_WIDTH=320,SCREEN_HEIGHT=240,LIGHT_POINT_GLOW=2;
struct Vec3f {f32 x,y,z;};
struct MtxF {f32 xx=1,xy=0,xz=0,xw=0,yx=0,yy=1,yz=0,yw=0,zx=0,zy=0,zz=1,zw=0,wx=0,wy=0,wz=0,ww=1;};
struct LightPoint {int16_t x=0,y=0,z=0;bool drawGlow=false;};
struct LightInfo {int type=LIGHT_POINT_GLOW;struct {LightPoint point;}params;};
struct LightNode {LightInfo* info;LightNode* next=nullptr;};
struct PlayState {MtxF viewProjectionMtxF;struct {LightNode* listHead;}lightCtx;};
uint32_t shrink=0;
uint32_t ShrinkWindow_GetCurrentVal() {return shrink;}
f32 OTRGetAspectRatio() {return 400.f/240.f;}
struct Area {float x=0,y=0,width=400,height=240;};
struct Fb {float orig_width=320,orig_height=240,applied_width=400,applied_height=240;};
struct Interpreter {
 bool mFbActive=false,mRendersToFb=false;int mMsaaLevel=1;
 Area mNativeDimensions={0,0,320,240},mCurDimensions,mGameWindowViewport,mGfxCurrentWindowDimensions;
 std::map<int,Fb> fbs;std::map<int,Fb>::iterator mActiveFrameBuffer=fbs.end();
 void AdjustPixelDepthCoordinates(float&,float&);
};
''' + macros + adjust + r'''
Interpreter bridge;DepthSnapshot3DS snapshot;
std::vector<std::pair<float,float>> prepared,queried;
void OTRGetPixelDepthPrepare(float x,float y) {bridge.AdjustPixelDepthCoordinates(x,y);prepared.emplace_back(x,y);}
uint16_t OTRGetPixelDepth(float x,float y) {bridge.AdjustPixelDepthCoordinates(x,y);queried.emplace_back(x,y);return snapshot.Sample(x,y);}
''' + multiply + actor + prepare + check + r'''
size_t pixelOffset(int x,int y) {
 // Native rotated LCD: PICA memory coordinate is (windowY,windowX).
 size_t morton=0;for(int bit=0;bit<3;++bit)morton|=((y>>bit)&1)<<(2*bit)|((x>>bit)&1)<<(2*bit+1);
 return ((x/8)*30+y/8)*64+morton;
}
int failures=0;
void expect(bool passed,const char* name) {if(!passed) {std::fprintf(stderr,"FAIL: %s\n",name);++failures;}}
int main() {
 PlayState play;LightInfo light;LightNode node{&light};play.lightCtx.listHead=&node;
 auto& p=light.params.point;auto& matrix=play.viewProjectionMtxF;
 matrix.zz=12810.f/12790.f;matrix.zw=-256000.f/12790.f;matrix.wz=1;matrix.ww=0;
 std::vector<uint32_t> data(240*400);
 const auto capture=[&] {assert(snapshot.Capture(data.data(),240,400,400,240,true));prepared.clear();queried.clear();};
 // Light 5000 units away, foreground wall 3000 units away. In the old N64
 // scale, projected32664 < sampled32680 incorrectly declared it visible.
 p.z=5000;data.assign(data.size(),0x0000ADE7);capture();
 Lights_GlowCheckPrepare(&play);Lights_GlowCheck(&play);
 expect(!p.drawGlow,"distant wall occludes glow 2000 units behind it");
 expect(queried.size()==1 && prepared==queried,"prepare/query use same point");
 // A wall beyond the light, and clear sky, must preserve visible glow.
 data.assign(data.size(),0x0000409D);capture();Lights_GlowCheck(&play);
 expect(p.drawGlow,"light in front of distant wall remains visible");
 data.assign(data.size(),0);capture();Lights_GlowCheck(&play);
 expect(p.drawGlow,"uncovered distant glow remains visible");
 // Near geometry still blocks a nearby light; zero is PICA's far clear.
 p.z=100;data.assign(data.size(),0x00330E25);capture();Lights_GlowCheck(&play);
 expect(!p.drawGlow,"near foreground wall blocks nearby glow");
 // Two off-center projections. Their rendered top-screen centers are
 // (280,150) and (120,90), not the uncorrected (300,150)/(100,90).
 for(int sign : {-1,1}) {
  p.x=sign*2500;p.y=sign*1250;p.z=5000;
  data.assign(data.size(),0);
  int screenX=200+sign*80,screenY=120+sign*30;
  for(int dx=-1;dx<=1;++dx)for(int dy=-1;dy<=1;++dy)
   data[pixelOffset(screenX+dx,screenY+dy)]=0x00FFFFFF;
  capture();Lights_GlowCheckPrepare(&play);Lights_GlowCheck(&play);
  expect(!p.drawGlow,"asymmetric foreground object occludes rendered glow");
  expect(prepared.size()==1 && queried.size()==1 && prepared==queried,"asymmetric prepare/query agree");
  expect(!queried.empty() && std::fabs(queried[0].first-screenX)<.001f && std::fabs(queried[0].second-screenY)<.001f,
         "depth probe matches actual widescreen raster position");
 }
 // Existing letterbox and behind-camera rejection must still suppress glow.
 p={0,5000,5000};shrink=30;capture();Lights_GlowCheckPrepare(&play);Lights_GlowCheck(&play);
 expect(!p.drawGlow && prepared.empty() && queried.empty(),"letterbox excludes glow");
 p={0,0,-100};shrink=0;capture();Lights_GlowCheckPrepare(&play);Lights_GlowCheck(&play);
 expect(!p.drawGlow && prepared.empty() && queried.empty(),"behind-camera light excluded");
 if(failures)return 1;
 std::puts("glow depth: distant/near occlusion, visible controls, asymmetric probes and clipping pass");
}
'''
with tempfile.TemporaryDirectory(prefix='soh-glow-depth-') as temporary:
    temporary = Path(temporary)
    source = temporary / 'test.cpp'
    source.write_text(cpp)
    binary = temporary / 'test'
    flags = ['-fsanitize=address,undefined', '-fno-omit-frame-pointer', '-no-pie'] if os.getenv('GLOW_SANITIZE') else []
    subprocess.run([os.getenv('CXX', 'g++'), '-std=c++17', '-D__3DS__', '-O1', *flags,
                    '-I'+str(ROOT / 'platform/3ds/include'), str(source), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
