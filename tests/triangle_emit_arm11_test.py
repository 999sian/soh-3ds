#!/usr/bin/env python3
"""Exact integrated EmitTriangle oracle differential on ARM11, including fallback.

Catches wrong admission, operation reordering, channel selection, output length,
capacity flush and kernel ABI corruption. The immutable pre-change emitter and
current emitter/Flush/LoadedVertex/TriState are extracted, backend drawing alone
is replaced with an exact captured output. Set MUTATE=omit_half for a negative
control. This module also supplies the complete wrapper to the 3DS benchmark.
"""
from pathlib import Path
import hashlib
import os
import re
import subprocess
import tempfile
_mutation = os.environ.pop('MUTATE', None)
try:
    import triangle_pair_test as pair
finally:
    if _mutation is not None:
        os.environ['MUTATE'] = _mutation
ROOT = pair.ROOT
DKP = Path(os.environ.get('DEVKITPRO', str(Path.home() / 'dkp-root/opt/devkitpro')))
CXX = DKP / 'devkitARM/bin/arm-none-eabi-g++'
KERNEL = ROOT / 'third_party/libultraship/src/fast/triangle_emit_arm11.S'
FLAGS = ['-std=c++20', '-O2', '-march=armv6k', '-mtune=mpcore', '-mfpu=vfp', '-mfloat-abi=hard',
         '-fno-fast-math', '-ffp-contract=off', '-fno-exceptions', '-fno-rtti']

def source_code(capacity=4):
    old = (ROOT / 'tests/fixtures/triangle_emit_before.cpp').read_text()
    assert hashlib.sha256(old.encode()).hexdigest() == '5c8cd1227dd0ad9f9961659a4c858a480f4a2dd7840e7688cb048710cb2c5b12'
    source = (ROOT / pair.SOURCE).read_text()
    assert 'void Interpreter::SelectTriangleEmitter(' in source, 'missing cached production emitter selector'
    if os.getenv('MUTATE') == 'stale_selector':
        before = 'ts.emitter = TriStateCache::Emitter::Generic;'
        assert before in source
        source = source.replace(before, '// mutation: retain the previous selection', 1)
    pre = '''
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "fast/triangle_emit_3ds.h"
constexpr unsigned MAX_TRI_BUFFER = 4;
constexpr float N64_PRIM_DEPTH_MAX = 32767.0f;
constexpr unsigned G_TL_LOD = 1u << 16;
enum { G_CCMUX_PRIMITIVE=3,G_CCMUX_SHADE=4,G_CCMUX_ENVIRONMENT=5,
G_CCMUX_KEY_CENTER=6,G_CCMUX_CONVERT_K4=7,G_CCMUX_KEY_SCALE=8,
G_CCMUX_PRIMITIVE_ALPHA=10,G_CCMUX_ENV_ALPHA=12,G_CCMUX_LOD_FRACTION=13,
G_CCMUX_PRIM_LOD_FRAC=14,G_CCMUX_CONVERT_K5=15,G_ACMUX_PRIM_LOD_FRAC=16 };
struct ShaderProgram; struct TextureCacheNode;
struct GfxClipParameters { bool z_is_from_0_to_1, invertY; };
enum class Soh3dsVertexPath { Generic, Arm11, Compact, CompactClip };
static inline void Soh3dsProfileVertexBatch(Soh3dsVertexPath, unsigned) {}
enum class Soh3dsProfileSection { TriangleEmit };
struct Soh3dsProfileScope { explicit Soh3dsProfileScope(Soh3dsProfileSection) {} };
'''
    pre += '\n'.join(pair.structure(n) for n in ['RGBA','LoadedVertex','ColorCombiner','TriStateKey','TriStateCache'])
    pre += '''
struct RDP {
 RGBA prim_color,env_color,fog_color,blend_color,grayscale_color,key_center,key_scale;
 uint8_t prim_lod_fraction; int16_t convert_k[6]; uint32_t other_mode_l; uint16_t prim_depth;
};
struct Backend {
 unsigned len, tris, flushes; float depth; float data[512];
 void SetCurrentPrimDepth(float d) { depth=d; }
 __attribute__((noinline)) void DrawTriangles(float* p,unsigned l,unsigned t) {
  len=l;tris=t;flushes++;
#ifndef BENCHMARK
  memcpy(data,p,l*4);
#else
  asm volatile(""::"r"(p),"r"(l),"r"(t):"memory");
#endif
 }
};
'''
    cls = '''
struct Interpreter {
 RDP rdp; RDP* mRdp; ColorCombiner comb; TriStateCache mTriState;
 GfxClipParameters mClipParameters; float storage[512]; float* mBufVbo;
 unsigned mBufVboLen,mBufVboNumTris; Backend backend; Backend* mRapi;
 void Flush(); __attribute__((noinline)) void EmitTriangle(LoadedVertex* const[3],bool);
 void SelectTriangleEmitter();
 bool TryEmitTriangleArm11(LoadedVertex* const[3],bool);
};
'''
    pre += '\nnamespace Original {\n' + cls + old + pair.function(source, 'void Interpreter::Flush(') + '\n}\n'
    pre += '\nnamespace Candidate {\n' + cls + pair.function(source,'void Interpreter::SelectTriangleEmitter(')
    pre += pair.function(source,'bool Interpreter::TryEmitTriangleArm11(')
    pre += pair.function(source,'void Interpreter::EmitTriangle(') + pair.function(source,'void Interpreter::Flush(') + '\n}\n'
    return pre.replace('MAX_TRI_BUFFER = 4', f'MAX_TRI_BUFFER = {capacity}').replace('storage[512]', 'storage[MAX_TRI_BUFFER*3*32+128]')

TEST = r'''
static unsigned kernelCalls;
#ifndef BENCHMARK
extern "C" float* __real_Soh3dsEmitTriangleArm11(float*,const void* const[3],const Soh3dsTriangleEmitParams*);
extern "C" float* __wrap_Soh3dsEmitTriangleArm11(float* o,const void* const v[3],const Soh3dsTriangleEmitParams* p){
 ++kernelCalls;return __real_Soh3dsEmitTriangleArm11(o,v,p);
}
extern "C" float* __real_Soh3dsEmitFogTriangleArm11(float*,const void* const[3],const Soh3dsFogEmitParams*);
extern "C" float* __wrap_Soh3dsEmitFogTriangleArm11(float* o,const void* const v[3],const Soh3dsFogEmitParams* p){
 ++kernelCalls;return __real_Soh3dsEmitFogTriangleArm11(o,v,p);
}
extern "C" unsigned Soh3dsFogEmitAbi(float*,const void* const[3],const Soh3dsFogEmitParams*);
extern "C" unsigned Soh3dsTriangleEmitAbi(float*,const void* const[3],const Soh3dsTriangleEmitParams*);
#endif
static uint32_t state=0x913abcd, failures[6];
static uint32_t rnd(){state=state*1664525+1013904223;return state;}
static float value(unsigned trial){
 static const uint32_t edges[]={0,0x80000000,1,0x80000001,0x007fffff,0x00800000,
 0x7f7fffff,0xff7fffff,0x7f800000,0xff800000,0x7fc12345,0xff812345};
 uint32_t bits=edges[rnd()%12];float f;memcpy(&f,&bits,4);
 return trial<4096 ? float(int(rnd()%65536)-32768)/128.0f : f;
}
static Original::Interpreter a;
static Candidate::Interpreter b;
static LoadedVertex vertices[3], saved[3];
static LoadedVertex* va[3]={&vertices[0],&vertices[1],&vertices[2]};
template<class I> static void setup(I& i,unsigned variant,unsigned trial,bool reset=true){
 if(reset)memset(&i,0,sizeof(i));i.mRdp=&i.rdp;i.mRapi=&i.backend;i.mBufVbo=i.storage;i.mTriState.comb=&i.comb;
 auto& ts=i.mTriState;ts.numInputs=(variant&1)?2:1;ts.use_alpha=true;
 ts.use_fog=ts.use_blend_color=ts.use_grayscale=false;ts.tm=0;
 ts.usedTextures[0]=(variant&2)!=0;ts.usedTextures[1]=false;
 i.comb.shader_input_mapping[0][0]=i.comb.shader_input_mapping[1][0]=G_CCMUX_SHADE;
 i.comb.shader_input_mapping[0][1]=i.comb.shader_input_mapping[1][1]=(variant&4)?G_CCMUX_ENVIRONMENT:G_CCMUX_PRIMITIVE;
 i.mClipParameters.invertY=(variant&8)!=0;i.mClipParameters.z_is_from_0_to_1=(variant&16)!=0;
 for(unsigned n=0;n<2;n++){
  ts.uMul[n]=value(trial);ts.vMul[n]=value(trial);ts.uAdd[n]=value(trial);ts.vAdd[n]=value(trial);
  ts.halfU[n]=value(trial);ts.halfV[n]=value(trial);ts.clampS[n]=0.8f;ts.clampT[n]=0.9f;
 }
 auto* c=(uint8_t*)&i.rdp;for(unsigned n=0;n<28;n++)c[n]=rnd();
 // Each unsupported layout exercises the actual generic fallback.
 unsigned fallback=(variant>>6);
 if(fallback==1)ts.use_fog=true;
 if(fallback==2)ts.use_fog=true,ts.use_blend_color=true;
 if(fallback==3)ts.use_grayscale=true;
 if(fallback==4)ts.use_alpha=false;
 if(fallback==5)ts.usedTextures[1]=true;
 if(fallback==6)ts.tm=15;
 if(fallback==7)i.comb.shader_input_mapping[1][0]=G_CCMUX_ENVIRONMENT;
 if(fallback==8)i.comb.shader_input_mapping[0][0]=G_CCMUX_KEY_CENTER;
 if(fallback==9)i.comb.shader_input_mapping[1][1]=G_CCMUX_PRIMITIVE;
 if(fallback==10)ts.numInputs=0;
 if(fallback==11)ts.numInputs=3;
 // Stock SetupDL25: MODULATEIDECALA + MODULATEIA_PRIM2, opaque fog.
 // RGB/alpha inputs are independently numbered; the unused alpha mapping
 // deliberately differs from RGB and must not block this opaque layout.
 if(fallback>=12 && fallback<=15){
  ts.use_alpha=false;ts.use_fog=true;ts.usedTextures[0]=ts.usedTextures[1]=true;ts.numInputs=2;
  i.comb.shader_input_mapping[0][0]=G_CCMUX_SHADE;
  i.comb.shader_input_mapping[0][1]=fallback==12?G_CCMUX_PRIMITIVE:G_CCMUX_ENVIRONMENT;
  i.comb.shader_input_mapping[1][0]=G_CCMUX_PRIMITIVE;i.comb.shader_input_mapping[1][1]=0;
  if(fallback>=14){ts.use_alpha=true;i.comb.shader_input_mapping[1][0]=fallback==14?G_CCMUX_PRIMITIVE:0;
   i.comb.shader_input_mapping[1][1]=fallback==14?G_CCMUX_KEY_CENTER:0;}
 }
 i.mBufVboLen=7;i.mBufVboNumTris=(trial%3==0)?MAX_TRI_BUFFER-1:0;
 for(unsigned n=0;n<512;n++)i.mBufVbo[n]=float(n)-21.0f;
}
static bool equal(){
 return a.mBufVboLen==b.mBufVboLen && a.mBufVboNumTris==b.mBufVboNumTris &&
 !memcmp(a.mBufVbo,b.mBufVbo,sizeof(a.storage)) && !memcmp(&a.backend,&b.backend,sizeof(a.backend)) &&
 !memcmp(vertices,saved,sizeof(vertices));
}
static void correctness(){
 for(unsigned trial=0;trial<32768;trial++){
  const unsigned variant=trial%1024;
  uint32_t seed=state;setup(a,variant,trial);state=seed;setup(b,variant,trial);
  for(auto& v:vertices){v.x=value(trial);v.y=value(trial);v.z=value(trial);v.w=value(trial);
   v.u=value(trial);v.v=value(trial);v.color={uint8_t(rnd()),uint8_t(rnd()),uint8_t(rnd()),uint8_t(rnd())};}
  memcpy(saved,vertices,sizeof(vertices));
  kernelCalls=0;
  b.SelectTriangleEmitter();
  a.EmitTriangle(va,(variant&32)!=0);b.EmitTriangle(va,(variant&32)!=0);
#ifndef BENCHMARK
  if((variant<64 || variant>=768) && kernelCalls!=SOH3DS_ARM11_TRIANGLE_EMIT){failures[0]++;failures[5]++;}
  if(((variant>>6)>=1 && (variant>>6)<=8) && kernelCalls){failures[0]++;failures[5]++;}
#endif
  if(!equal()){failures[0]++;if(!failures[1]){failures[1]=trial+1;
   for(unsigned n=0;n<512;n++)if(memcmp(a.mBufVbo+n,b.mBufVbo+n,4)){failures[2]=n;memcpy(failures+3,a.mBufVbo+n,4);memcpy(failures+4,b.mBufVbo+n,4);break;}}}
 }
 // Every admitted alpha-source pair, both uniform RGB sources, all clip/rect
 // options and both ordinary and edge floats. Neither expected words nor
 // admitted alpha constants are synthesized: the immutable emitter is oracle.
 const uint8_t alphaSources[]={0,G_CCMUX_SHADE,G_CCMUX_PRIMITIVE,G_CCMUX_ENVIRONMENT,G_CCMUX_KEY_CENTER,G_CCMUX_KEY_SCALE};
 for(unsigned pair=0;pair<36;pair++)for(unsigned option=0;option<16;option++)for(unsigned edge=0;edge<2;edge++){
  unsigned variant=768+(option&1?64:0)+(option&2?8:0)+(option&4?16:0)+(option&8?32:0);
  uint32_t seed=state;setup(a,variant,edge?5000:0);state=seed;setup(b,variant,edge?5000:0);
  a.mTriState.use_alpha=b.mTriState.use_alpha=true;
  a.comb.shader_input_mapping[1][0]=b.comb.shader_input_mapping[1][0]=alphaSources[pair/6];
  a.comb.shader_input_mapping[1][1]=b.comb.shader_input_mapping[1][1]=alphaSources[pair%6];
  for(auto& v:vertices){v.x=value(edge?5000:0);v.y=value(edge?5000:0);v.z=value(edge?5000:0);v.w=value(edge?5000:0);
   v.u=value(edge?5000:0);v.v=value(edge?5000:0);v.color={uint8_t(rnd()),uint8_t(rnd()),uint8_t(rnd()),uint8_t(rnd())};}
  memcpy(saved,vertices,sizeof(vertices));kernelCalls=0;b.SelectTriangleEmitter();
  a.EmitTriangle(va,(variant&32)!=0);b.EmitTriangle(va,(variant&32)!=0);
  if(!equal()){failures[0]++;failures[5]++;}
#ifndef BENCHMARK
  if(kernelCalls!=SOH3DS_ARM11_TRIANGLE_EMIT){failures[0]++;failures[5]++;}
#endif
 }
 // Neighbors of the admitted fog layout must retain the exact fallback.
 for(unsigned scenario=0;scenario<16;scenario++){
  uint32_t seed=state;setup(a,768,0);state=seed;setup(b,768,0);
  auto change=[&](auto& i){
   i.mTriState.use_alpha=true;
   i.comb.shader_input_mapping[1][0]=G_CCMUX_PRIMITIVE;i.comb.shader_input_mapping[1][1]=0;
   switch(scenario){
    case 0:case 1:i.comb.shader_input_mapping[1][scenario]=31;break;
    case 2:case 3:i.comb.shader_input_mapping[1][scenario-2]=G_CCMUX_LOD_FRACTION;break;
    case 4:case 5:i.comb.shader_input_mapping[1][scenario-4]=G_ACMUX_PRIM_LOD_FRAC;break;
    case 6:i.mTriState.use_blend_color=true;break;
    case 7:i.mTriState.use_grayscale=true;break;
    case 8:i.mTriState.tm=1;break;
    case 9:i.mTriState.usedTextures[0]=false;break;
    case 10:i.mTriState.usedTextures[1]=false;break;
    case 11:i.mTriState.numInputs=1;break;
    case 12:i.mTriState.numInputs=3;break;
    case 13:i.comb.shader_input_mapping[0][0]=G_CCMUX_ENVIRONMENT;break;
    case 14:i.comb.shader_input_mapping[0][1]=G_CCMUX_SHADE;break;
    case 15:i.mTriState.use_fog=false;break;
   }
  };change(a);change(b);memcpy(saved,vertices,sizeof(vertices));kernelCalls=0;b.SelectTriangleEmitter();
  a.EmitTriangle(va,false);b.EmitTriangle(va,false);
  if(!equal()){failures[0]++;failures[5]++;}
#ifndef BENCHMARK
  if(kernelCalls){failures[0]++;failures[5]++;}
#endif
 }
 // Reuse a layout while every live colour, clip flag and rectangle flag changes.
 // A selector that caches colour values breaks this case.
 // Re-select on the same interpreter through positive -> negative -> positive
 // transitions; none of these tests assign a cached emitter kind themselves.
 const unsigned transitions[]={3,322,768,66,896,960,7};
 for(unsigned layout:transitions){
  uint32_t seed=state;setup(a,layout,0,false);state=seed;setup(b,layout,0,false);
  if(layout==960){
   a.comb.shader_input_mapping[1][0]=b.comb.shader_input_mapping[1][0]=G_CCMUX_KEY_SCALE;
   a.comb.shader_input_mapping[1][1]=b.comb.shader_input_mapping[1][1]=G_CCMUX_ENVIRONMENT;
  }
  b.SelectTriangleEmitter();const auto key=b.mTriState.key;
  for(unsigned draw=0;draw<64;draw++){
   auto live=[&](auto& i){
    auto* colors=(uint8_t*)&i.rdp;for(unsigned c=0;c<28;c++)colors[c]=uint8_t(draw*37+c*19);
    i.mClipParameters.invertY=(draw&1)!=0;i.mClipParameters.z_is_from_0_to_1=(draw&2)!=0;
   };live(a);live(b);memcpy(saved,vertices,sizeof(vertices));kernelCalls=0;
   a.EmitTriangle(va,(draw&4)!=0);b.EmitTriangle(va,(draw&4)!=0);
   if(!equal() || !(key==b.mTriState.key)){failures[0]++;failures[5]++;}
#ifndef BENCHMARK
   unsigned expected=(layout==322 || layout==66)?0:SOH3DS_ARM11_TRIANGLE_EMIT;
   if(kernelCalls!=expected){failures[0]++;failures[5]++;}
#endif
  }
 }
#ifndef BENCHMARK
 Soh3dsTriangleEmitParams params{};
 Soh3dsFogEmitParams fogParams{};
 const void* pointers[3]={va[0],va[1],va[2]};
 for(unsigned flags=0;flags<32;flags++){
  params.flags=flags;fogParams.flags=flags;
  if(Soh3dsTriangleEmitAbi(b.mBufVbo,pointers,&params)){failures[0]++;failures[5]++;}
  if(Soh3dsFogEmitAbi(b.mBufVbo,pointers,&fogParams)){failures[0]++;failures[5]++;}
 }
#endif
}
'''
RUNTIME = r'''
extern "C" void* memcpy(void* d,const void* s,size_t n){auto* x=(unsigned char*)d;auto* y=(const unsigned char*)s;while(n--)*x++=*y++;return d;}
extern "C" void* memset(void* d,int v,size_t n){auto* x=(unsigned char*)d;while(n--)*x++=v;return d;}
extern "C" int memcmp(const void* a,const void* b,size_t n){auto*x=(const unsigned char*)a;auto*y=(const unsigned char*)b;while(n--){if(*x!=*y)return *x-*y;x++;y++;}return 0;}
extern "C" void __aeabi_unwind_cpp_pr0(){} extern "C" void __aeabi_unwind_cpp_pr1(){}
extern "C" void _start(){correctness();
 register uint32_t r0 asm("r0")=1;register void* r1 asm("r1")=failures;register uint32_t r2 asm("r2")=24;
 asm volatile("mov r7,#4\nsvc #0":"+r"(r0):"r"(r1),"r"(r2):"r7","memory");
 r0=failures[0]!=0;asm volatile("mov r7,#1\nsvc #0"::"r"(r0):"r7","memory");__builtin_unreachable();}
'''

def main():
    runtime_helpers, runtime_entry = RUNTIME.split('extern "C" void _start()', 1)
    code=source_code()+TEST+'extern "C" void _start()'+runtime_entry
    with tempfile.TemporaryDirectory(prefix='soh-triangle-emit-') as d:
        p=Path(d);(p/'test.cpp').write_text(code)
        # Only the freestanding libc definitions disable builtins, to avoid
        # compiling memcpy/memset into recursive calls to themselves. The
        # production wrappers use the same builtin optimization as the game.
        (p/'runtime.cpp').write_text('#include <stddef.h>\n'+runtime_helpers)
        subprocess.run([str(CXX),*FLAGS,'-fno-builtin','-c',str(p/'runtime.cpp'),'-o',str(p/'runtime.o')],check=True)
        kernel=KERNEL
        mutation=os.getenv('MUTATE')
        if mutation and mutation != 'stale_selector':
            changes={'omit_half':('vaddeq.f32 s2, s2, s10','vmoveq.f32 s2, s2'),
                     'fpscr':('vmsr fpscr, r7','nop'),
                     'fog_alpha':('vstmia r0!, {s20-s22}\n    ldrb r3, [r4, #27]',
                                  'vstmia r0!, {s20-s22}\n    ldrb r3, [r4, #24]'),
                     'fog_half1':('vaddeq.f32 s2, s2, s18','vmoveq.f32 s2, s2'),
                     'fog_vfp':('vpop {d8-d14}','add sp, sp, #56')}
            before,after=changes[mutation]
            assert before in KERNEL.read_text()
            kernel=p/'mutated.S';kernel.write_text(KERNEL.read_text().replace(before,after))
        for optimization,enabled in [(o,e) for o in ['-O2','-O3'] for e in [0,1]]:
            exe=p/f'test-{optimization}-{enabled}'
            flags=[f for f in FLAGS if not f.startswith('-O')]+[optimization]
            subprocess.run([str(CXX),*flags,f'-DSOH3DS_ARM11_TRIANGLE_EMIT={enabled}',
                '-I'+str(ROOT/'third_party/libultraship/include'),'-nostdlib','-Wl,-e,_start','-Wl,-Ttext=0x10000','-Wl,-z,noexecstack','-Wl,--wrap=Soh3dsEmitTriangleArm11','-Wl,--wrap=Soh3dsEmitFogTriangleArm11',
                str(p/'test.cpp'),str(p/'runtime.o'),str(kernel),str(ROOT/'tests/fixtures/triangle_emit_abi.S'),'-lgcc','-o',str(exe)],check=True)
            run=subprocess.run(['qemu-arm','-cpu','arm11mpcore',str(exe)],stdout=subprocess.PIPE)
            assert run.returncode==0 and run.stdout==bytes(24),(optimization,enabled,run.returncode,run.stdout.hex())
            dis=subprocess.check_output([str(DKP/'devkitARM/bin/arm-none-eabi-objdump'),'-d',str(exe)],text=True)
            body=dis.split('<Soh3dsEmitTriangleArm11>:',1)[1].split('\n\n',1)[0]
            assert not re.search(r'\b(vfma|vfms|vdiv|vld[1-4]|vst[1-4]|q[0-9]+)\b',body,re.I),body
            assert not re.search(r'\b(?:d(?:[89]|1[0-5])|s(?:1[6-9]|2[0-9]|3[01]))\b',body),body
            assert 'push\t{r4, r5, r6, r7, r8, lr}' in body and 'vmsr\tfpscr, r7' in body
            fog=dis.split('<Soh3dsEmitFogTriangleArm11>:',1)[1].split('\n\n',1)[0]
            assert not re.search(r'\b(vfma|vfms|vdiv|vld[1-4]|vst[1-4]|q[0-9]+)\b',fog,re.I),fog
            assert 'vpush\t{d8-d14}' in fog and 'vpop\t{d8-d14}' in fog and 'vmsr\tfpscr, r7' in fog
            print(f'PASS: {optimization} guard={enabled}: 34384 complete emission comparisons, actual selector, live colours, exact words and flush state')

if __name__=='__main__':main()
