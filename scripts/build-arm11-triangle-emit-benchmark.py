#!/usr/bin/env python3
"""Build a correctness-first Old 3DS complete-emitter + depth capture benchmark.

Extracts the immutable baseline wrapper and current complete wrapper, including
cached dispatch, live setup, counters and capacity Flush. The real production
selector runs once before each timed material run, including rejected layouts.
Only renderer backend/profiling endpoints are replaced. Reports
paired hardware ticks; no host or QEMU timings are used as performance evidence.
"""
from pathlib import Path
import hashlib
import json
import os
import re
import subprocess
import sys
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tests'))
import triangle_emit_arm11_test as test
OUT=ROOT/'builds/arm11-triangle-emit-benchmark'
OUT.mkdir(parents=True,exist_ok=True)
capacity=int(re.search(r'constexpr size_t MAX_TRI_BUFFER = (\d+);',(ROOT/test.pair.SOURCE).read_text()).group(1))
code='#include <3ds.h>\n#include <stdio.h>\n#include <inttypes.h>\n'+test.source_code(capacity)
# Hardware correctness covers all 1024 layout/clip combinations and float edges;
# QEMU runs the full 32768-case version. Reduce large fixture initialization here.
code+=test.TEST.replace('trial<32768','trial<2048').replace('trial<4096','trial<1024')
code+=r'''
#include "platform/3ds/include/depth_snapshot_3ds.h"
#define DepthSnapshot3DS OriginalDepthSnapshot3DS
#include "tests/fixtures/depth_snapshot_before.h"
#undef DepthSnapshot3DS
static volatile uint32_t checksum;
static void oldEmit(){a.EmitTriangle(va,false);}
static void newEmit(){b.EmitTriangle(va,false);}
static void oldRect(){a.EmitTriangle(va,true);}
static void newRect(){b.EmitTriangle(va,true);}
using EmitFn=void(*)();
static uint64_t timedEmit(EmitFn input){
 EmitFn volatile fn=input;
 uint64_t start=svcGetSystemTick();
 for(unsigned n=0;n<32768;n++)fn();
 return svcGetSystemTick()-start;
}
static uint32_t bufferChecksum(const float* p,unsigned n){
 uint32_t h=2166136261u;for(unsigned i=0;i<n;i++){uint32_t u;memcpy(&u,p+i,4);h=(h^u)*16777619u;}return h;
}
static void emitBench(FILE* f){
 const unsigned variants[]={0,1,2,3,7,39,66,322,768,864,896,928,960};
 const char* names[]={"shade","shade_primitive","texture_shade","texture_shade_primitive",
 "texture_shade_environment","texture_shade_environment_rect","fog_fallback","two_texture_fallback",
 "setup25_opaque_fog","opaque_fog_environment_rect","live_fog_alpha_prim_keycenter",
 "live_fog_alpha_prim_keycenter_rect","live_fog_alpha_zero"};
 for(unsigned v=0;v<sizeof(variants)/sizeof(variants[0]);v++){
  uint32_t seed=state;setup(a,variants[v],0);state=seed;setup(b,variants[v],0);
  b.SelectTriangleEmitter();
  a.mBufVboLen=b.mBufVboLen=a.mBufVboNumTris=b.mBufVboNumTris=0;
  for(unsigned n=0;n<3;n++)vertices[n]={float(n)-1.0f,float(n)*0.5f,0.25f,1.0f,float(n)*7,float(n)*11,{64,128,192,255},0};
  EmitFn oldFn=(variants[v]&32)?oldRect:oldEmit, newFn=(variants[v]&32)?newRect:newEmit;
  timedEmit(oldFn);timedEmit(newFn);
  if(f)fprintf(f,"# %s iterations=32768 triangle_capacity=%u selection=once_before_timing cached_path=%s\n",
   names[v],MAX_TRI_BUFFER,b.mTriState.emitter==TriStateCache::Emitter::Generic?"negative":
   b.mTriState.emitter==TriStateCache::Emitter::StandardFog?"standard_fog":"shade");
  for(unsigned round=0;round<7;round++){
   uint64_t oldTicks,newTicks;
   if(round&1){newTicks=timedEmit(newFn);oldTicks=timedEmit(oldFn);}
   else{oldTicks=timedEmit(oldFn);newTicks=timedEmit(newFn);}
   unsigned stride=4+(a.mTriState.usedTextures[0]?2:0)+(a.mTriState.usedTextures[1]?2:0)+(a.mTriState.use_fog?4:0)+a.mTriState.numInputs*(a.mTriState.use_alpha?4:3);
   uint32_t oldSum=bufferChecksum(a.mBufVbo,3*stride),newSum=bufferChecksum(b.mBufVbo,3*stride);
   checksum=checksum^oldSum^newSum;
   if(oldSum!=newSum)failures[0]++;
   if(f)fprintf(f,"%s,%u,%s,%llu,%llu,%u,%u\n",names[v],round,round&1?"new-first":"old-first",
    (unsigned long long)oldTicks,(unsigned long long)newTicks,oldSum,newSum);
  }
  printf("Emitter %s done\n",names[v]);
 }
}
static uint32_t depthSource[512*256];
static OriginalDepthSnapshot3DS oldDepth;
static DepthSnapshot3DS newDepth;
struct DepthShape{uint32_t width,height,contentWidth,contentHeight;bool rotate;const char* name;};
template<class D> __attribute__((noinline)) static uint64_t timedDepth(D& depth,const DepthShape& s){
 uint64_t start=svcGetSystemTick();
 for(unsigned n=0;n<128;n++){
  if(!depth.Capture(depthSource,s.width,s.height,s.contentWidth,s.contentHeight,s.rotate))failures[0]++;
  asm volatile("":::"memory");
 }
 uint64_t elapsed=svcGetSystemTick()-start;
 checksum=checksum^depth.Sample(123,87);return elapsed;
}
static void depthBench(FILE* f){
 for(auto& p:depthSource)p=rnd();
 const DepthShape shapes[]={{512,256,320,240,false,"depth_padded_512x256_content320x240"},
 {240,400,400,240,true,"depth_native_240x400_content400x240_rotated"}};
 for(const auto& s:shapes){
  oldDepth.Reset();newDepth.Reset();
  if(!oldDepth.Capture(depthSource,s.width,s.height,s.contentWidth,s.contentHeight,s.rotate)||
     !newDepth.Capture(depthSource,s.width,s.height,s.contentWidth,s.contentHeight,s.rotate)){failures[0]++;return;}
  uint32_t oldSum=0,newSum=0;
  for(unsigned y=0;y<240;y++)for(unsigned x=0;x<400;x++){
   uint16_t o=oldDepth.Sample(x,y),n=newDepth.Sample(x,y);if(o!=n)failures[0]++;oldSum+=o;newSum+=n;
  }
  if(f)fprintf(f,"# %s iterations=128 exact_pixels=96000 checksum=%u\n",s.name,oldSum);
  if(failures[0])return;
  timedDepth(oldDepth,s);timedDepth(newDepth,s);
  for(unsigned round=0;round<7;round++){
   uint64_t oldTicks,newTicks;
   if(round&1){newTicks=timedDepth(newDepth,s);oldTicks=timedDepth(oldDepth,s);}
   else{oldTicks=timedDepth(oldDepth,s);newTicks=timedDepth(newDepth,s);}
   if(f)fprintf(f,"%s,%u,%s,%llu,%llu,%u,%u\n",s.name,round,round&1?"new-first":"old-first",
    (unsigned long long)oldTicks,(unsigned long long)newTicks,oldSum,newSum);
  }
  printf("%s done\n",s.name);
 }
}
int main(){
 gfxInitDefault();consoleInit(GFX_TOP,nullptr);
 bool newModel=false;APT_CheckNew3DS(&newModel);osSetSpeedupEnable(newModel);
 printf("ARM11 complete emitter / depth benchmark\nCorrectness running...\n");
 correctness();printf("Emitter correctness failures: %lu\n",(unsigned long)failures[0]);
 FILE* f=fopen("sdmc:/3ds/soh/arm11-triangle-emit-benchmark.csv","w");
 if(f)fprintf(f,"# model=%s emitter_correctness_cases=3664 failures=%lu\nworkload,round,order,old_ticks,new_ticks,old_checksum,new_checksum\n",
  newModel?"new":"old",(unsigned long)failures[0]);
 if(!failures[0]){emitBench(f);depthBench(f);}
 if(f){fprintf(f,"# final_failures=%lu\n",(unsigned long)failures[0]);fclose(f);}
 printf("Finished. Failures: %lu\nCSV %s\nSTART exits.\n",(unsigned long)failures[0],f?"saved to sdmc:/3ds/soh":"open failed");
 while(aptMainLoop()){hidScanInput();if(hidKeysDown()&KEY_START)break;gfxFlushBuffers();gfxSwapBuffers();gspWaitForVBlank();}
 gfxExit();return failures[0]!=0;
}
'''
cpp=OUT/'benchmark.cpp';cpp.write_text(code)
dkp=test.DKP
elf=OUT/'soh-arm11-triangle-emit-benchmark.elf'
# Match the renderer's existing -O3, strict float and builtin optimization.
flags=[f for f in test.FLAGS if f not in ['-fno-exceptions','-O2','-fno-builtin']]+['-O3']
subprocess.run([str(test.CXX),*flags,'-mtp=soft','-mword-relocations','-D__3DS__','-DARM11',
 '-Wl,-z,noexecstack','-DSOH3DS_ARM11_TRIANGLE_EMIT=1','-DBENCHMARK','-I'+str(dkp/'libctru/include'),'-I'+str(ROOT),
 '-I'+str(ROOT/'third_party/libultraship/include'),'-specs='+str(dkp/'devkitARM/arm-none-eabi/lib/3dsx.specs'),
 str(cpp),str(test.KERNEL),'-L'+str(dkp/'libctru/lib'),'-lctru','-lm','-o',str(elf)],check=True)
three=OUT/'soh-arm11-triangle-emit-benchmark.3dsx'
subprocess.run([str(dkp/'tools/bin/3dsxtool'),str(elf),str(three)],check=True)
files=[cpp,elf,three,test.KERNEL,ROOT/test.pair.SOURCE,ROOT/test.pair.HEADER,Path(test.__file__),
 ROOT/'platform/3ds/include/depth_snapshot_3ds.h',
 ROOT/'tests/fixtures/triangle_emit_before.cpp',ROOT/'tests/fixtures/depth_snapshot_before.h']
manifest={'description':'Complete wrapper and depth Capture; physical timings pending',
          'compiler_flags':flags,
          'sha256':{str(p.relative_to(ROOT)):hashlib.sha256(p.read_bytes()).hexdigest() for p in files}}
(OUT/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
print(three)
