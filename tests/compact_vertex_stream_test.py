#!/usr/bin/env python3
"""Exact compact stream differential against immutable bd091259 complete emission/packing.

The production emitter, compact packing, layout reconstruction, clipping and
constant selection are extracted. Hardware benchmark generation reuses this
harness, replacing only the GPU draw endpoint with captured packed bytes.
"""
from pathlib import Path
import argparse
import hashlib
import os
import subprocess
import tempfile
ROOT = Path(__file__).resolve().parents[1]
FIXTURES = Path('tests/fixtures')
LUS = Path('third_party/libultraship')
SOURCE = LUS / 'src/fast/interpreter.cpp'
BACKEND = Path('platform/3ds/source/gfx_citro3d.cpp')
HEADER = LUS / 'include/fast/backends/gfx_compact_vertex.h'

def block(source, signature):
    start=source.index(signature)
    return source[start:source.index('\n}',start)+2]

ORACLE_FILES = {
    SOURCE: FIXTURES/'compact_stream_bd091259_emit.cpp',
    BACKEND: FIXTURES/'compact_stream_bd091259_backend.cpp',
}
BASELINE_HASHES = {
    ORACLE_FILES[SOURCE]: '49faeb486fd3594129af311f1a9a1229c302bfdf2613556bd5aae050f2eddf7c',
    ORACLE_FILES[BACKEND]: 'd3451a625187a38e6f1558ceba76b617ae03fc85d110d4048d32a201ff13c821',
    LUS/'src/fast/triangle_emit_arm11.S': 'b9db68eae4557e0c41dd018bdcc0f5ce251fe68ed2be2adb4487150f711eda75',
    FIXTURES/'compact_stream_fixture.h': 'e194d88ada8fe5860e35c2a9face246c70373c771f7bcf4f6528f64a2c9d5e98',
    FIXTURES/'compact_stream_arm_runtime.cpp': 'e7a6d91ea52ab152691e4c5203c527302dacf353e06478572783027819bf28be',
}
DKP = Path(os.environ.get('DEVKITPRO', str(Path.home() / 'dkp-root/opt/devkitpro')))
CXX = DKP / 'devkitARM/bin/arm-none-eabi-g++'
KERNEL = ROOT / LUS / 'src/fast/triangle_emit_arm11.S'
FLAGS = ['-std=c++20', '-O3', '-march=armv6k', '-mtune=mpcore', '-mfpu=vfp', '-mfloat-abi=hard',
         '-fno-fast-math', '-ffp-contract=off', '-fno-exceptions', '-fno-rtti']

def harness(capacity=256, instrumented=True):
    assert (ROOT/HEADER).exists(), 'Missing versioned compact stream representation'
    source=(ROOT/SOURCE).read_text(); backend=(ROOT/BACKEND).read_text()
    for path, digest in BASELINE_HASHES.items():
        assert hashlib.sha256((ROOT/path).read_bytes()).hexdigest()==digest, 'Immutable fixture changed: '+str(path)
    before={path:(ROOT/oracle).read_text() for path,oracle in ORACLE_FILES.items()}
    header=(ROOT/LUS/'include/fast/interpreter.h').read_text()
    structures='\n'.join(block(header,'struct '+name+' {')+';' for name in ['RGBA','LoadedVertex','ColorCombiner','TriStateKey','TriStateCache'])
    pre=(ROOT/FIXTURES/'compact_stream_fixture.h').read_text().replace('COMPACT_FIXTURE_CAPACITY',str(capacity)).replace('// PRODUCTION_STRUCTS',structures)
    if instrumented:
        pre+='static unsigned coverage[4]; static inline void Soh3dsProfileVertexBatch(Soh3dsVertexPath p, unsigned n) { coverage[unsigned(p)]+=n; }\n'
    else:
        pre+='static inline void Soh3dsProfileVertexBatch(Soh3dsVertexPath, unsigned) {}\n'
    pre='''#include <algorithm>\n#include <array>\n#include <cmath>\n#include <cstdint>\n#include <cstring>\n#include "fast/backends/gfx_compact_vertex.h"\nusing namespace Fast;\n''' + pre
    pre+='#undef CHECK\n#undef FIXTURE_COUNTER\n'
    if instrumented:
        pre+='static uint32_t failures[6];\n#define CHECK(x) do { if (!(x)) { ++failures[0]; if (!failures[1]) failures[1]=__LINE__; } } while(0)\n#define FIXTURE_COUNTER(x) do { x; } while(0)\n'
    else:
        # Compile-time omission: even assertion operands are absent from timing.
        pre+='#define CHECK(x) ((void)0)\n#define FIXTURE_COUNTER(x) ((void)0)\n'
    pre+='static void Soh3dsProfilePackBatch(bool, uint32_t) {}\n#include "compiled_channel_3ds.h"\n'
    shader=block((ROOT/'platform/3ds/include/gfx_citro3d.h').read_text(),'struct ShaderProgram {')+';'
    pre+=shader+block(backend,'struct PackedVertex {')+';'
    pre+='constexpr size_t kMaxVertexStrideFloats=64, kVertexBufferCapacity=MAX_TRI_BUFFER*6;\n'
    pre+=block(backend,'uint8_t FloatColorToByte(')
    pre+=block(backend,'void PackGenericVertices(')+block(backend,'bool PackVertices(')
    pre+=block(backend,'int SelectCompactInputs(')+block(backend,'void PackCompactVertices(')
    pre+=block(backend,'size_t ClipTriangleAgainstW(')
    oldbackend=before[BACKEND]
    pre+=block(oldbackend,'uint8_t FloatColorToByte(').replace('FloatColorToByte','BaselineColorToByte')
    pre+=block(oldbackend,'void PackGenericVertices(').replace('PackGenericVertices','BaselinePack').replace('FloatColorToByte','BaselineColorToByte')
    pre+=block(oldbackend,'size_t ClipTriangleAgainstW(').replace('ClipTriangleAgainstW','BaselineClip')
    pre+=r'''
struct Scratch {
 float values[MAX_TRI_BUFFER*6*32];
 void resize(size_t n) { CHECK(n<=MAX_TRI_BUFFER*6*32); }
 float* data() { return values; }
};
struct Pipeline {
 Scratch clipScratch;
 PackedVertex output[MAX_TRI_BUFFER*6];
 PackedVertex* packedVertices=output;
 size_t packedVertexCount=0, count=0;
 std::array<std::array<float,4>,7> inputConstants{};
 std::array<float,4> firstFog{};
 int varying=-1;
 unsigned draws=0;
 float coverScale=1.0f;
 std::array<float,2> textureScaleU={1,1},textureScaleV={1,1},textureOffsetV={0,0};
 std::array<bool,2> rotatedFramebuffer={false,false};
 void Old(float*,size_t,const ShaderProgram*);
 void New(float*,size_t,const ShaderProgram*,const CompactVertexLayout*);
};
'''
    for isnew in (False,True):
        text=backend if isnew else oldbackend
        draw=block(text,'void GfxRenderingAPICitro3D::DrawTrianglesInternal(' if isnew else 'void GfxRenderingAPICitro3D::DrawTriangles(')
        core=draw[draw.index('    const float* drawVertices = bufVbo;'):draw.index('    // Fast3D already applies')]
        core=core.replace('throw std::length_error("3DS packed vertex buffer exhausted");','CHECK(false); return;')
        core=core.replace('static_cast<size_t>(kMaxDrawVertices)','size_t(MAX_TRI_BUFFER*6)')
        if not isnew: core=core.replace('ClipTriangleAgainstW','BaselineClip')
        pack=draw[draw.index('    {\n        Soh3dsProfileScope packProfile'):draw.index('    const int alphaVaryingInput')]
        if not isnew: pack=pack.replace('const bool commonPacked = PackVertices(', 'const bool commonPacked = (BaselinePack(').replace('rotatedFramebuffer);','rotatedFramebuffer), false);')
        pre+=('void Pipeline::New(float* bufVbo,size_t sourceVertexCount,const ShaderProgram* program,const CompactVertexLayout* compactLayout) {\n' if isnew else
              'void Pipeline::Old(float* bufVbo,size_t sourceVertexCount,const ShaderProgram* program) {\n')
        validation=draw[draw.index('    if (compactLayout != nullptr &&'):draw.index('    // A texture whose allocation failed')] if isnew else ''
        pre+='auto* mImpl=this; size_t sourceTriangleCount=sourceVertexCount/3;\ncount=0;\n'+validation+core+pack
        pre+='count=vertexCount;inputConstants=constants;varying=varyingInput;firstFog=fogColor;FIXTURE_COUNTER(++draws);\n}\n'
    pre+=r'''
template<bool UseCandidate> struct FixtureBackend {
 Pipeline pipeline;
 ShaderProgram program;
 bool supports=true;
 unsigned flushes=0,compactCalls=0;
 float depth=0;
 bool SupportsCompactVertexStream() const { return supports; }
 void SetCurrentPrimDepth(float v) {depth=v;}
 void DrawTriangles(float* p,size_t l,size_t t) {
  CHECK(l==t*3*program.strideFloats);FIXTURE_COUNTER(++flushes);
  if constexpr (UseCandidate) pipeline.New(p,t*3,&program,nullptr); else pipeline.Old(p,t*3,&program);
 }
 void DrawCompactTriangles(float* p,size_t t,const CompactVertexLayout& layout) {
  FIXTURE_COUNTER(++flushes);FIXTURE_COUNTER(++compactCalls);pipeline.New(p,t*3,&program,&layout);
 }
};
'''
    api=(ROOT/LUS/'include/fast/backends/gfx_rendering_api.h').read_text()
    def api_method(signature):
        start=api.index(signature); pos=api.index('{',start); depth=1; end=pos+1
        while depth: depth+=(api[end]=='{')-(api[end]=='}'); end+=1
        return api[start:end]
    pre+='struct PortableBackend : FixtureBackend<false> {\n'+api_method('virtual bool SupportsCompactVertexStream()')+api_method('virtual void DrawCompactTriangles(')+'\n};\n'
    pre+='struct Capability { bool SupportsCompactVertexStream() const; };\n'+block(backend,'bool GfxRenderingAPICitro3D::SupportsCompactVertexStream(').replace('GfxRenderingAPICitro3D::','Capability::')
    cls=r'''
struct Interpreter {
 RDP rdp{}; RDP* mRdp=&rdp; ColorCombiner comb{}; TriStateCache mTriState{};
 GfxClipParameters mClipParameters{}; float storage[MAX_TRI_BUFFER*3*32+128]{}; float* mBufVbo=storage;
 size_t mBufVboLen=0,mBufVboNumTris=0; Backend backend; Backend* mRapi=&backend;
 bool mBufVboCompact=false; CompactVertexLayout mBufVboCompactLayout{};
 void Flush(); __attribute__((noinline)) void EmitTriangle(LoadedVertex* const[3],bool);
 void SelectTriangleEmitter();
 bool TryEmitTriangleArm11(LoadedVertex* const[3],bool);
 bool TryEmitTriangleCompact(LoadedVertex* const[3],bool);
};
'''
    for ns,text in [('Original',before[SOURCE]),('Candidate',source)]:
        pre+='namespace '+ns+' {\nusing Backend = FixtureBackend<'+('true' if ns=='Candidate' else 'false')+'>;\n'+cls
        if ns=='Candidate':
            pre+=block(text,'void Interpreter::SelectTriangleEmitter(')
            pre+='\n#if SOH3DS_COMPACT_VERTEX_STREAM\n'+block(text,'bool Interpreter::TryEmitTriangleCompact(')+'\n#endif\n'
        pre+='\n#if SOH3DS_ARM11_TRIANGLE_EMIT\n'+block(text,'bool Interpreter::TryEmitTriangleArm11(')+'\n#endif\n'
        pre+=block(text,'void Interpreter::EmitTriangle(')+block(text,'void Interpreter::Flush(')+'\n}\n'
    mutations={
      'empty_reset': ('if (mBufVboLen == 0) mBufVboCompact = false;', '// mutation: omit aborted-frame reset'),
      'fog_alpha': ('if (layout.fog) destination.color[3] = record.fog[3];','if (layout.fog) destination.color[3] = record.fog[0];'),
      'transition_flush': ('ExpandCompactVerticesInPlace(mBufVbo, mBufVboNumTris * 3, mBufVboCompactLayout);','Flush();'),
      'expand_direction': ('stride * sizeof(float) < sizeof(CompactVertex) ? step : count - 1 - step','count - 1 - step'),
      'input_selection': ('varyingInput = input;\n                break;', 'varyingInput = 0;\n                break;'),
      'freeze_uniform': ('if (ts.numInputs == 2) std::memcpy(record.input[1], uniform, 4);','if (ts.numInputs == 2) std::memcpy(record.input[1], mBufVboNumTris ? ReadCompactVertex(mBufVbo, 0).input[1] : reinterpret_cast<const uint8_t*>(uniform), 4);'),
      'fog_alpha_source': ('case G_CCMUX_KEY_CENTER: alpha[input] = mRdp->key_center.a; break;','case G_CCMUX_KEY_CENTER: alpha[input] = mRdp->prim_color.a; break;'),
    }
    mutation=os.getenv('MUTATE')
    if mutation:
        a,b=mutations[mutation]
        if mutation=='expand_direction':
            header=(ROOT/HEADER).read_text().replace('#pragma once','')
            assert a in header
            pre=pre.replace('#include "fast/backends/gfx_compact_vertex.h"',header.replace(a,b))
        else:
            assert a in pre,mutation
            pre=pre.replace(a,b,1)
    return pre

TEST = r'''
static Original::Interpreter a;
static Candidate::Interpreter b;
static LoadedVertex vertices[3];
static LoadedVertex* va[3]={vertices,vertices+1,vertices+2};
static uint32_t state=0x43824a89,checks;
static uint32_t rnd(){state=state*1664525+1013904223;return state;}
static ShaderProgram layout(const TriStateCache& ts) {
 ShaderProgram p{};p.numInputs=ts.numInputs;p.alpha=ts.use_alpha;p.fog=ts.use_fog;p.grayscale=ts.use_grayscale;
 unsigned off=4;
 for(unsigned t=0;t<2;t++){
  p.usedTextures[t]=ts.usedTextures[t];
  p.clamp[t][0]=(ts.tm&(1u<<(t*2)))!=0;p.clamp[t][1]=(ts.tm&(2u<<(t*2)))!=0;
  if(p.usedTextures[t]){p.textureOffsets[t]=off;off+=2+p.clamp[t][0]+p.clamp[t][1];}
 }
 if(p.fog){p.fogOffset=off;off+=4;}
 if(p.grayscale){p.grayscaleOffset=off;off+=4;}
 for(unsigned n=0;n<p.numInputs;n++){p.inputOffsets[n]=off;off+=p.alpha?4:3;}
 p.strideFloats=off;return p;
}
template<class I> static void setup(I& i,unsigned variant,bool candidate) {
 i.mRdp=&i.rdp;i.mRapi=&i.backend;i.mBufVbo=i.storage;
 i.backend.pipeline.packedVertices=i.backend.pipeline.output;
 i.backend.pipeline.packedVertexCount=0;i.backend.pipeline.coverScale=1.0f;
 i.mBufVboLen=i.mBufVboNumTris=0;i.mBufVboCompact=false;
 i.backend.flushes=i.backend.compactCalls=0;i.backend.supports=true;(void)candidate;
 i.backend.pipeline.draws=0;i.backend.pipeline.count=0;
 i.mTriState={};auto& ts=i.mTriState;ts.comb=&i.comb;ts.use_alpha=true;
 ts.numInputs=(variant&1)?2:1;ts.usedTextures[0]=(variant&2)!=0;
 i.comb.shader_input_mapping[0][0]=i.comb.shader_input_mapping[1][0]=G_CCMUX_SHADE;
 i.comb.shader_input_mapping[0][1]=i.comb.shader_input_mapping[1][1]=(variant&4)?G_CCMUX_ENVIRONMENT:G_CCMUX_PRIMITIVE;
 if(variant&8){ts.use_fog=true;ts.usedTextures[0]=ts.usedTextures[1]=true;ts.numInputs=2;ts.use_alpha=(variant&16)!=0;}
 for(unsigned t=0;t<2;t++){
  ts.uMul[t]=0.0078125f*(t+1);ts.vMul[t]=0.01171875f*(t+1);
  ts.uAdd[t]=-0.275f;ts.vAdd[t]=0.19f;ts.halfU[t]=0.125f;ts.halfV[t]=0.25f;
  ts.clampS[t]=0.6f;ts.clampT[t]=0.9f;
 }
 i.rdp.prim_color={17,81,199,41};i.rdp.env_color={215,117,55,163};i.rdp.fog_color={13,145,79,23};
 i.rdp.key_center={23,63,191,85};i.rdp.key_scale={59,87,71,137};i.rdp.prim_depth=27183;
 i.mClipParameters={false,false};i.backend.program=layout(ts);
 i.backend.pipeline.rotatedFramebuffer={bool(variant&32),bool(variant&64)};
 i.backend.pipeline.textureScaleU={0.625f,0.9375f};i.backend.pipeline.textureScaleV={0.46875f,0.75f};
 i.backend.pipeline.textureOffsetV={0.03125f,0.125f};
}
static void seeds(unsigned n,bool constantShade=false){
 for(unsigned v=0;v<3;v++){
  vertices[v]={float(v)*0.75f-1,float(v)-0.5f,0.25f,1.0f,float(int(n%19)-9)*17,float(int(v+n%13)-6)*23,
      {uint8_t(constantShade?80:n*17+v*37),uint8_t(constantShade?70:n*11+v*41),uint8_t(constantShade?60:n*7+v*29),uint8_t(constantShade?50:n*31+v*83)},0};
 }
}
static void compare(){
 CHECK(a.mBufVboLen==b.mBufVboLen);CHECK(a.mBufVboNumTris==b.mBufVboNumTris);
 CHECK(a.backend.flushes==b.backend.flushes);CHECK(a.backend.depth==b.backend.depth);
 const auto& x=a.backend.pipeline;const auto& y=b.backend.pipeline;
 CHECK(x.count==y.count);CHECK(x.draws==y.draws);
 if(x.count && x.count==y.count){
  CHECK(!memcmp(x.output,y.output,x.count*sizeof(PackedVertex)));
  CHECK(x.varying==y.varying);CHECK(!memcmp(&x.inputConstants,&y.inputConstants,sizeof(x.inputConstants)));CHECK(!memcmp(&x.firstFog,&y.firstFog,sizeof(x.firstFog)));
 }
 ++checks;
}
static float expanded[MAX_TRI_BUFFER*3*32+128];
static void comparePending(){
 CHECK(a.mBufVboLen==b.mBufVboLen);
 memcpy(expanded,b.mBufVbo,sizeof(b.storage));
 if(b.mBufVboCompact)ExpandCompactVerticesInPlace(expanded,b.mBufVboNumTris*3,b.mBufVboCompactLayout);
 CHECK(!memcmp(expanded,a.mBufVbo,a.mBufVboLen*sizeof(float)));
}
static void live(unsigned n){
 a.rdp.prim_color=b.rdp.prim_color={uint8_t(n*3),uint8_t(n*11),uint8_t(n*23),uint8_t(n*37)};
 a.rdp.env_color=b.rdp.env_color={uint8_t(n*5),uint8_t(n*13),uint8_t(n*29),uint8_t(n*41)};
 a.rdp.fog_color=b.rdp.fog_color={uint8_t(n*7),uint8_t(n*17),uint8_t(n*31),uint8_t(n*43)};
 a.rdp.key_center.a=b.rdp.key_center.a=uint8_t(n*19);
 a.rdp.key_scale.a=b.rdp.key_scale.a=uint8_t(n*47);
}
static void correctness(){
 CHECK(Capability{}.SupportsCompactVertexStream()==bool(SOH3DS_COMPACT_VERTEX_STREAM));
 // Run the actual portable API fallback: false capability plus exact expansion.
 static PortableBackend portable;
 portable.pipeline.packedVertices=portable.pipeline.output;
 portable.pipeline.coverScale=1.0f;portable.pipeline.textureScaleU={1,1};portable.pipeline.textureScaleV={1,1};
 portable.program={};portable.program.numInputs=1;portable.program.alpha=true;portable.program.strideFloats=8;portable.program.inputOffsets[0]=4;
 CHECK(!portable.SupportsCompactVertexStream());
 CompactVertexLayout portableLayout{1,1,0,true,false};
 float portableStorage[3*11];
 for(unsigned v=0;v<3;v++){CompactVertex record{};record.position[3]=1;record.input[0][0]=71;record.input[0][3]=163;WriteCompactVertex(portableStorage,v,record);}
 portable.DrawCompactTriangles(portableStorage,1,portableLayout);
 CHECK(portable.pipeline.count==3);CHECK(portable.pipeline.output[0].color[0]==71);CHECK(portable.pipeline.output[0].color[3]==163);
 // The actual backend guard rejects unknown versions and incompatible layouts before reads.
 for(unsigned bad=0;bad<6;bad++){
  setup(b,3,true);CompactVertexLayout invalid{1,2,1,true,false};
  if(bad==0)invalid.version=2;
  if(bad==1)invalid.numInputs=3;
  if(bad==2)invalid.textureMask=2;
  if(bad==3)invalid.alpha=false;
  if(bad==4)invalid.fog=true;
  if(bad==5)b.backend.program.clamp[0][0]=true;
  b.backend.pipeline.New(b.storage,3,&b.backend.program,&invalid);
  CHECK(b.backend.pipeline.count==0);CHECK(b.backend.pipeline.draws==0);
 }
 // All 256 byte values round-trip exactly and inequality agrees with the old threshold.
 for(unsigned x=0;x<256;x++)for(unsigned y=0;y<256;y++){
  float xf=x*(1.0f/255.0f),yf=y*(1.0f/255.0f);
  CHECK(FloatColorToByte(xf)==x);CHECK((fabsf(xf-yf)>1e-5f)==(x!=y));
 }
 for(unsigned variant=0;variant<128;variant++)for(unsigned shape=0;shape<8;shape++){
  setup(a,variant,false);setup(b,variant,true);b.SelectTriangleEmitter();
  for(unsigned n=0;n<11;n++){
   seeds(n,shape==1 || shape==2);if(shape!=2)live(n);
   a.mClipParameters=b.mClipParameters={bool(shape&4),bool(shape&2)};
   if(shape==3)vertices[n%3].z=-1.75f;
   if(shape==4)for(auto& v:vertices)v.z=-2.0f;
   if(shape==5){vertices[0].z=-2;vertices[1].z=-1.5f;}
   if(shape==6){vertices[0].z=-1.0f;vertices[1].z=-1.0000001f;}
   a.EmitTriangle(va,(shape&1)!=0);b.EmitTriangle(va,(shape&1)!=0);compare();comparePending();
  }
  a.Flush();b.Flush();compare();
  CHECK(!b.mBufVboCompact);
 }
 // IEEE float edge words retain exact position/UV operation order through packing.
 const uint32_t edges[]={0,0x80000000,1,0x80000001,0x007fffff,0x00800000,
  0x7f7fffff,0xff7fffff,0x7f800000,0xff800000,0x7fc12345,0xff812345};
 for(unsigned variant:{0u,3u,15u,31u,127u})for(uint32_t word:edges)for(unsigned component=0;component<6;component++){
  setup(a,variant,false);setup(b,variant,true);b.SelectTriangleEmitter();seeds(1);
  float value;memcpy(&value,&word,4);memcpy(reinterpret_cast<unsigned char*>(vertices)+component*4,&value,4);
  a.EmitTriangle(va,false);b.EmitTriangle(va,false);comparePending();a.Flush();b.Flush();compare();
 }
 // Independent alpha mappings for standard fog, including shade=1, zero and live key colours.
 const unsigned sources[]={0,G_CCMUX_SHADE,G_CCMUX_PRIMITIVE,G_CCMUX_ENVIRONMENT,G_CCMUX_KEY_CENTER,G_CCMUX_KEY_SCALE};
 for(unsigned x:sources)for(unsigned y:sources){
  setup(a,31,false);setup(b,31,true);
  a.comb.shader_input_mapping[1][0]=b.comb.shader_input_mapping[1][0]=x;
  a.comb.shader_input_mapping[1][1]=b.comb.shader_input_mapping[1][1]=y;b.SelectTriangleEmitter();
  for(unsigned n=0;n<7;n++){seeds(n,true);live(n);a.EmitTriangle(va,false);b.EmitTriangle(va,false);comparePending();}
  a.Flush();b.Flush();compare();
 }
 // Compact -> unsupported with the same float layout -> eligible remains one float batch.
 // Changing mapping may change selection without changing the shader/float layout.
 for(unsigned variant:{0u,1u,3u,15u,31u}){
  setup(a,variant,false);setup(b,variant,true);b.SelectTriangleEmitter();
  seeds(0);a.EmitTriangle(va,false);b.EmitTriangle(va,false);comparePending();
  CHECK(b.mBufVboCompact==bool(SOH3DS_COMPACT_VERTEX_STREAM));
  const unsigned old=a.comb.shader_input_mapping[0][0];
  a.comb.shader_input_mapping[0][0]=b.comb.shader_input_mapping[0][0]=G_CCMUX_KEY_CENTER;
  b.SelectTriangleEmitter();seeds(1);live(1);a.EmitTriangle(va,false);b.EmitTriangle(va,false);
  CHECK(!b.mBufVboCompact);CHECK(b.backend.flushes==0);comparePending();
  a.comb.shader_input_mapping[0][0]=b.comb.shader_input_mapping[0][0]=old;b.SelectTriangleEmitter();
  seeds(2);live(2);a.EmitTriangle(va,false);b.EmitTriangle(va,false);CHECK(!b.mBufVboCompact);comparePending();
  a.Flush();b.Flush();compare();
 }
 // The pending layout survives a later derivation before an existing Flush.
 setup(a,0,false);setup(b,0,true);b.SelectTriangleEmitter();seeds(1);
 a.EmitTriangle(va,false);b.EmitTriangle(va,false);
 b.mTriState.use_fog=true;b.mTriState.usedTextures[1]=true;b.mTriState.numInputs=2;
 b.SelectTriangleEmitter();a.Flush();b.Flush();compare();
 // Frame-abort handlers clear counts directly; the next batch must capture its new layout.
 setup(a,0,false);setup(b,0,true);b.SelectTriangleEmitter();seeds(0);
 a.EmitTriangle(va,false);b.EmitTriangle(va,false);
 a.mBufVboLen=a.mBufVboNumTris=b.mBufVboLen=b.mBufVboNumTris=0;
 a.mTriState.usedTextures[0]=b.mTriState.usedTextures[0]=true;
 a.backend.program=layout(a.mTriState);b.backend.program=layout(b.mTriState);b.SelectTriangleEmitter();
 a.EmitTriangle(va,false);b.EmitTriangle(va,false);comparePending();a.Flush();b.Flush();compare();
 // Unavailable backend and initial generic triangles keep the portable float path.
 setup(a,3,false);setup(b,3,true);b.backend.supports=false;b.SelectTriangleEmitter();
 for(unsigned n=0;n<5;n++){seeds(n);a.EmitTriangle(va,false);b.EmitTriangle(va,false);comparePending();}
 a.Flush();b.Flush();compare();CHECK(!b.backend.compactCalls);
 // Capacity crosses exactly at the original triangle count; clipping can double output.
 for(unsigned variant:{0u,3u,15u,31u}){
  setup(a,variant,false);setup(b,variant,true);b.SelectTriangleEmitter();
  for(unsigned n=0;n<MAX_TRI_BUFFER*2+3;n++){
   seeds(n);live(n);vertices[0].z=-2;a.EmitTriangle(va,false);b.EmitTriangle(va,false);compare();
  }
  a.Flush();b.Flush();compare();CHECK(b.backend.flushes==3);
 }
 // Every unsupported static layout is rejected without modifying pending bytes.
 for(unsigned unsupported=0;unsupported<8;unsupported++){
  setup(a,3,false);setup(b,3,true);
  auto reject=[&](auto& i){auto& t=i.mTriState;
   if(unsupported==0)t.use_grayscale=true;
   if(unsupported==1)t.tm=1;
   if(unsupported==2)t.numInputs=3;
   if(unsupported==3)t.use_alpha=false;
   if(unsupported==4)t.usedTextures[1]=true;
   if(unsupported==5)t.use_fog=true,t.use_blend_color=true;
   if(unsupported==6)i.comb.shader_input_mapping[1][0]=G_CCMUX_KEY_CENTER;
   if(unsupported==7)i.comb.shader_input_mapping[0][0]=G_CCMUX_KEY_CENTER;
   i.backend.program=layout(t);
  };reject(a);reject(b);b.SelectTriangleEmitter();
  seeds(0);a.EmitTriangle(va,false);b.EmitTriangle(va,false);comparePending();
  a.Flush();b.Flush();compare();CHECK(!b.backend.compactCalls);
 }
 CHECK(coverage[unsigned(Soh3dsVertexPath::Generic)]>0);
 CHECK(bool(coverage[unsigned(Soh3dsVertexPath::Arm11)])==bool(SOH3DS_ARM11_TRIANGLE_EMIT));
 CHECK(bool(coverage[unsigned(Soh3dsVertexPath::Compact)])==bool(SOH3DS_COMPACT_VERTEX_STREAM));
 CHECK(bool(coverage[unsigned(Soh3dsVertexPath::CompactClip)])==bool(SOH3DS_COMPACT_VERTEX_STREAM));
 CHECK(coverage[unsigned(Soh3dsVertexPath::CompactClip)]<=coverage[unsigned(Soh3dsVertexPath::Compact)]);
}
'''

def timing_setup():
    """Shared deterministic workload construction; no correctness loop in timing TU."""
    return TEST[:TEST.index('static void compare(){')]

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--arm',action='store_true');parser.add_argument('--sanitize',action='store_true')
    parser.add_argument('--output',type=Path);args=parser.parse_args()
    code=harness()+TEST
    with tempfile.TemporaryDirectory(prefix='soh-compact-stream-') as directory:
        p=Path(directory)
        flags=['-std=c++20','-O3','-ffp-contract=off','-fno-fast-math','-I'+str(ROOT/LUS/'include'),'-I'+str(ROOT/'platform/3ds/include')]
        if args.arm:
            runtime=(ROOT/FIXTURES/'compact_stream_arm_runtime.cpp').read_text()
            code+=runtime[runtime.index('extern "C" void _start()'):]
            helpers=runtime[:runtime.index('extern "C" void _start()')]
            # std algorithms can lower overlap-safe copies to memmove.
            helpers+='extern "C" void* memmove(void*d,const void*s,size_t n){auto*x=(unsigned char*)d;auto*y=(const unsigned char*)s;if(x<y)for(size_t i=0;i<n;i++)x[i]=y[i];else while(n--)x[n]=y[n];return d;}\n'
            (p/'runtime.cpp').write_text('#include <stddef.h>\n'+helpers)
            subprocess.run([str(CXX),*FLAGS,'-fno-builtin','-c',str(p/'runtime.cpp'),'-o',str(p/'runtime.o')],check=True)
            flags+=[f for f in FLAGS if not f.startswith(('-O','-std'))]+['-fno-threadsafe-statics']
        else:
            code+='#include <cstdio>\nint main(){correctness();printf("failures=%u first_line=%u checks=%u\\n",failures[0],failures[1],checks);return failures[0]!=0;}\n'
            if args.sanitize:flags+=['-O1','-fsanitize=address,undefined','-fno-omit-frame-pointer']
        (p/'test.cpp').write_text(code)
        if args.output:args.output.mkdir(parents=True,exist_ok=True);(args.output/'harness.cpp').write_text(code)
        for compact in (0,1):
            for arm in ((0,1) if args.arm else (0,)):
                exe=p/f'test-{compact}-{arm}'
                cmd=[str(CXX) if args.arm else 'g++',*flags,f'-DSOH3DS_ARM11_TRIANGLE_EMIT={arm}',f'-DSOH3DS_COMPACT_VERTEX_STREAM={compact}',str(p/'test.cpp')]
                if args.arm:cmd += [str(p/'runtime.o'),str(KERNEL),'-nostdlib','-Wl,-e,_start','-Wl,-Ttext=0x10000','-Wl,-z,noexecstack','-lgcc']
                subprocess.run(cmd+['-o',str(exe)],check=True)
                result=subprocess.run(['qemu-arm','-cpu','arm11mpcore',str(exe)] if args.arm else [str(exe)],stdout=subprocess.PIPE)
                if args.arm:assert result.returncode==0 and result.stdout==bytes(24),(compact,arm,result.stdout.hex())
                else:assert result.returncode==0,result.stdout.decode()
                print(f'PASS compact={compact} arm11={arm} production emission/packing/clipping/constants/transitions/capacity',result.stdout.decode() if not args.arm else '')

if __name__=='__main__': main()
