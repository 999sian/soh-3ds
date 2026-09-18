#!/usr/bin/env python3
"""Build and ARM11-verify a native benchmark of the production vertex loader.

Uses the real Interpreter/RSP declarations and extracted production helpers.
This measures controlled loader workloads, not game frame time.
"""
import hashlib
import importlib.util
import json
from pathlib import Path
import re
import struct
import subprocess

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('vertex', ROOT / 'tests/arm11_vertex_test.py')
vertex = importlib.util.module_from_spec(spec)
spec.loader.exec_module(vertex)
OUT = ROOT / 'builds/arm11-vertex-loader-benchmark'
SOURCE = ROOT / 'third_party/libultraship/src/fast/interpreter.cpp'


def function(source, signature):
    start = source.index(signature)
    opening = source.index('{', start)
    depth = 1
    end = opening + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end] + '\n'


PREFIX = r'''
#define _LANGUAGE_C
#include <set>
#include <cstring>
#include <cmath>
#include <algorithm>
#include <limits>
#include "fast/interpreter.h"
#include "fast/vertex_transform_3ds.h"
#include "ship/utils/Utils.h"
#include "fast/backends/gfx_profile_3ds.h"
extern "C" void VertexDisjointReference(const void*,void*,const float[4][4],uint32_t);
'''

DRIVER = r'''
using namespace Fast;
using Loader = void (*)(Interpreter*,size_t,size_t,const F3DVtx*);
extern "C" void LoaderC(Interpreter*,size_t,size_t,const F3DVtx*);
extern "C" void LoaderAsm(Interpreter*,size_t,size_t,const F3DVtx*);
extern "C" void LoaderAsm16(Interpreter*,size_t,size_t,const F3DVtx*);
extern "C" void LoaderC16(Interpreter*,size_t,size_t,const F3DVtx*);
static const Loader paths[]={LoaderC,LoaderAsm,LoaderAsm16,LoaderC16};
static const unsigned counts[]={1,2,4,8,15,16,17,32,64,68};
static const unsigned modes[]={0,G_FOG,G_LIGHTING,G_LIGHTING|G_FOG,
    G_LIGHTING|G_TEXTURE_GEN,G_LIGHTING|G_TEXTURE_GEN|G_TEXTURE_GEN_LINEAR,
    G_LIGHTING|G_LIGHTING_POSITIONAL|G_FOG};
static uint32_t rng=0x3d511,failures,checks;
static Interpreter *baseline,*candidate;
static RSP seed;
static F3DVtx vertices[72];
static uint32_t randomWord(){rng=rng*1664525u+1013904223u;return rng;}
static void fill(void* dst,size_t size){auto* p=(uint8_t*)dst;while(size--)*p++=randomWord()>>24;}
static void check(bool ok){++checks;if(!ok)++failures;}
static uint32_t fpscr(){uint32_t v;__asm__ volatile("vmrs %0,fpscr":"=r"(v));return v;}
static void makeSeed(unsigned mode) {
    seed={};seed.modelview_matrix_stack_size=1;seed.geometry_mode=mode;
    seed.current_num_lights=3; // Two active lights plus ambient.
    seed.texture_scaling_factor={0xffff,0x8000};seed.fog_mul=100;seed.fog_offset=32;
    for(unsigned i=0;i<16;++i) {
        ((float*)seed.MP_matrix)[i]=((int)(randomWord()%128)-64)/32.0f;
        ((float*)seed.P_matrix)[i]=((int)(randomWord()%128)-64)/32.0f;
        ((float*)seed.modelview_matrix_stack[0])[i]=(i%5==0)?1.0f:0.0f;
    }
    for(unsigned i=0;i<3;++i) {
        auto& light=seed.current_lights[i];
        light.l.col[0]=40+i*20;light.l.col[1]=60+i*10;light.l.col[2]=80+i*15;
        light.l.dir[0]=31;light.l.dir[1]=63;light.l.dir[2]=95;
        if(mode&G_LIGHTING_POSITIONAL) {
            light.p.pos[0]=10001+i*1000;light.p.pos[1]=11003;light.p.pos[2]=12007;
            light.p.unk3=light.p.unk7=light.p.unkE=1;
        }
        for(unsigned j=0;j<3;++j)seed.current_lights_coeffs[i][j]=(j+1)*0.125f;
    }
    for(unsigned i=0;i<2;++i) {
        seed.lookat[i].dir[0]=31;seed.lookat[i].dir[1]=63;seed.lookat[i].dir[2]=95;
        for(unsigned j=0;j<3;++j)seed.current_lookat_coeffs[i][j]=(j+1)*0.125f;
    }
    fill(seed.loaded_vertices,sizeof(seed.loaded_vertices));fill(vertices,sizeof(vertices));
}
static void reset(Interpreter* ctx,bool dirty=false,bool aspectMiss=false,bool fixed=false) {
    *ctx->mRsp=seed;ctx->mCombinedMatrixDirty=dirty;
    ctx->mCurDimensions.width=400;ctx->mCurDimensions.height=240;
    ctx->mAspectAdjustWidth=aspectMiss?0:400;ctx->mAspectAdjustHeight=240;
    ctx->mAspectAdjustMultiplier=0.8f;ctx->mFbActive=fixed;
}
static bool equal() {
    return memcmp(baseline->mRsp,candidate->mRsp,sizeof(RSP))==0 &&
        baseline->mCombinedMatrixDirty==candidate->mCombinedMatrixDirty &&
        baseline->mAspectAdjustWidth==candidate->mAspectAdjustWidth &&
        baseline->mAspectAdjustHeight==candidate->mAspectAdjustHeight &&
        memcmp(&baseline->mAspectAdjustMultiplier,&candidate->mAspectAdjustMultiplier,4)==0;
}
static void init() {
    baseline=new Interpreter;candidate=new Interpreter;
    for(auto* ctx:{baseline,candidate}) {
        ctx->mFrameBuffers.emplace(1,FBInfo{});
        ctx->mActiveFrameBuffer=ctx->mFrameBuffers.find(1);
        ctx->mActiveFrameBuffer->second.forceFixedAspect=true;
    }
}
static void correctness() {
    // Do not import QEMU's exhaustive floating-point mode sweep onto VFP11.
    if((fpscr()&0x07f79f00u)!=0x03000000u){check(false);return;}
    for(unsigned variant=1;variant<4;++variant) {
        rng=0x3d511;
        for(unsigned trial=0;trial<2048;++trial) {
            makeSeed(modes[trial%7]);seed.lights_changed=(trial&8)!=0;
            reset(baseline,trial&16,trial&32,trial&64);
            reset(candidate,trial&16,trial&32,trial&64);
            unsigned count=trial%69,dest=(trial/69)%4;
            const auto* src=(const F3DVtx*)((const uint8_t*)vertices+2*(trial&1));
            LoaderC(baseline,count,dest,src);paths[variant](candidate,count,dest,src);check(equal());
        }
        for(unsigned offset=0;offset<32;offset+=2) {
            makeSeed(0);reset(baseline);reset(candidate);
            LoaderC(baseline,32,0,(const F3DVtx*)((const uint8_t*)baseline->mRsp->loaded_vertices+offset));
            paths[variant](candidate,32,0,(const F3DVtx*)((const uint8_t*)candidate->mRsp->loaded_vertices+offset));
            check(equal());
        }
        // A 16-vertex input overlaps look-at coefficients changed at vertex 0.
        makeSeed(G_LIGHTING);seed.lights_changed=true;reset(baseline);reset(candidate);
        LoaderC(baseline,16,0,(const F3DVtx*)baseline->mRsp->current_lookat_coeffs);
        paths[variant](candidate,16,0,(const F3DVtx*)candidate->mRsp->current_lookat_coeffs);check(equal());
        for(unsigned count:{0U,1U,68U,69U})for(unsigned dest:{0U,67U,68U,0xffffffffU}) {
            makeSeed(0);reset(baseline,true);reset(candidate,true);
            LoaderC(baseline,count,dest,nullptr);paths[variant](candidate,count,dest,nullptr);check(equal());
        }
        // Validate all timing cases, including threshold boundaries and alignment.
        for(unsigned mode:modes)for(unsigned count:counts)for(unsigned offset=0;offset<2;++offset) {
            makeSeed(mode);reset(baseline);reset(candidate);
            const auto* src=(const F3DVtx*)((const uint8_t*)vertices+2*offset);
            LoaderC(baseline,count,0,src);paths[variant](candidate,count,0,src);check(equal());
        }
    }
    check((fpscr()&0x07f79f00u)==0x03000000u);
}

#ifdef LOADER_QEMU_TEST
#include <sys/reent.h>
extern "C" struct _reent* __getreent(){static struct _reent r=_REENT_INIT(r);return &r;}
extern "C" void __aeabi_unwind_cpp_pr0(){}
extern "C" void __aeabi_unwind_cpp_pr1(){}
// This bare QEMU runner has no console C++ exception/thread runtime. Any
// unexpected exception fails the run immediately; the native build uses its
// ordinary libctru/libstdc++ runtime and the same tested loader objects.
extern "C" void* __cxa_begin_catch(void*) noexcept {__builtin_trap();}
extern "C" void __cxa_end_catch(){__builtin_trap();}
extern "C" void __cxa_end_cleanup(){__builtin_trap();}
extern "C" void __cxa_rethrow(){__builtin_trap();}
extern "C" int __gxx_personality_v0(...){__builtin_trap();}
namespace std {
void __throw_bad_alloc(){__builtin_trap();}
void __throw_bad_array_new_length(){__builtin_trap();}
void __throw_length_error(const char*){__builtin_trap();}
}
alignas(32) static uint8_t heap[1024*1024];
static size_t used;
void* operator new(size_t size) {
    size=(size+31)&~size_t(31);if(size>sizeof(heap)-used)__builtin_trap();
    void* result=heap+used;used+=size;return result;
}
void* operator new[](size_t size){return ::operator new(size);}
void operator delete(void*) noexcept{}
void operator delete[](void*) noexcept{}
void operator delete(void*,size_t) noexcept{}
void operator delete[](void*,size_t) noexcept{}
extern "C" void _start() {
    uint32_t controls=0x03000000;__asm__ volatile("vmsr fpscr,%0"::"r"(controls):"memory");
    init();correctness();uint32_t result[4]={failures,checks,0,(uint32_t)sizeof(RSP)};
    controls=0;__asm__ volatile("vmsr fpscr,%0"::"r"(controls):"memory");
    correctness();result[2]=failures==result[0]+1 && checks==result[1]+1;
    register uint32_t r0 __asm__("r0")=1;
    register void* r1 __asm__("r1")=result;
    register uint32_t r2 __asm__("r2")=sizeof(result);
    __asm__ volatile("mov r7,#4\nsvc #0":"+r"(r0):"r"(r1),"r"(r2):"r7","memory");
    r0=result[0]!=0 || !result[2];
    __asm__ volatile("mov r7,#1\nsvc #0"::"r"(r0):"r7","memory");__builtin_unreachable();
}
#else
#include <stdio.h>
extern "C" uint64_t benchTicks();
extern "C" void benchProgress(unsigned,unsigned);
static volatile uint32_t checksum;
static uint64_t measure(Loader selected,unsigned count,unsigned offset,unsigned iterations) {
    Loader volatile fn=selected;
    const auto* src=(const F3DVtx*)((const uint8_t*)vertices+2*offset);
    uint64_t start=benchTicks();
    for(unsigned i=0;i<iterations;++i)fn(candidate,count,0,src);
    uint64_t elapsed=benchTicks()-start;
    uint32_t word;memcpy(&word,&candidate->mRsp->loaded_vertices[0].x,4);checksum^=word;
    return elapsed;
}
extern "C" int runLoaderBenchmark(FILE* file,bool newModel) {
    init();correctness();
    printf("Correctness: %lu failures / %lu checks\n",(unsigned long)failures,(unsigned long)checks);
    if(file)fprintf(file,"# model=%s correctness_failures=%lu checks=%lu rsp_bytes=%u\n",
        newModel?"new":"old",(unsigned long)failures,(unsigned long)checks,(unsigned)sizeof(RSP));
    if(failures || !file)return 1;
    fprintf(file,"mode,vertices,input_offset,round,iterations,c_ticks,asm_ticks,asm16_ticks,c16_ticks\n");
    // Cached matrix/aspect/light state, two active lights. Identical state and
    // addresses for every path, restored outside its timed interval.
    rng=0x3d511;
    for(unsigned m=0;m<7;++m)for(unsigned count:counts) {
        makeSeed(modes[m]);
        for(unsigned offset=0;offset<2;++offset) {
            const unsigned iterations=16384/count;
            for(unsigned round=0;round<5;++round) {
                uint64_t ticks[4];
                for(unsigned slot=0;slot<4;++slot) {
                    unsigned path=round&1?3-slot:slot;
                    reset(candidate);measure(paths[path],count,offset,64);
                    reset(candidate);ticks[path]=measure(paths[path],count,offset,iterations);
                }
                fprintf(file,"%u,%u,%u,%u,%u,%llu,%llu,%llu,%llu\n",m,count,2*offset,round,iterations,
                    (unsigned long long)ticks[0],(unsigned long long)ticks[1],
                    (unsigned long long)ticks[2],(unsigned long long)ticks[3]);
            }
        }
        fflush(file);benchProgress(m,count);
    }
    return 0;
}
#endif
'''

PLATFORM = r'''
#include <3ds.h>
#include <stdio.h>
#include <sys/stat.h>
extern int runLoaderBenchmark(FILE*,bool);
uint64_t benchTicks(void){return svcGetSystemTick();}
void benchProgress(unsigned mode,unsigned count) {
    printf("mode %u / %u vertices complete\n",mode,count);
    gfxFlushBuffers();gfxSwapBuffers();gspWaitForVBlank();
}
int main(void) {
    gfxInitDefault();consoleInit(GFX_TOP,NULL);
    bool newModel=false;APT_CheckNew3DS(&newModel);osSetSpeedupEnable(newModel);
    uint32_t fpscr;__asm__ volatile("vmrs %0,fpscr":"=r"(fpscr));
    printf("SoH vertex LOADER benchmark v1\n%s 3DS / FPSCR %08lx\nChecking results...\n",
        newModel?"New":"Old",(unsigned long)fpscr);
    gfxFlushBuffers();gfxSwapBuffers();gspWaitForVBlank();
    mkdir("sdmc:/3ds",0777);mkdir("sdmc:/3ds/soh",0777);
    FILE* file=fopen("sdmc:/3ds/soh/arm11-vertex-loader-benchmark.csv","w");
    if(file) {
        fprintf(file,"# benchmark=vertex-loader-v1 fpscr=%08lx stage=correctness\n",(unsigned long)fpscr);
        fflush(file);
    } else printf("ERROR: cannot save CSV. Check SD card.\n");
    int failed=runLoaderBenchmark(file,newModel);
    if(file) {if(ferror(file))failed=1;if(fclose(file)!=0)failed=1;}
    printf("\n%s\nSTART to exit\n",failed?"FAILED - see CSV/console":"Done");
    while(aptMainLoop()) {
        hidScanInput();if(hidKeysDown()&KEY_START)break;
        gfxFlushBuffers();gfxSwapBuffers();gspWaitForVBlank();
    }
    gfxExit();return failed;
}
'''


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    source = SOURCE.read_text()
    helper_names = ['void Interpreter::NormalizeVector(', 'void Interpreter::TransposedMatrixMul(',
                    'void Interpreter::MatrixMul(', 'void Interpreter::CalculateNormalDir(',
                    'void Interpreter::UpdateCombinedMatrix(', 'float Interpreter::AdjXForAspectRatio(']
    helpers = ''.join(function(source, name) for name in helper_names)
    method = function(source, 'void Interpreter::GfxSpVertex(')
    # Retain the measured v1 controls when production's experimental threshold
    # changes. Only normalize the count predicate; preserve the safety guards.
    method, replacements = re.subn(
        r'(vertices != nullptr && )n_vertices (?:!= 0|>= 32)( && inputDisjoint)',
        r'\1n_vertices != 0\2', method)
    assert replacements == 1
    assert method.count('vertices != nullptr && n_vertices != 0 && inputDisjoint') == 1
    sdk = vertex.DKP / 'devkitARM/bin'
    cc, cxx = str(sdk / 'arm-none-eabi-gcc'), str(sdk / 'arm-none-eabi-g++')
    flags = [*vertex.FLAGS, '-mtp=soft', '-mword-relocations', '-D__3DS__', '-DARM11',
             '-DF3DEX_GBI_2', '-DNDEBUG', '-ffunction-sections', '-fdata-sections',
             '-I'+str(ROOT / 'third_party/libultraship/include'),
             '-I'+str(ROOT / 'third_party/libultraship/build-3ds/_deps/imgui-src'),
             '-I'+str(vertex.DKP / 'portlibs/3ds/include')]
    objects = []
    for name, enabled, threshold in [('LoaderC',0,0),('LoaderAsm',1,0),('LoaderAsm16',1,16),('LoaderC16',1,16)]:
        body = method
        if threshold:
            body = body.replace('vertices != nullptr && n_vertices != 0 && inputDisjoint',
                                'vertices != nullptr && n_vertices >= 16 && inputDisjoint')
        if name == 'LoaderC16':
            body = body.replace('Soh3dsTransformVerticesArm11(', 'VertexDisjointReference(')
        path = OUT / (name+'.cpp')
        path.write_text(PREFIX + f'\n#define SOH3DS_ARM11_VERTEX_ASM {enabled}\nnamespace Fast {{\n' + helpers + body + '}\n')
        obj = path.with_suffix('.o')
        subprocess.run([cxx,*flags,'-std=gnu++20','-c',str(path),'-o',str(obj)],check=True)
        symbols = subprocess.check_output([str(sdk/'arm-none-eabi-nm'),'--defined-only',str(obj)],text=True)
        matches = [line.split()[-1] for line in symbols.splitlines() if 'GfxSpVertex' in line]
        assert len(matches)==1, matches
        # Rename the actual method entry (this pointer is its first argument).
        # Local helpers preserve per-translation-unit inlining and avoid ODR
        # conflicts without changing the production class declaration/layout.
        localize = ['--localize-symbol='+line.split()[-1] for line in symbols.splitlines()
                    if line.split()[-2]=='T' and line.split()[-1]!=matches[0]]
        subprocess.run([str(sdk/'arm-none-eabi-objcopy'),'--redefine-sym',matches[0]+'='+name,
                        *localize,str(obj)],check=True)
        objects.append(str(obj))
    utilities = (ROOT/'third_party/libultraship/src/ship/utils/Utils.cpp').read_text()
    support = PREFIX + '\nextern "C" { bool gSoh3dsRenderProfileEnabled=false; }\nnamespace Fast {\n'
    support += 'constexpr size_t MAX_TRI_BUFFER=256;\n' + function(source,'Interpreter::Interpreter()')
    support += function(source,'Interpreter::~Interpreter()') + '}\nnamespace Ship::Math {\n'
    support += function(utilities,'float clamp(') + '}\n'
    (OUT/'support.cpp').write_text(support)
    shared = vertex.harness()
    start=shared.index('__attribute__((noinline)) void vertexReference')
    end=shared.index('static uint32_t rng=',start)
    reference=shared[:end].replace('vertexReference(', 'VertexDisjointReference(')
    reference=reference.replace('const void* input,void* output,','const void* restrict input,void* restrict output,')
    reference=reference.replace('const float matrix[4][4]','const float matrix[restrict 4][4]')
    (OUT/'reference.c').write_text(reference)
    (OUT/'driver.cpp').write_text(PREFIX+DRIVER)
    (OUT/'platform.c').write_text(PLATFORM)
    for compiler, name, extra in [(cxx,'support.cpp',['-std=gnu++20']), (cc,'reference.c',[]),
                                  (cc,'platform.c',['-I'+str(vertex.DKP/'libctru/include')])]:
        obj=OUT/(Path(name).stem+'.o')
        subprocess.run([compiler,*flags,*extra,'-c',str(OUT/name),'-o',str(obj)],check=True)
        if name!='platform.c': objects.append(str(obj))
    subprocess.run([cc,*flags,'-c',str(vertex.ASM),'-o',str(OUT/'vertex.o')],check=True)
    objects.append(str(OUT/'vertex.o'))
    # Verify the exact loader objects and the native correctness driver on ARM11.
    qemu=OUT/'qemu-test'
    subprocess.run([cxx,*flags,'-std=gnu++20','-DLOADER_QEMU_TEST','-fno-strict-aliasing',
                    '-fno-exceptions','-fno-rtti','-nostdlib','-Wl,-e,_start','-Wl,-Ttext=0x10000',
                    '-Wl,--gc-sections',str(OUT/'driver.cpp'),*objects,
                    '-Wl,--start-group','-lstdc++','-lm','-lc','-lgcc','-Wl,--end-group',
                    '-o',str(qemu)],check=True)
    run=subprocess.run(['qemu-arm','-cpu','arm11mpcore',str(qemu)],capture_output=True,timeout=60)
    assert len(run.stdout)==16,(run.returncode,run.stderr.decode())
    failures, checks, rejected, rsp_bytes=struct.unpack('<IIII',run.stdout)
    assert run.returncode==0 and failures==0 and rejected==1,(failures,checks,rejected)
    verification={'failures':failures,'native_checks':checks,'unsupported_fpscr_rejected':bool(rejected),
                  'rsp_bytes':rsp_bytes,'cpu':'arm11mpcore'}
    (OUT/'verification.json').write_text(json.dumps(verification,indent=2)+'\n')
    print(json.dumps(verification))
    elf=OUT/'soh-arm11-vertex-benchmark.elf'
    subprocess.run([cxx,*flags,'-std=gnu++20','-fno-strict-aliasing',
                    '-specs='+str(vertex.DKP/'devkitARM/arm-none-eabi/lib/3dsx.specs'),
                    '-Wl,--gc-sections',str(OUT/'driver.cpp'),*objects,str(OUT/'platform.o'),
                    '-L'+str(vertex.DKP/'libctru/lib'),'-lctru','-lm','-o',str(elf)],check=True)
    subprocess.run([str(vertex.DKP/'tools/bin/3dsxtool'),str(elf),str(elf.with_suffix('.3dsx'))],check=True)
    disasm=subprocess.check_output([str(sdk/'arm-none-eabi-objdump'),'-d',str(elf)],text=True)
    (OUT/'benchmark.disasm').write_text(disasm)
    manifest={path.name:hashlib.sha256(path.read_bytes()).hexdigest() for path in OUT.iterdir()
              if path.suffix in ('.c','.cpp','.o','.3dsx','.elf')}
    manifest['production_source_sha256']=hashlib.sha256(SOURCE.read_bytes()).hexdigest()
    manifest['assembly_sha256']=hashlib.sha256(vertex.ASM.read_bytes()).hexdigest()
    manifest['builder_sha256']=hashlib.sha256(Path(__file__).read_bytes()).hexdigest()
    for name in ('interpreter.h','lus_gbi.h'):
        manifest[name+'_sha256']=hashlib.sha256((ROOT/'third_party/libultraship/include/fast'/name).read_bytes()).hexdigest()
    (OUT/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
    print(elf.with_suffix('.3dsx'))


if __name__=='__main__':
    main()
