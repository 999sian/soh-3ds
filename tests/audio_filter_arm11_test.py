#!/usr/bin/env python3
"""Exact FIR PCM/history tests shared with a physical ARM11 benchmark."""
from pathlib import Path
import subprocess
import tempfile
import sys

ROOT = Path(__file__).resolve().parents[1]
SDK = Path('/home/sian/dkp-root/opt/devkitpro/devkitARM/bin')


def function(source, signature):
    start = source.index(signature)
    end = source.index('{', start) + 1
    depth = 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]


def harness():
    source = (ROOT/'third_party/shipwright/soh/soh/mixer.c').read_text()
    state = source[source.index('static struct {'):source.index('} rspa;') + len('} rspa;')]
    body = function(source, 'void aFilterImpl(')
    return r'''
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <arm_acle.h>
#include "compat3ds/arm11/kernels.h"
#define A_INIT 1
#define ROUND_UP_16(v) (((v)+15)&~15)
#define DMEM_BUF_SIZE (0x1000-0x3C0-0x40)
#define BUF_S16(a) (rspa.buf.as_s16+((a)-0x3C0)/sizeof(int16_t))
#define SOH3DS_AUDIO_GUARD(op,p,n) ((p)!=NULL)
typedef int16_t ADPCM_STATE[16];
#ifdef FILTER_FREESTANDING
void* memcpy(void* d,const void* s,size_t n){for(size_t i=0;i<n;i++)((char*)d)[i]=((const char*)s)[i];return d;}
void* memset(void* d,int v,size_t n){for(size_t i=0;i<n;i++)((char*)d)[i]=v;return d;}
#endif
void Soh3dsFilterArm11(int16_t*,int16_t[8],const int16_t[8],uint32_t);
int Soh3dsFilterAbiCheck(int16_t*,int16_t[8],const int16_t[8],uint32_t);
static uint32_t randomState=123, failures=0, comparisons=0;
static uint32_t rnd(void){randomState=randomState*1664525u+1013904223u;return randomState;}
static void check(int ok){++comparisons;if(!ok)++failures;}
static int equal(const void* a,const void* b,unsigned n){
 for(unsigned i=0;i<n;i++)if(((const uint8_t*)a)[i]!=((const uint8_t*)b)[i])return 0;return 1;
}
''' + function(source, 'static inline int16_t clamp16(') + r'''
// Match mixer.c's actual -O2 and unroll-loops policy for the full C baseline.
#pragma GCC optimize("unroll-loops")
#undef SOH3DS_ARM11_FILTER_ASM
#define rspa refRspa
''' + state + '\n' + body.replace('void aFilterImpl(', '__attribute__((noinline)) void filterC(') + r'''
#undef rspa
#define SOH3DS_ARM11_FILTER_ASM 1
#define rspa asmRspa
''' + state + '\n' + body.replace('void aFilterImpl(', '__attribute__((noinline)) void filterAsm(') + r'''
#undef rspa
#undef SOH3DS_ARM11_FILTER_ASM
// Independent primitive oracle: full 64-bit sum and original block history.
__attribute__((noinline)) void filterReference(int16_t* buf,int16_t history[8],const int16_t coeff[8],uint32_t count){
 while(count){
  int16_t window[16];memcpy(window,history,16);memcpy(window+8,buf,16);
  for(int i=0;i<8;i++){
   int64_t acc=0x4000;
   for(int j=0;j<8;j++)acc+=(int32_t)window[i+j]*coeff[7-j];
   int32_t out=(int32_t)(acc>>15);buf[i]=out<-32768?-32768:out>32767?32767:out;
  }
  memcpy(history,window+8,16);buf+=8;count-=8;
 }
}
static int16_t bufC[560] __attribute__((aligned(4))),bufAsm[560] __attribute__((aligned(4)));
static int16_t historyC[12] __attribute__((aligned(4))),historyAsm[12] __attribute__((aligned(4)));
static int16_t coeffs[12] __attribute__((aligned(4))),coeffCopy[12];
static int16_t stateC[20] __attribute__((aligned(4))),stateAsm[20] __attribute__((aligned(4)));
static void correctness(void){
 check(Soh3dsFilterAbiCheck(NULL,NULL,NULL,0)==0);
 for(unsigned trial=0;trial<8192;trial++){
  for(unsigned i=0;i<560;i++)bufC[i]=bufAsm[i]=trial<16?32767:trial<32?-32768:trial<48?(i&1?32767:-32768):(int16_t)rnd();
  for(unsigned i=0;i<12;i++){
   historyC[i]=historyAsm[i]=trial<16?32767:trial<32?-32768:(int16_t)rnd();
   coeffs[i]=coeffCopy[i]=trial<32?(trial&8?-32768:32767):(int16_t)rnd();
  }
  unsigned count=trial<16?0:trial<64?8:((trial%68)+1)*8;
  unsigned b=2+(trial&1),h=2+((trial>>1)&1),f=2+((trial>>2)&1);
  filterReference(bufC+b,historyC+h,coeffs+f,count);
  int abi=Soh3dsFilterAbiCheck(bufAsm+b,historyAsm+h,coeffs+f,count);
  check(abi==0&&equal(bufC,bufAsm,sizeof(bufC))&&equal(historyC,historyAsm,sizeof(historyC))&&equal(coeffs,coeffCopy,sizeof(coeffs)));
 }
 // Actual production aFilterImpl: setup rounding/wrap, init/continuation,
 // coefficient averaging (including negative odd sums), rejected pointers,
 // changed coefficients and entire DMEM/state including surrounding canaries.
 for(unsigned trial=0;trial<2048;trial++){
  memset(&refRspa,0,sizeof(refRspa));
  for(unsigned i=0;i<DMEM_BUF_SIZE/2;i++)refRspa.buf.as_s16[i]=(int16_t)rnd();
  memcpy(&asmRspa,&refRspa,sizeof(refRspa));
  for(unsigned i=0;i<20;i++)stateC[i]=stateAsm[i]=(int16_t)rnd();
  for(unsigned i=0;i<12;i++)coeffs[i]=(int16_t)rnd();
  const uint16_t counts[]={0,1,15,16,17,31,127,128,129,1024,1088,65535};
  uint16_t bytes=counts[trial%12],addr=0x500+2*(trial&1);
  int16_t* coefficients=trial%32?coeffs+(trial&1):NULL;
  filterC(2,bytes,coefficients);filterAsm(2,bytes,coefficients);
  check(equal(&refRspa,&asmRspa,sizeof(refRspa)));
  for(unsigned step=0;step<4;step++){
   int16_t* a=trial%16?stateC+(trial&1):NULL;
   int16_t* b=trial%16?stateAsm+(trial&1):NULL;
   uint8_t flags=step==0&&trial&2?A_INIT:0;
   filterC(flags,addr,a);filterAsm(flags,addr,b);
   check(equal(&refRspa,&asmRspa,sizeof(refRspa))&&equal(stateC,stateAsm,sizeof(stateC)));
   if(step==1){
    for(unsigned i=0;i<12;i++)coeffs[i]=(int16_t)rnd();
    filterC(2,bytes,coeffs+(trial&1));filterAsm(2,bytes,coeffs+(trial&1));
   }
  }
 }
}
'''


def main():
    with tempfile.TemporaryDirectory() as directory:
        p = Path(directory)
        stub = '--stub' in sys.argv
        (p/'test.c').write_text(harness() + (r'''
void Soh3dsFilterArm11(int16_t* b,int16_t h[8],const int16_t c[8],uint32_t n){}
''' if stub else '') + r'''
void __aeabi_unwind_cpp_pr0(void){} void __aeabi_unwind_cpp_pr1(void){}
void _start(void){
 correctness();uint32_t result[2]={failures,comparisons};
 register uint32_t r0 __asm__("r0")=1;register void* r1 __asm__("r1")=result;register uint32_t r2 __asm__("r2")=8;
 __asm__ volatile("mov r7,#4\nsvc #0":"+r"(r0):"r"(r1),"r"(r2):"r7","memory");
 r0=failures!=0;__asm__ volatile("mov r7,#1\nsvc #0"::"r"(r0):"r7","memory");__builtin_unreachable();}
''')
        asm = [] if stub else [str(ROOT/'src/compat3ds/arm11/audio_filter.S')]
        subprocess.run([str(SDK/'arm-none-eabi-gcc'), '-O2', '-D__3DS__', '-DFILTER_FREESTANDING',
                        '-march=armv6k', '-mfloat-abi=soft', '-fno-builtin', '-nostdlib',
                        '-Wl,-e,_start', '-Wl,-Ttext=0x10000', '-I'+str(ROOT/'src'),
                        str(p/'test.c'), str(ROOT/'tests/fixtures/filter_arm11_abi.S'),
                        *asm, '-o', str(p/'test')], check=True)
        run = subprocess.run(['qemu-arm', '-cpu', 'arm11mpcore', str(p/'test')], stdout=subprocess.PIPE, timeout=60)
        assert len(run.stdout)==8, (run.returncode,run.stdout.hex())
        failures, comparisons = (int.from_bytes(run.stdout[i:i+4], 'little') for i in [0,4])
        assert run.returncode==0 and failures==0, (run.returncode,failures,comparisons)
        print(f'PASS ARM11 filter: {comparisons} PCM/history/production-state comparisons; halfword alignment and canaries')


if __name__ == '__main__':
    main()
