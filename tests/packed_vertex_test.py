#!/usr/bin/env python3
"""Byte-equivalence of production vertex packing, including dispatch and clipping.

The oracle is the frozen, hashed pre-experiment production loop, not a second
implementation of the layout rules. This catches incorrect offsets, color
conversion, fog alpha, UV scaling, unsafe admission and destination overruns.
Use --sanitize for ASan/UBSan, or --benchmark for a separate uninstrumented run.
"""
import argparse
import hashlib
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
EVIDENCE = ROOT / 'docs/evidence/fps60-optimization-2026-09-05/packed-vertex'
BACKEND = Path('platform/3ds/source/gfx_citro3d.cpp')
BASELINE_HASH = '55548608389edb9c3568dff6827b86c7053d3e234368890204b6bace9ff5d8a3'


def block(source, signature, start=0):
    start = source.index(signature, start)
    opening = source.index('{', start)
    depth, end = 1, opening + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]


def harness():
    source = (ROOT / BACKEND).read_text()
    baseline_bytes = (EVIDENCE / 'before' / BACKEND).read_bytes()
    assert hashlib.sha256(baseline_bytes).hexdigest() == BASELINE_HASH, 'Baseline fixture changed'
    baseline = baseline_bytes.decode()
    assert 'bool TryPackCommonVertices(' in source, 'Common packing dispatcher is not implemented'
    packed_vertex = block(source, 'struct PackedVertex {') + ';'
    shader = block((ROOT / 'platform/3ds/include/gfx_citro3d.h').read_text(), 'struct ShaderProgram {') + ';'
    helpers = source[source.index('#ifndef SOH3DS_EXPERIMENT_COMMON_PACK'):
                     source.index('constexpr uint32_t kDisplayTransferFlags')]
    original_loop = block(baseline, 'for (size_t vertex = 0; vertex < vertexCount; ++vertex)',
                          baseline.index('Soh3dsProfileScope packProfile'))
    original_loop = original_loop.replace('mImpl->packedVertices[firstVertex + vertex]',
                                           'packedVertices[vertex]')
    original_loop = original_loop.replace('FloatColorToByte(', 'BaselineFloatColorToByte(')
    original_color = block(baseline, 'uint8_t FloatColorToByte(').replace('FloatColorToByte', 'BaselineFloatColorToByte')
    clip = block(source, 'size_t ClipTriangleAgainstW(')
    original_clip = block(baseline, 'size_t ClipTriangleAgainstW(').replace('ClipTriangleAgainstW', 'BaselineClip')
    return r'''
#include <algorithm>
#include <array>
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <random>
#include <vector>
''' + shader + packed_vertex + block(source, 'uint8_t FloatColorToByte(') + '\n' + helpers + original_color + r'''
using Scales = std::array<float, 2>;
using Rotations = std::array<bool, 2>;
#define PACK_ARGUMENTS const float* drawVertices, PackedVertex* packedVertices, size_t vertexCount, \
 const ShaderProgram* program, int varyingInput, float coverScale, const Scales& textureScaleU, \
 const Scales& textureScaleV, const Scales& textureOffsetV, const Rotations& rotatedFramebuffer
#define PACK_VALUES drawVertices, packedVertices, vertexCount, program, varyingInput, coverScale, \
 textureScaleU, textureScaleV, textureOffsetV, rotatedFramebuffer
__attribute__((noinline)) void BaselinePack(PACK_ARGUMENTS) {
''' + original_loop + r'''
}
__attribute__((noinline)) void CandidatePack(PACK_ARGUMENTS) { PackVertices(PACK_VALUES); }
constexpr size_t kMaxVertexStrideFloats = 64;
''' + clip + original_clip + r'''
std::mt19937 randomEngine(0x35c1703dU);
size_t comparedVertices = 0, comparedBatches = 0, admittedBatches = 0, rejectedBatches = 0;

// Build valid parser layouts, independently of either packing loop.
ShaderProgram Layout(unsigned mask, bool alpha, bool fog, unsigned inputs=1,
                     bool grayscale=false, unsigned clamps=0) {
 ShaderProgram p;
 p.alpha=alpha;p.fog=fog;p.grayscale=grayscale;p.numInputs=inputs;
 unsigned offset=4;
 for(unsigned unit=0;unit<2;++unit) {
  p.usedTextures[unit]=(mask&(1U<<unit))!=0;
  p.clamp[unit][0]=(clamps&(1U<<(unit*2)))!=0;
  p.clamp[unit][1]=(clamps&(2U<<(unit*2)))!=0;
  if(p.usedTextures[unit]) {
   p.textureOffsets[unit]=offset;offset+=2;
   offset+=p.clamp[unit][0]+p.clamp[unit][1];
  }
 }
 if(fog){p.fogOffset=offset;offset+=4;}
 if(grayscale){p.grayscaleOffset=offset;offset+=4;}
 for(unsigned input=0;input<inputs;++input){p.inputOffsets[input]=offset;offset+=alpha?4:3;}
 p.strideFloats=offset;
 return p;
}

std::vector<float> Generated(const ShaderProgram& p,size_t count) {
 std::vector<float> fields(count*p.strideFloats);
 for(float& value:fields)value=(static_cast<int>(randomEngine()%20001)-10000)/4096.0f;
 for(size_t vertex=0;vertex<count;++vertex) {
  float* s=fields.data()+vertex*p.strideFloats;
  s[2]=0;s[3]=1;
 }
 return fields;
}

void Compare(const ShaderProgram& p,const std::vector<float>& fields,bool expectedCommon,
             int varyingInput=0,float coverScale=1.0f,Scales scaleU={1,1},Scales scaleV={1,1},
             Scales offsetV={0,0},Rotations rotation={false,false}) {
 const size_t count=fields.size()/p.strideFloats;
 // Prefix and suffix records detect stray stores and nonzero destination offsets.
 std::vector<PackedVertex> baseline(count+5),candidate(count+5),generic(count+5),attempt(count+5);
 std::memset(baseline.data(),0xa5,baseline.size()*sizeof(PackedVertex));
 candidate=generic=attempt=baseline;
 BaselinePack(fields.data(),baseline.data()+2,count,&p,varyingInput,coverScale,scaleU,scaleV,offsetV,rotation);
 const bool selected=PackVertices(fields.data(),candidate.data()+2,count,&p,varyingInput,coverScale,scaleU,scaleV,offsetV,rotation);
 assert(selected==(SOH3DS_EXPERIMENT_COMMON_PACK && expectedCommon));
 PackGenericVertices(fields.data(),generic.data()+2,count,&p,varyingInput,coverScale,scaleU,scaleV,offsetV,rotation);
 const bool common=TryPackCommonVertices(fields.data(),attempt.data()+2,count,&p,varyingInput,coverScale,
                                       scaleU,scaleV,rotation);
 assert(common==expectedCommon);
 assert(std::memcmp(baseline.data(),candidate.data(),baseline.size()*sizeof(PackedVertex))==0);
 assert(std::memcmp(baseline.data(),generic.data(),baseline.size()*sizeof(PackedVertex))==0);
 if(common) {
  assert(std::memcmp(baseline.data(),attempt.data(),baseline.size()*sizeof(PackedVertex))==0);
  ++admittedBatches;
 } else {
  const auto* bytes=reinterpret_cast<const unsigned char*>(attempt.data());
  for(size_t byte=0;byte<attempt.size()*sizeof(PackedVertex);++byte)assert(bytes[byte]==0xa5);
  ++rejectedBatches;
 }
 comparedVertices+=count;++comparedBatches;
}

void TestAdmitted() {
 std::vector<float> colors={-std::numeric_limits<float>::infinity(),-1.0f,-0.0f,0.0f,1.0f,2.0f,
                           std::numeric_limits<float>::infinity(),std::numeric_limits<float>::quiet_NaN()};
 for(unsigned byte=0;byte<256;++byte) {
  colors.push_back(byte*(1.0f/255.0f));
  const float midpoint=(byte+0.5f)/255.0f;
  colors.push_back(std::nextafter(midpoint,-std::numeric_limits<float>::infinity()));
  colors.push_back(midpoint);
  colors.push_back(std::nextafter(midpoint,std::numeric_limits<float>::infinity()));
 }
 for(unsigned mask:{1U,3U})for(bool alpha:{false,true})for(bool fog:{false,true}) {
  auto p=Layout(mask,alpha,fog);
  auto fields=Generated(p,colors.size());
  for(size_t vertex=0;vertex<colors.size();++vertex) {
   float* s=fields.data()+vertex*p.strideFloats;
   // Every component sees every boundary value; alpha differs from fog.
   for(unsigned channel=0;channel<(alpha?4U:3U);++channel)
    s[p.inputOffsets[0]+channel]=colors[(vertex+channel*37)%colors.size()];
   if(fog)s[p.fogOffset+3]=colors[(vertex+131)%colors.size()];
  }
  Compare(p,fields,true);
  Compare(p,fields,true,0,1.25f,{400.0f/512,13.0f/16},{240.0f/256,7.0f/8});
  Compare(p,fields,true,0,1.0f,{0.0f,-2.25f},{-0.0f,0.03125f},{0.125f,0.375f});
  Compare(p,{},true);
  for(size_t count:{1U,3U,6U,63U,768U,1536U})Compare(p,Generated(p,count),true);
  if(mask==1)Compare(p,fields,true,0,1,{1,1},{1,1},{0,0},{false,true});
 }
}

void TestFallback() {
 for(unsigned mask=0;mask<4;++mask)for(bool alpha:{false,true})for(bool fog:{false,true}) {
  for(unsigned inputs:{0U,2U,3U,7U}) {
   auto p=Layout(mask,alpha,fog,inputs);
   for(int varying=-1;varying<static_cast<int>(inputs);++varying)
    Compare(p,Generated(p,19),false,varying,1.25f,{0.7f,0.8f},{0.3f,0.9f});
  }
  auto p=Layout(mask,alpha,fog);
  if(mask!=1 && mask!=3)Compare(p,Generated(p,33),false);
  Compare(p,Generated(p,33),false,-1);
  p=Layout(mask,alpha,fog,1,true);Compare(p,Generated(p,33),false);
  for(unsigned clamp=1;clamp<16;++clamp) {
   p=Layout(mask,alpha,fog,1,false,clamp);
   Compare(p,Generated(p,33),false,0,1.25f,{0.4f,0.8f},{0.9f,0.3f},{0.125f,0.375f},{true,true});
   Compare(p,Generated(p,33),false);
  }
  for(unsigned unit=0;unit<2;++unit)if(mask&(1U<<unit)) {
   p=Layout(mask,alpha,fog);Rotations rotation={false,false};rotation[unit]=true;
   Compare(p,Generated(p,33),false,0,1.25f,{240.0f/256,13.0f/16},{400.0f/512,7.0f/8},
           {112.0f/512,0.375f},rotation);
  }
 }
 // A malformed layout must not take a hard-coded offset/stride path.
 for(unsigned mutation=0;mutation<5;++mutation) {
  auto p=Layout(3,true,true);
  if(mutation==0)++p.strideFloats;
  if(mutation==1)--p.textureOffsets[0];
  if(mutation==2)--p.textureOffsets[1];
  if(mutation==3)--p.inputOffsets[0];
  if(mutation==4)--p.fogOffset;
  Compare(p,Generated(p,33),false);
 }
}

void TestClipped() {
 for(unsigned mask:{1U,3U})for(bool alpha:{false,true})for(bool fog:{false,true})
 for(unsigned insideMask=0;insideMask<8;++insideMask)for(unsigned distanceCase=0;distanceCase<5;++distanceCase) {
  auto p=Layout(mask,alpha,fog);
  auto fields=Generated(p,3);
  for(unsigned vertex=0;vertex<3;++vertex) {
   float* s=fields.data()+vertex*p.strideFloats;
   const float w=distanceCase==4?1000000.0f:1.0f;
   const float distance=distanceCase==0?0.25f:distanceCase==1?0.0f:
                        distanceCase==2?std::ldexp(1.0f,-22):distanceCase==3?4.0f:100.0f;
   s[3]=w;s[2]=-w+((insideMask&(1U<<vertex))?distance:-distance);
  }
  std::vector<float> baseline(p.strideFloats*6),clipped(p.strideFloats*6);
  const float* triangle[]={fields.data(),fields.data()+p.strideFloats,fields.data()+p.strideFloats*2};
  const auto baselineCount=BaselineClip(triangle,p.strideFloats,baseline.data());
  const auto count=ClipTriangleAgainstW(triangle,p.strideFloats,clipped.data());
  assert(count==baselineCount);
  assert(std::memcmp(baseline.data(),clipped.data(),count*p.strideFloats*sizeof(float))==0);
  clipped.resize(count*p.strideFloats);
  Compare(p,clipped,true,0,1.25f,{400.0f/512,13.0f/16},{240.0f/256,7.0f/8});
  // The shared pack path sees a mixed batch of post-clipping vertices.
  auto uncut=Generated(p,3);clipped.insert(clipped.end(),uncut.begin(),uncut.end());
  Compare(p,clipped,true);
 }
}

using Packer=void (*)(PACK_ARGUMENTS);
struct Batch {ShaderProgram p;std::vector<float> fields;int varying=0;Rotations rotation={false,false};};
volatile uint64_t benchmarkSink=0;

std::vector<float> BenchmarkFields(const ShaderProgram& p,size_t count) {
 auto fields=Generated(p,count);
 for(size_t vertex=0;vertex<count;++vertex) {
  float* s=fields.data()+vertex*p.strideFloats;
  for(unsigned input=0;input<p.numInputs;++input)
   for(unsigned channel=0;channel<(p.alpha?4U:3U);++channel)
    s[p.inputOffsets[input]+channel]=(randomEngine()%256)*(1.0f/255.0f);
  if(p.fog)for(unsigned channel=0;channel<4;++channel)
   s[p.fogOffset+channel]=(randomEngine()%256)*(1.0f/255.0f);
 }
 return fields;
}

__attribute__((noinline)) double TimePacking(Packer packer,const std::vector<Batch>& batches,unsigned loops) {
 std::vector<PackedVertex> output(768);
 const auto begin=std::chrono::steady_clock::now();
 for(unsigned loop=0;loop<loops;++loop)for(const auto& batch:batches) {
  packer(batch.fields.data(),output.data(),batch.fields.size()/batch.p.strideFloats,&batch.p,batch.varying,
         1.25f,{400.0f/512,13.0f/16},{240.0f/256,7.0f/8},{0.125f,0.375f},batch.rotation);
  // All stores must remain observable, without timing a whole-buffer checksum.
  asm volatile("" : : "g"(output.data()) : "memory");
 }
 const auto end=std::chrono::steady_clock::now();
 for(const auto& vertex:output)benchmarkSink+=vertex.color[0];
 return std::chrono::duration<double,std::nano>(end-begin).count();
}

void Benchmark() {
 for(size_t count:{3U,24U,192U,768U})for(unsigned workload=0;workload<3;++workload) {
  std::vector<Batch> batches;
  for(unsigned mask:{1U,3U})for(bool alpha:{false,true})for(bool fog:{false,true}) {
   Batch batch;batch.p=Layout(mask,alpha,fog);batch.fields=BenchmarkFields(batch.p,count);batches.push_back(batch);
   if(workload>0) {
    batch.p=Layout(3,true,true,2,true,15);batch.fields=BenchmarkFields(batch.p,count);batch.rotation={true,true};
    batches.push_back(batch);
   }
  }
  if(workload==2)batches.erase(std::remove_if(batches.begin(),batches.end(),
                                           [](const Batch& batch){return batch.p.numInputs==1;}),batches.end());
  const unsigned loops=std::max(300U,static_cast<unsigned>(1200000/(count*batches.size())));
  (void)TimePacking(BaselinePack,batches,50);(void)TimePacking(CandidatePack,batches,50);
  for(unsigned round=0;round<9;++round) {
   double baseline,candidate;
   if(round%2==0){baseline=TimePacking(BaselinePack,batches,loops);candidate=TimePacking(CandidatePack,batches,loops);}
   else {candidate=TimePacking(CandidatePack,batches,loops);baseline=TimePacking(BaselinePack,batches,loops);}
   const double vertices=static_cast<double>(count)*batches.size()*loops;
   std::printf("%zu,%u,%u,%.4f,%.4f\n",count,workload,round,baseline/vertices,candidate/vertices);
  }
 }
}

int main(int argc,char**) {
 if(argc>1){std::puts("vertices_per_batch,workload,round,baseline_ns_per_vertex,candidate_ns_per_vertex");Benchmark();return 0;}
 TestAdmitted();TestFallback();TestClipped();
 std::printf("packed vertex: %zu complete 36-byte vertices across %zu batches; %zu admitted, %zu fallback; clipping and guards pass (switch=%d)\n",
             comparedVertices,comparedBatches,admittedBatches,rejectedBatches,SOH3DS_EXPERIMENT_COMMON_PACK);
}
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--sanitize', action='store_true')
    parser.add_argument('--benchmark', action='store_true')
    parser.add_argument('--keep', type=Path, help='Keep generated source and executables here')
    args = parser.parse_args()
    assert not (args.sanitize and args.benchmark), 'Timing and sanitizer builds must be separate'
    cpp = harness()
    with tempfile.TemporaryDirectory(prefix='soh-packed-vertex-') as directory:
        directory = args.keep or Path(directory)
        directory.mkdir(parents=True, exist_ok=True)
        source = directory / 'test.cpp'
        source.write_text(cpp)
        for enabled in ([1] if args.benchmark else [None, 0, 1]):
            executable = directory / ('test-' + str(enabled))
            flags = ['-std=c++17', '-O2', '-Wall', '-Wextra', '-fno-fast-math', '-ffp-contract=off']
            if enabled is None:
                # Check the shipped default, as well as both explicit A/B modes.
                source.write_text(cpp + '\nstatic_assert(SOH3DS_EXPERIMENT_COMMON_PACK == 0, "Vertex optimization must remain off by default");\n')
            else:
                source.write_text(cpp)
                flags += ['-DSOH3DS_EXPERIMENT_COMMON_PACK=' + str(enabled)]
            if args.sanitize:
                flags += ['-fsanitize=address,undefined', '-fno-omit-frame-pointer', '-g']
            subprocess.run([os.environ.get('CXX', 'g++'), *flags, str(source), '-o', str(executable)], check=True)
            subprocess.run([str(executable), *(['benchmark'] if args.benchmark else [])], check=True)


if __name__ == '__main__':
    main()
