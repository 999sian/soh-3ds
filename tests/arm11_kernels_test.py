#!/usr/bin/env python3
"""Run handwritten kernels on qemu's ARM11; compare C results and memory.
QEMU is used for ISA/correctness only. Physical timing uses the 3DS benchmark.
"""
from pathlib import Path
import subprocess,tempfile,re
ROOT=Path(__file__).resolve().parents[1]
SDK=Path('/home/sian/dkp-root/opt/devkitpro/devkitARM/bin')
def body(s,name):
 a=re.search(r'^void '+name+r'\([^;{}]*\) \{',s,re.M).start();a=s.index('{',a);i=a+1;depth=1
 while depth:
  depth+=(s[i]=='{')-(s[i]=='}');i+=1
 return s[a:i]
def harness():
 mixer=(ROOT/'third_party/shipwright/soh/soh/mixer.c').read_text()
 table=mixer[mixer.index('static int16_t resample_table'):mixer.index('static void aMixImplSSE2')]
 table=table.replace('resample_table[64][4]', 'resample_table[64][4] __attribute__((aligned(4)))')
 matrix=(ROOT/'third_party/shipwright/soh/src/code/sys_matrix.c').read_text()
 return r'''
#include <stdint.h>
#include <stddef.h>
#include "compat3ds/arm11/kernels.h"
typedef struct { float xx,yx,zx,wx,xy,yy,zy,wy,xz,yz,zz,wz,xw,yw,zw,ww; } MtxF;
void Matrix_MtxFCopy(MtxF*,MtxF*);
__attribute__((noinline)) void Soh3dsMatrixCopyReference(MtxF* dest,MtxF* src)
'''+body(matrix,'Matrix_MtxFCopy')+ '\n'+table+r'''
static int failures;
static uint32_t randomState=123;
static uint32_t rnd(void){randomState=randomState*1664525u+1013904223u;return randomState;}
static void check(int value){if(!value)++failures;}
static int equal(const void* a,const void* b,unsigned size){
 const unsigned char* x=a;const unsigned char* y=b;
 for(unsigned i=0;i<size;i++)if(x[i]!=y[i])return 0;return 1;
}
static void copy(void* a,const void* b,unsigned size){
 unsigned char* x=a;const unsigned char* y=b;for(unsigned i=0;i<size;i++)x[i]=y[i];
}
__attribute__((noinline)) int16_t* resampleReference(int16_t* in,int16_t* out,uint32_t* phase,
 uint32_t step,uint32_t count,const int16_t table[64][4]){
 uint32_t p=*phase;
 while(count--){
  const int16_t* tbl=table[p>>10];
  int sample=((in[0]*tbl[0]+0x4000)>>15)+((in[1]*tbl[1]+0x4000)>>15)+
             ((in[2]*tbl[2]+0x4000)>>15)+((in[3]*tbl[3]+0x4000)>>15);
  *out++=sample<-32768?-32768:(sample>32767?32767:sample);
  p+=step;in+=p>>16;p&=65535;
 }
 *phase=p;return in;
}
static int16_t input[4096] __attribute__((aligned(4)));
static int16_t expected[1040],actual[1040];
static uint32_t matrixA[64],matrixB[64];
static void correctness(void){
 // Every table phase; saturated, impulse, alternating extrema and random PCM.
 for(int trial=0;trial<8192;trial++){
  for(unsigned j=0;j<4096;j++) input[j]=trial<64?32767:trial<128?-32768:trial<192?(j&1?32767:-32768):(int16_t)rnd();
  for(unsigned j=0;j<1040;j++)expected[j]=actual[j]=0x1234;
  uint32_t p=(trial%64)*1024+(rnd()&1023),q=p;
  uint32_t step=trial<256?(uint32_t[]){0,1,65536,131070}[trial%4]:(rnd()&65535)*2;
  uint32_t count=trial<64?trial:((rnd()%512)+1);
  int16_t* start=input+(trial&1); // Includes 2-byte but not 4-byte alignment.
  int16_t* a=resampleReference(start,expected+2,&p,step,count,resample_table);
  int16_t* b=Soh3dsResampleArm11(start,actual+2,&q,step,count,resample_table);
  check(a==b && p==q && equal(expected,actual,sizeof(actual)));
 }
 // Copy exact bits (all float encodings), including every partial overlap.
 for(int trial=0;trial<256;trial++)for(int offset=-16;offset<=16;offset++){
  for(int j=0;j<64;j++)matrixA[j]=matrixB[j]=rnd();
  Soh3dsMatrixCopyReference((MtxF*)(matrixA+24+offset),(MtxF*)(matrixA+24));
  Matrix_MtxFCopy((MtxF*)(matrixB+24+offset),(MtxF*)(matrixB+24));
  check(equal(matrixA,matrixB,sizeof(matrixA)));
 }
}
'''
def main():
 with tempfile.TemporaryDirectory(prefix='soh-arm11-') as d:
  p=Path(d)
  code=harness()+r'''
void __aeabi_unwind_cpp_pr0(void){}
void _start(void){
 correctness();
 register uint32_t r0 __asm__("r0")=1;
 register void* r1 __asm__("r1")=&failures;
 register uint32_t r2 __asm__("r2")=4;
 __asm__ volatile("mov r7,#4\nsvc #0":"+r"(r0):"r"(r1),"r"(r2):"r7","memory");
 r0=failures!=0;__asm__ volatile("mov r7,#1\nsvc #0"::"r"(r0):"r7","memory");__builtin_unreachable();
}
'''
  (p/'test.c').write_text(code)
  cmd=[str(SDK/'arm-none-eabi-gcc'),'-O2','-march=armv6k','-mfpu=vfp','-mfloat-abi=hard','-fno-fast-math','-ffp-contract=off','-fno-builtin','-nostdlib','-Wl,-e,_start','-Wl,-Ttext=0x10000','-I'+str(ROOT/'src'),str(p/'test.c'),str(ROOT/'src/compat3ds/arm11/resample.S'),str(ROOT/'src/compat3ds/arm11/matrix_copy.S'),'-o',str(p/'test')]
  subprocess.run(cmd,check=True)
  run=subprocess.run(['qemu-arm','-cpu','arm11mpcore',str(p/'test')],stdout=subprocess.PIPE)
  result=run.stdout
  assert run.returncode==0,(run.returncode,result.hex())
  assert result==b'\0'*4,result
 print('PASS: ARM11 resampler 8192 PCM/phase cases; game matrix 8448 bit-exact overlap cases')
if __name__=='__main__':main()
