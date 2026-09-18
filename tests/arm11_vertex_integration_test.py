#!/usr/bin/env python3
"""Run the production vertex loader with ASM enabled/disabled on ARM11.

Exercises the real loop, bounds and alias fallback, texture coordinates,
directional/positional lighting, fog, clipping and aspect correction.
Platform services are excluded; renderer math uses devkitARM's real libm.
"""
import importlib.util
from pathlib import Path
import struct
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('vertex', ROOT / 'tests/arm11_vertex_test.py')
vertex = importlib.util.module_from_spec(spec)
spec.loader.exec_module(vertex)


def harness():
    source = (ROOT / 'third_party/libultraship/src/fast/interpreter.cpp').read_text()
    begin = source.index('void Interpreter::GfxSpVertex(')
    end = source.index('\nvoid Interpreter::GfxSpModifyVertex', begin)
    method = source[begin:end]
    code = vertex.harness() + r'''
#include <algorithm>
#include <cmath>
#include <limits>
#include <cstring>
#define MAX_VERTICES 64
#define G_LIGHTING_POSITIONAL 1
#define G_LIGHTING 2
#define G_TEXTURE_GEN 4
#define G_TEXTURE_GEN_LINEAR 8
#define G_FOG 16
typedef Input F3DVtx_t;
struct F3DVtx_tn { int16_t ob[3]; uint16_t flag; int16_t tc[2]; int8_t n[3]; uint8_t a; };
union F3DVtx { F3DVtx_t v; F3DVtx_tn n; int64_t alignment; };
struct RGBA { uint8_t r,g,b,a; };
struct LoadedVertex { float x,y,z,w,u,v; RGBA color; uint8_t clip_rej; };
struct F3DLight_t { uint8_t col[3]; int8_t dir[3]; };
struct Light { F3DLight_t l; struct { float pos[3]; uint8_t unk3,unk7,unkE; } p; };
struct RSP {
    float MP_matrix[4][4],modelview_matrix_stack[2][4][4];
    unsigned modelview_matrix_stack_size=1,geometry_mode;
    struct { uint16_t s,t; } texture_scaling_factor;
    bool lights_changed=false;
    int current_num_lights=2;
    Light current_lights[3]; float current_lights_coeffs[3][3];
    F3DLight_t lookat[2]; float current_lookat_coeffs[2][3];
    float fog_mul,fog_offset;
    LoadedVertex loaded_vertices[MAX_VERTICES+4];
};
namespace Ship { namespace Math {
template<class T> T clamp(T v,T lo,T hi){return std::clamp(v,lo,hi);}
} }
enum class Soh3dsProfileSection {Vertex};
struct Soh3dsProfileScope { explicit Soh3dsProfileScope(Soh3dsProfileSection){} };
struct Interpreter {
    RSP state{}; RSP* mRsp=&state; unsigned updates=0;
    void UpdateCombinedMatrix(){++updates;}
    float AdjXForAspectRatio(float x){return x*0.8f;}
    void CalculateNormalDir(const F3DLight_t* light,float* coeffs){
        for(int i=0;i<3;++i)coeffs[i]=light->dir[i]/127.0f;
    }
    void TransposedMatrixMul(float res[3],const float a[3],const float b[4][4]) {
        for(int i=0;i<3;++i)res[i]=a[0]*b[i][0]+a[1]*b[i][1]+a[2]*b[i][2];
    }
    void GfxSpVertex(size_t,size_t,const F3DVtx*);
    void Reference(size_t,size_t,const F3DVtx*);
};
extern "C" void VertexAssembly(const void*,void*,const float[4][4],uint32_t);
static unsigned batchCalls;
extern "C" void Soh3dsTransformVerticesArm11(const void* a,void* b,const float m[4][4],uint32_t n){
    ++batchCalls;VertexAssembly(a,b,m,n);
}
#define __3DS__
#define SOH3DS_ARM11_VERTEX_ASM 0
''' + method.replace('Interpreter::GfxSpVertex(', 'Interpreter::Reference(') + r'''
#undef SOH3DS_ARM11_VERTEX_ASM
#define SOH3DS_ARM11_VERTEX_ASM 1
''' + method + r'''
static Interpreter baseline, candidate;
static F3DVtx vertices[72];
static void setup(unsigned mode) {
    fill(&baseline.state,sizeof(baseline.state));
    RSP& state=baseline.state;
    state.modelview_matrix_stack_size=1;state.geometry_mode=mode;
    state.current_num_lights=2;state.lights_changed=(mode&32)!=0;
    state.fog_mul=100.0f;state.fog_offset=32.0f;
    state.texture_scaling_factor={0xffff,0x8000};
    for(unsigned i=0;i<16;++i) {
        ((float*)state.MP_matrix)[i]=(int)(randomWord()%128)/32.0f-2.0f;
        ((float*)state.modelview_matrix_stack[0])[i]=(i%5==0)?1.0f:0.0f;
    }
    for(auto& light:state.current_lights) {
        for(unsigned i=0;i<3;++i)light.p.pos[i]=10000.0f+i*1000;
        light.p.unk3=1;light.p.unk7=1;light.p.unkE=1;
    }
    for(auto& coeffs:state.current_lights_coeffs)for(float& v:coeffs)v=0.125f;
    for(auto& coeffs:state.current_lookat_coeffs)for(float& v:coeffs)v=0.125f;
    copyBytes(&candidate.state,&baseline.state,sizeof(RSP));
    baseline.updates=candidate.updates=0;batchCalls=0;
}
static void integrationCorrectness() {
    for(unsigned trial=0;trial<2048;++trial) {
        setup(trial&63);fill(vertices,sizeof(vertices));
        unsigned count=trial%69, dest=(trial/69)%4;
        baseline.Reference(count,dest,vertices);
        candidate.GfxSpVertex(count,dest,vertices);
        check(same(&baseline.state,&candidate.state,sizeof(RSP)));
        check(baseline.updates==candidate.updates);
        check(batchCalls==unsigned(count>=32 && count<=68-dest));
    }
    // Whole-record aliasing and offsets of one halfword: the fallback must
    // retain interleaved color/position writes and reads of later vertices.
    for(unsigned offset=0;offset<32;offset+=2) {
        setup(0);fill(baseline.state.loaded_vertices,sizeof(baseline.state.loaded_vertices));
        copyBytes(&candidate.state,&baseline.state,sizeof(RSP));
        baseline.Reference(32,0,(const F3DVtx*)((const uint8_t*)baseline.state.loaded_vertices+offset));
        candidate.GfxSpVertex(32,0,(const F3DVtx*)((const uint8_t*)candidate.state.loaded_vertices+offset));
        check(same(&baseline.state,&candidate.state,sizeof(RSP)));check(batchCalls==0);
    }
    // Lighting recalculates look-at coefficients during the first vertex.
    // Input backed by that state must read the updated second vertex in order.
    setup(G_LIGHTING | 32);
    baseline.Reference(32,0,(const F3DVtx*)baseline.state.current_lookat_coeffs);
    candidate.GfxSpVertex(32,0,(const F3DVtx*)candidate.state.current_lookat_coeffs);
    check(same(&baseline.state,&candidate.state,sizeof(RSP)));check(batchCalls==0);
    // Measured large-batch dispatch threshold, including adjacent counts.
    for(unsigned count:{31U,32U,33U}) {
        setup(G_LIGHTING | G_FOG);fill(vertices,sizeof(vertices));
        baseline.Reference(count,0,vertices);candidate.GfxSpVertex(count,0,vertices);
        check(same(&baseline.state,&candidate.state,sizeof(RSP)));
        check(batchCalls==unsigned(count>=32));
    }
    for(unsigned count: {0U,1U,68U,69U})for(unsigned dest: {0U,67U,68U,0xffffffffU}) {
        setup(0);baseline.Reference(count,dest,nullptr);candidate.GfxSpVertex(count,dest,nullptr);
        check(same(&baseline.state,&candidate.state,sizeof(RSP)) && batchCalls==0);
        check(baseline.updates==candidate.updates);
    }
}
'''
    return code


def main():
    with tempfile.TemporaryDirectory(prefix='soh-vertex-integration-') as temp:
        out = Path(temp)
        (out / 'test.cpp').write_text(harness() + r'''
#include <sys/reent.h>
extern "C" struct _reent* __getreent(void) {
    static struct _reent state = _REENT_INIT(state);
    return &state;
}
extern "C" void __aeabi_unwind_cpp_pr0(void){}
extern "C" void __aeabi_unwind_cpp_pr1(void){}
extern "C" void _start(void) {
    integrationCorrectness();
    uint32_t result[2]={failures,comparisons};
    register uint32_t r0 __asm__("r0")=1;
    register void* r1 __asm__("r1")=result;
    register uint32_t r2 __asm__("r2")=sizeof(result);
    __asm__ volatile("mov r7,#4\nsvc #0":"+r"(r0):"r"(r1),"r"(r2):"r7","memory");
    r0=failures!=0;
    __asm__ volatile("mov r7,#1\nsvc #0"::"r"(r0):"r7","memory");
    __builtin_unreachable();
}
''')
        binpath = vertex.DKP / 'devkitARM/bin'
        subprocess.run([str(binpath / 'arm-none-eabi-gcc'), *vertex.FLAGS,
                        '-DSoh3dsTransformVerticesArm11=VertexAssembly', '-c', str(vertex.ASM),
                        '-o', str(out / 'vertex.o')], check=True)
        subprocess.run([str(binpath / 'arm-none-eabi-g++'), *vertex.FLAGS, '-std=c++20',
                        '-I'+str(ROOT / 'third_party/libultraship/include'),
                        '-fno-strict-aliasing', '-fno-exceptions', '-fno-rtti',
                        '-ffunction-sections', '-fdata-sections', '-nostdlib',
                        '-Wl,-e,_start', '-Wl,-Ttext=0x10000', '-Wl,--gc-sections',
                        str(out / 'test.cpp'), str(out / 'vertex.o'),
                        '-Wl,--start-group', '-lm', '-lc', '-lgcc', '-Wl,--end-group',
                        '-o', str(out / 'test')], check=True)
        run = subprocess.run(['qemu-arm', '-cpu', 'arm11mpcore', str(out / 'test')],
                             capture_output=True, timeout=60)
        assert len(run.stdout) == 8, (run.returncode, run.stderr.decode())
        failures, checks = struct.unpack('<II', run.stdout)
        assert run.returncode == 0 and failures == 0, (failures, checks)
        print(f'PASS: production vertex loader, {checks} ARM11 comparisons across render modes and alias/bounds cases')


if __name__ == '__main__':
    main()
