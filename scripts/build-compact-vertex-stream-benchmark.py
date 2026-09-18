#!/usr/bin/env python3
"""Build a physical Old 3DS synthetic complete emit-to-pack pipeline benchmark.

Control: immutable bd091259 ARM11-enabled wrapper plus original generic pack,
first-input selection and near-plane clipping. Candidate: production cached
selector, compact emission, direct packing and identical fallback. Both retain
real capacity Flush and caller batch boundaries. GPU submission, textures,
display-list interpretation, material derivation and game work are not timed.
No host/QEMU timing is reported as hardware performance or FPS.
"""
from pathlib import Path
import hashlib
import json
import subprocess
import re
import sys
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tests'))
import compact_vertex_stream_test as test
OUT=ROOT/'builds/compact-vertex-stream-benchmark'
OUT.mkdir(parents=True,exist_ok=True)
# Separate translation units keep all correctness counters and checks out of the
# timed code at compile time. Namespace the validation copy to avoid ODR/linkage
# collisions with its uninstrumented timing counterpart.
validation=test.harness()
includes='\n'.join(dict.fromkeys(re.findall(r'^#include .+$',validation,re.M)))
validation=re.sub(r'^#include .+\n','',validation,flags=re.M)
correctness_code=includes+'\nnamespace Validation {\n'+validation+test.TEST+'\n}\n'+'''
extern "C" void CompactStreamCorrectness(uint32_t* results) {
 Validation::correctness();
 for(unsigned n=0;n<6;n++)results[n]=Validation::failures[n];
 results[6]=Validation::checks;
 for(unsigned n=0;n<4;n++)results[7+n]=Validation::coverage[n];
}
'''
code='#include <3ds.h>\n#include <cstdio>\n'+test.harness(instrumented=False)+test.timing_setup()+'''
// Benchmark checks run before/after each timed call, never inside it.
extern "C" void CompactStreamCorrectness(uint32_t*);
static uint32_t failures[6];
#undef CHECK
#define CHECK(x) do { if (!(x)) { ++failures[0]; if (!failures[1]) failures[1]=__LINE__; } } while(0)
'''
code+=r'''
static volatile uint32_t checksum;
static uint32_t digest(const Pipeline& p){
 const auto* bytes=reinterpret_cast<const uint8_t*>(p.output);uint32_t hash=2166136261u;
 for(size_t n=0;n<p.count*sizeof(PackedVertex);n++)hash=(hash^bytes[n])*16777619u;
 for(const auto& input:p.inputConstants)for(float value:input){uint32_t word;memcpy(&word,&value,4);hash=(hash^word)*16777619u;}
 return hash^uint32_t(p.varying)^uint32_t(p.count);
}
template<class I> __attribute__((noinline)) static uint64_t timed(I& i,unsigned triangles,unsigned batches,bool dynamic,bool rect){
 uint64_t begin=svcGetSystemTick();
 for(unsigned batch=0;batch<batches;batch++){
  for(unsigned triangle=0;triangle<triangles;triangle++){
   if(dynamic){
    i.rdp.prim_color={uint8_t(triangle*3),uint8_t(triangle*11),uint8_t(triangle*23),uint8_t(triangle*37)};
    i.rdp.env_color={uint8_t(triangle*5),uint8_t(triangle*13),uint8_t(triangle*29),uint8_t(triangle*41)};
    i.rdp.key_center.a=uint8_t(triangle*19);i.rdp.key_scale.a=uint8_t(triangle*47);
   }
   i.EmitTriangle(va,rect);
  }
  i.Flush();
  asm volatile(""::"r"(i.backend.pipeline.output):"memory");
 }
 return svcGetSystemTick()-begin;
}
static void benchmark(FILE* f){
 struct Workload{const char* name;unsigned variant;bool uniformShade,dynamic,clip,unsupported,rect;};
 const Workload workloads[]={
  {"shade_one",0,false,false,false,false,false},
  {"texture_shade_primitive",3,false,false,false,false,false},
  {"texture_constant_shade_dynamic_uniform",7,true,true,false,false,false},
  {"fog_opaque_dynamic",15,false,true,false,false,false},
  {"fog_alpha_independent",31,false,true,false,false,false},
  {"fog_rotated_framebuffers_rect",127,false,true,false,false,true},
  {"fog_near_clip",31,false,true,true,false,false},
  {"generic_grayscale",3,false,true,false,true,false},
 };
 for(const auto& w:workloads)for(unsigned triangles:{1u,8u,64u,256u}){
  setup(a,w.variant,false);setup(b,w.variant,true);seeds(1,w.uniformShade);
  if(w.variant&16){
   a.comb.shader_input_mapping[1][0]=b.comb.shader_input_mapping[1][0]=G_CCMUX_PRIMITIVE;
   a.comb.shader_input_mapping[1][1]=b.comb.shader_input_mapping[1][1]=G_CCMUX_KEY_CENTER;
  }
  if(w.unsupported){a.mTriState.use_grayscale=b.mTriState.use_grayscale=true;
   a.backend.program=layout(a.mTriState);b.backend.program=layout(b.mTriState);}
  if(w.clip)vertices[0].z=-2.0f;
  b.SelectTriangleEmitter();
  const unsigned batches=8192/triangles;
  timed(a,triangles,32,w.dynamic,w.rect);timed(b,triangles,32,w.dynamic,w.rect);
  CHECK(digest(a.backend.pipeline)==digest(b.backend.pipeline));
  if(f)fprintf(f,"# workload=%s triangles_per_batch=%u batches=%u capacity=%u selection=once_before_timing\n",w.name,triangles,batches,MAX_TRI_BUFFER);
  for(unsigned round=0;round<7;round++){
   uint64_t oldTicks,newTicks;
   if(round&1){newTicks=timed(b,triangles,batches,w.dynamic,w.rect);oldTicks=timed(a,triangles,batches,w.dynamic,w.rect);}
   else{oldTicks=timed(a,triangles,batches,w.dynamic,w.rect);newTicks=timed(b,triangles,batches,w.dynamic,w.rect);}
   const uint32_t oldSum=digest(a.backend.pipeline),newSum=digest(b.backend.pipeline);
   checksum=checksum^oldSum^newSum;CHECK(oldSum==newSum);
   if(f)fprintf(f,"%s,%u,%u,%s,%llu,%llu,%u,%u\n",w.name,triangles,round,round&1?"new-first":"old-first",
    (unsigned long long)oldTicks,(unsigned long long)newTicks,oldSum,newSum);
  }
  printf("%s batch=%u done\n",w.name,triangles);
 }
}
int main(){
 gfxInitDefault();consoleInit(GFX_TOP,nullptr);
 bool newModel=false;APT_CheckNew3DS(&newModel);osSetSpeedupEnable(newModel);
 printf("Compact vertex complete pipeline benchmark\nCorrectness running...\n");
 uint32_t validation[11];CompactStreamCorrectness(validation);
 memcpy(failures,validation,6*sizeof(uint32_t));checks=validation[6];
 printf("Correctness failures: %lu first line: %lu\n",(unsigned long)failures[0],(unsigned long)failures[1]);
 FILE* f=fopen("sdmc:/3ds/soh/compact-vertex-stream-benchmark.csv","w");
 if(f){fprintf(f,"# synthetic emit+capacity_flush+near_clip+first_input_select+pack; GPU submission/material derivation/game work excluded; not FPS\n");
  fprintf(f,"# control=bd091259_complete_ARM11_wrapper_plus_original_generic_pack model=%s failures=%lu checks=%lu\n",
    newModel?"new":"old",(unsigned long)failures[0],(unsigned long)checks);
  fprintf(f,"# fixture_instrumentation=untimed_correctness_only coverage_generic=%lu arm11=%lu compact=%lu compact_clip=%lu\n",
   (unsigned long)validation[7],(unsigned long)validation[8],(unsigned long)validation[9],(unsigned long)validation[10]);
  fprintf(f,"workload,triangles_per_batch,round,order,old_ticks,new_ticks,old_checksum,new_checksum\n");}
 if(!failures[0])benchmark(f);
 if(f){fprintf(f,"# final_failures=%lu\n",(unsigned long)failures[0]);fclose(f);}
 printf("Finished. Failures: %lu\nCSV %s\nSTART exits.\n",(unsigned long)failures[0],f?"saved to sdmc:/3ds/soh":"open failed");
 while(aptMainLoop()){hidScanInput();if(hidKeysDown()&KEY_START)break;gfxFlushBuffers();gfxSwapBuffers();gspWaitForVBlank();}
 gfxExit();return failures[0]!=0;
}
'''
cpp=OUT/'benchmark.cpp';cpp.write_text(code)
correctness_cpp=OUT/'correctness.cpp';correctness_cpp.write_text(correctness_code)
dkp=test.DKP;elf=OUT/'soh-compact-vertex-stream-benchmark.elf'
flags=test.FLAGS+['-mtp=soft','-mword-relocations']
compile_flags=[*flags,'-D__3DS__','-DARM11','-DSOH3DS_ARM11_TRIANGLE_EMIT=1','-DSOH3DS_COMPACT_VERTEX_STREAM=1',
 '-I'+str(dkp/'libctru/include'),'-I'+str(ROOT/'third_party/libultraship/include'),'-I'+str(ROOT/'platform/3ds/include')]
objects=[]
for source in (cpp,correctness_cpp):
 obj=source.with_suffix('.o');objects.append(obj)
 subprocess.run([str(test.CXX),*compile_flags,'-c',str(source),'-o',str(obj)],check=True)
subprocess.run([str(test.CXX),*flags,'-Wl,-z,noexecstack',
 '-specs='+str(dkp/'devkitARM/arm-none-eabi/lib/3dsx.specs'),*[str(p) for p in objects],str(test.KERNEL),
 '-L'+str(dkp/'libctru/lib'),'-lctru','-lm','-o',str(elf)],check=True)
# Preserve both machine code and compiler-expanded timing source. These checks
# fail if instrumentation accidentally reaches the timing translation unit.
preprocessed=subprocess.check_output([str(test.CXX),*compile_flags,'-E','-P',str(cpp)],text=True)
(OUT/'benchmark.preprocessed.cpp').write_text(preprocessed)
assert 'coverage[' not in preprocessed
assert not re.search(r'\+\+\s*(?:flushes|compactCalls|draws)\b',preprocessed)
assert 'l==t*3*program.strideFloats' not in preprocessed
assert 'pipeline.candidate' not in preprocessed
assert 'failures[' not in test.block(preprocessed,'struct FixtureBackend {')
for signature in ('void Pipeline::Old(', 'void Pipeline::New('):
 body=test.block(preprocessed,signature)
 assert 'failures[' not in body and '++draws' not in body,signature
 assert 'mImpl->packedVertexCount + vertexCount > kVertexBufferCapacity' in body,signature
symbols=subprocess.check_output([str(dkp/'devkitARM/bin/arm-none-eabi-nm'),'-C',str(objects[0])],text=True)
assert 'coverage' not in symbols and 'Soh3dsProfileVertexBatch' not in symbols
(OUT/'benchmark.symbols.txt').write_text(symbols)
disassembly=subprocess.check_output([str(dkp/'devkitARM/bin/arm-none-eabi-objdump'),'-drC',str(objects[0])],text=True)
(OUT/'benchmark.disasm.txt').write_text(disassembly)
assert 'timed<Original::Interpreter>' in disassembly and 'timed<Candidate::Interpreter>' in disassembly
validation_symbols=subprocess.check_output([str(dkp/'devkitARM/bin/arm-none-eabi-nm'),'-C',str(objects[1])],text=True)
assert 'Validation::coverage' in validation_symbols
(OUT/'correctness.symbols.txt').write_text(validation_symbols)
timing_symbols=[line for line in symbols.splitlines() if any(name in line for name in ('timed<','Interpreter::EmitTriangle','Interpreter::Flush','Pipeline::Old','Pipeline::New'))]
(OUT/'timing-audit.json').write_text(json.dumps({
 'coverage_symbol_absent_from_timing_object':True,
 'coverage_symbol_retained_in_correctness_object':True,
 'fixture_counter_updates_absent_from_timing_preprocessed_source':True,
 'fixture_assertion_operands_absent_from_timing_pipeline':True,
 'fixture_backend_selection':'compile-time specialization',
 'production_capacity_guards_retained':True,
 'timed_symbols':timing_symbols,
},indent=2)+'\n')
three=OUT/'soh-compact-vertex-stream-benchmark.3dsx'
subprocess.run([str(dkp/'tools/bin/3dsxtool'),str(elf),str(three)],check=True)
files=[cpp,correctness_cpp,*objects,elf,three,test.KERNEL,ROOT/test.SOURCE,ROOT/test.BACKEND,ROOT/test.HEADER,
 ROOT/test.LUS/'include/fast/interpreter.h',ROOT/test.LUS/'include/fast/backends/gfx_rendering_api.h',
 ROOT/test.LUS/'include/fast/triangle_emit_3ds.h',ROOT/'platform/3ds/include/gfx_citro3d.h',
 ROOT/'platform/3ds/include/compiled_channel_3ds.h',Path(test.__file__),Path(__file__).resolve(),
 *[ROOT/p for p in test.BASELINE_HASHES],OUT/'benchmark.preprocessed.cpp',OUT/'benchmark.disasm.txt',
 OUT/'benchmark.symbols.txt',OUT/'correctness.symbols.txt',OUT/'timing-audit.json']
manifest={'description':__doc__,'compiler_flags':flags,'baseline_sha256':{str(p):h for p,h in test.BASELINE_HASHES.items()},
 'timing_instrumentation':'compile-time omitted in benchmark.o; correctness.cpp retains independent coverage/checks',
 'sha256':{str(p.relative_to(ROOT)):hashlib.sha256(p.read_bytes()).hexdigest() for p in files}}
(OUT/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
print('PASS timing instrumentation audit: no coverage/call/draw counters or fixture assertions in timed pipeline; correctness counters retained separately')
print(three)
