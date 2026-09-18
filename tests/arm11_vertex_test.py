#!/usr/bin/env python3
"""Compare ARM11 vertex batches with the production transform expressions.

Catches wrong matrix order, signed-coordinate conversion, record strides,
overwrites of other vertex fields, zero-count reads, and AAPCS corruption.
QEMU results validate correctness, never physical performance.
"""
from pathlib import Path
import os
import shutil
import struct
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
DKP = Path(os.environ.get('DEVKITPRO', str(Path.home() / 'dkp-root/opt/devkitpro')))
ASM = ROOT / 'third_party/libultraship/src/fast/vertex_transform_arm11.S'
FLAGS = ['-O3', '-march=armv6k', '-mtune=mpcore', '-mfpu=vfp',
         '-mfloat-abi=hard', '-fno-fast-math', '-ffp-contract=off']


def harness():
    source = (ROOT / 'third_party/libultraship/src/fast/interpreter.cpp').read_text()
    start = source.index('        x = v->ob[0]')
    end = source.index('\n        }', start)
    expressions = source[start:end].replace('mRsp->MP_matrix', 'matrix')
    return r'''
#include <stdint.h>
#include <stddef.h>
typedef struct { int16_t ob[3]; uint16_t flag; int16_t tc[2]; uint8_t cn[4]; } Input;
typedef struct { float x,y,z,w,u,v; uint8_t color[4],clip_rej,padding[3]; } Output;
#include "fast/vertex_transform_3ds.h"
__attribute__((noinline)) void vertexReference(const void* input,void* output,
                                              const float matrix[4][4],uint32_t count) {
    const Input* vertices = (const Input*)input;
    Output* dest = (Output*)output;
    for (uint32_t i = 0; i < count; ++i) {
        const Input* v = vertices + i;
        float x,y,z,w;
''' + expressions + r'''
        dest[i].x=x;dest[i].y=y;dest[i].z=z;dest[i].w=w;
    }
}
static uint32_t rng=0x3d511;
static uint32_t randomWord(void) { rng=rng*1664525u+1013904223u;return rng; }
static uint32_t failures, comparisons;
static void check(int success) { ++comparisons;if(!success)++failures; }
static int same(const void* a,const void* b,unsigned size) {
    const uint8_t* x=(const uint8_t*)a;const uint8_t* y=(const uint8_t*)b;
    for(unsigned i=0;i<size;++i)if(x[i]!=y[i])return 0;return 1;
}
static void fill(void* target,unsigned size) {
    uint8_t* p=(uint8_t*)target;for(unsigned i=0;i<size;++i)p[i]=(uint8_t)randomWord();
}
static void copyBytes(void* dst,const void* src,unsigned size) {
    uint8_t* d=(uint8_t*)dst;const uint8_t* s=(const uint8_t*)src;
    for(unsigned i=0;i<size;++i)d[i]=s[i];
}
static Input inputs[72], savedInputs[72];
static Output expected[72], actual[72];
static float matrix[4][4], savedMatrix[4][4];
static uint32_t fpscr(void) { uint32_t v;__asm__ volatile("vmrs %0,fpscr":"=r"(v));return v; }
static void setFpscr(uint32_t v) { __asm__ volatile("vmsr fpscr,%0"::"r"(v):"memory"); }
static void vertexCorrectness(void) {
    const uint32_t original=fpscr();
#ifdef SOH3DS_VERTEX_HARDWARE_BENCHMARK
    // VFP11 delegates gradual underflow/non-RunFast cases to a software
    // exception handler, which 3DS applications do not have. QEMU implements
    // those cases itself. Never import its mode sweep into the native test.
    if ((original & 0x07f79f00u) != 0x03000000u) {
        check(0);return; // Report unsupported inherited controls without a trap.
    }
#endif
    // No reads, including of a null matrix, are allowed for an empty batch.
    Soh3dsTransformVerticesArm11(0,0,0,0);
    for(unsigned trial=0;trial<8192;++trial) {
        fill(inputs,sizeof(inputs));copyBytes(savedInputs,inputs,sizeof(inputs));
        fill(expected,sizeof(expected));copyBytes(actual,expected,sizeof(actual));
        for(unsigned j=0;j<16;++j) {
            union {uint32_t bits;float value;} v;
            v.bits=randomWord();
            if(trial<4096)v.value=((int32_t)(v.bits&65535)-32768)/128.0f;
            ((float*)matrix)[j]=v.value;
        }
        copyBytes(savedMatrix,matrix,sizeof(matrix));
        // Every legal batch size, plus halfword-only source and word-only
        // destination alignment. Guards cover every untouched record/field.
        unsigned count=trial%69;
        const void* src=(const uint8_t*)inputs+2*(trial&1);
        void* want=(uint8_t*)expected+32+4*((trial>>1)&1);
        void* got=(uint8_t*)actual+32+4*((trial>>1)&1);
#ifdef SOH3DS_VERTEX_HARDWARE_BENCHMARK
        uint32_t mode=original & ~0x9fu; // Clear arithmetic flags only.
#else
        uint32_t mode=((trial>>2)&3)<<22; // QEMU: all four rounding modes
        if(trial&16)mode|=1u<<24; // flush-to-zero
        if(trial&32)mode|=1u<<25; // default NaN
#endif
        setFpscr(mode);vertexReference(src,want,matrix,count);uint32_t cFlags=fpscr();
        setFpscr(mode);Soh3dsTransformVerticesArm11(src,got,matrix,count);uint32_t aFlags=fpscr();
        check(same(expected,actual,sizeof(actual)));
        check((cFlags&0x03c0009f)==(aFlags&0x03c0009f));
        check(same(inputs,savedInputs,sizeof(inputs)) && same(matrix,savedMatrix,sizeof(matrix)));
    }
    setFpscr(original);
    // Hand-computed fixture catches a shared mistake in a reference extraction.
    for(unsigned i=0;i<16;++i)((float*)matrix)[i]=(float)(i+1);
    inputs[0].ob[0]=-2;inputs[0].ob[1]=3;inputs[0].ob[2]=4;
    Soh3dsTransformVerticesArm11(inputs,actual,matrix,1);
    check(actual[0].x==62 && actual[0].y==68 && actual[0].z==74 && actual[0].w==80);
}
'''


ABI_PROBE = r'''
.syntax unified
.arch armv6k
.fpu vfp
.arm
.global checkVertexAbi
.type checkVertexAbi,%function
checkVertexAbi:
    push {r4-r11,r12,lr}
    vpush {d8-d15}
    sub sp,sp,#16
    str r1,[sp]
    str r2,[sp,#4]
    mov r12,r0
    ldmia r12!,{r4-r11}
    vldmia r12,{d8-d15}
    bl Soh3dsTransformVerticesArm11
    ldr r12,[sp,#4]
    stmia r12!,{r4-r11}
    vstmia r12,{d8-d15}
    add sp,sp,#16
    vpop {d8-d15}
    pop {r4-r11,r12,pc}
.size checkVertexAbi,.-checkVertexAbi
.section .note.GNU-stack,"",%progbits
'''


def main():
    assert ASM.exists(), 'ARM11 batched vertex transform has not been implemented'
    compiler = DKP / 'devkitARM/bin/arm-none-eabi-gcc'
    if not compiler.exists() or not shutil.which('qemu-arm'):
        raise SystemExit('Set DEVKITPRO and install qemu-arm to run the ARM11 test')
    with tempfile.TemporaryDirectory(prefix='soh-vertex-arm11-') as temp:
        out = Path(temp)
        # ABI probe supplies valid but otherwise unimportant pointers. For count
        # zero none are dereferenced; for count one its 96-byte seed is an input.
        code = harness() + r'''
void checkVertexAbi(void*,void*,void*,uint32_t);
void __aeabi_unwind_cpp_pr0(void){}
void __aeabi_unwind_cpp_pr1(void){}
void _start(void) {
    vertexCorrectness();
    uint32_t seed[24],observed[24];
    for(unsigned i=0;i<24;++i)seed[i]=randomWord();
    checkVertexAbi(seed,actual,observed,0);check(same(seed,observed,sizeof(seed)));
    checkVertexAbi(seed,actual,observed,1);check(same(seed,observed,sizeof(seed)));
    uint32_t result[2]={failures,comparisons};
    register uint32_t r0 __asm__("r0")=1;
    register void* r1 __asm__("r1")=result;
    register uint32_t r2 __asm__("r2")=sizeof(result);
    __asm__ volatile("mov r7,#4\nsvc #0":"+r"(r0):"r"(r1),"r"(r2):"r7","memory");
    r0=failures!=0;
    __asm__ volatile("mov r7,#1\nsvc #0"::"r"(r0):"r7","memory");
    __builtin_unreachable();
}
'''
        (out / 'test.c').write_text(code)
        (out / 'abi.S').write_text(ABI_PROBE)
        subprocess.run([str(compiler), *FLAGS, '-fno-strict-aliasing', '-fno-builtin',
                        '-I'+str(ROOT / 'third_party/libultraship/include'),
                        '-nostdlib', '-Wl,-e,_start', '-Wl,-Ttext=0x10000',
                        str(out / 'test.c'), str(out / 'abi.S'), str(ASM),
                        '-o', str(out / 'test')], check=True)
        run = subprocess.run(['qemu-arm', '-cpu', 'arm11mpcore', str(out / 'test')],
                             capture_output=True, timeout=60)
        assert len(run.stdout) == 8, (run.returncode, run.stderr.decode())
        failures, checks = struct.unpack('<II', run.stdout)
        assert run.returncode == 0 and failures == 0, (failures, checks)
        print(f'PASS: 8192 ARM11 vertex batches; {checks} exact-result, memory, FPSCR and ABI checks')


if __name__ == '__main__':
    main()
