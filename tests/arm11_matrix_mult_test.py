#!/usr/bin/env python3
"""Exact ARM11 test for the SkinMatrix 4x4 multiply replacement."""
from pathlib import Path
import re
import os
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
DEVKITPRO = Path(os.environ.get("DEVKITPRO", str(Path.home() / "dkp-root/opt/devkitpro")))
CC = DEVKITPRO / "devkitARM/bin/arm-none-eabi-gcc"
OBJDUMP = DEVKITPRO / "devkitARM/bin/arm-none-eabi-objdump"
KERNEL = ROOT / "src/compat3ds/arm11/skin_matrix_mult.S"


def function_body(source: str, name: str) -> str:
    matches = list(re.finditer(r"^void " + name + r"\([^;{}]*\) \{", source, re.M))
    assert matches, name
    match = matches[-1]
    start = source.index("{", match.start())
    pos, depth = start + 1, 1
    while depth:
        depth += (source[pos] == "{") - (source[pos] == "}")
        pos += 1
    return source[start:pos]


def main() -> None:
    assert KERNEL.exists(), f"missing production kernel: {KERNEL}"
    game = (ROOT / "third_party/shipwright/soh/src/code/z_skin_matrix.c").read_text()
    reference = function_body(game, "SkinMatrix_MtxFMtxFMult")
    code = r'''
#include <stdint.h>
typedef float f32;
typedef struct { float xx,yx,zx,wx,xy,yy,zy,wy,xz,yz,zz,wz,xw,yw,zw,ww; } MtxF;
void Soh3dsSkinMatrixMultArm11(MtxF*, MtxF*, MtxF*);
int Soh3dsSkinMatrixMultAbi(MtxF*, MtxF*, MtxF*);
__attribute__((noinline)) void reference(MtxF* mfA, MtxF* mfB, MtxF* dest)
''' + reference + r'''
__attribute__((noinline)) void brokenReference(MtxF* a,MtxF* b,MtxF* d){
 for(unsigned col=0;col<4;col++)for(unsigned row=0;row<4;row++){
  float* x=(float*)a,*y=(float*)b,*z=(float*)d;
  z[col*4+row]=x[row]*y[col*4]+x[4+row]*y[col*4+1]+x[8+row]*y[col*4+2];
 }
}
static uint32_t state=0x91e10da5u;
static uint32_t failures[6];
static uint32_t rnd(void){state=state*1664525u+1013904223u;return state;}
static int equal(const void* a,const void* b,unsigned n){
 const unsigned char* x=a;const unsigned char* y=b;
 for(unsigned i=0;i<n;i++)if(x[i]!=y[i])return 0;return 1;
}
static int matrixEqual(const uint32_t* a,const uint32_t* b,unsigned first){
 for(unsigned i=0;i<16;i++)if(a[first+i]!=b[first+i]){
  // NaN payload/sign selection is not specified by C and GCC itself chooses
  // different multiply accumulators among the 16 otherwise identical terms.
  uint32_t x=a[first+i]&0x7fffffffu,y=b[first+i]&0x7fffffffu;
  if(x<=0x7f800000u||y<=0x7f800000u)return 0;
 }
 return 1;
}
static uint32_t a[48],b[48],x[48],y[48];
static void correctness(void){
 static const uint32_t edges[16]={
  0x00000000,0x80000000,0x00000001,0x80000001,
  0x007fffff,0x807fffff,0x00800000,0x80800000,
  0x3f800000,0xbf800000,0x7f7fffff,0xff7fffff,
  0x7f800000,0xff800000,0x7fc12345,0xff812345
 };
 for(unsigned i=0;i<48;i++)a[i]=x[i]=edges[i&15],b[i]=y[i]=edges[(i*5+3)&15];
 reference((MtxF*)(a+8),(MtxF*)(b+8),(MtxF*)(a+24));
 Soh3dsSkinMatrixMultArm11((MtxF*)(x+8),(MtxF*)(y+8),(MtxF*)(x+24));
 if(!matrixEqual(a,x,24))failures[0]++;
 // Negative control: deleting the fourth product must be observable.
 for(unsigned i=0;i<48;i++)a[i]=x[i]=0,b[i]=y[i]=0;
 for(unsigned i=0;i<16;i++)a[8+i]=x[8+i]=b[8+i]=y[8+i]=0x3f800000;
 reference((MtxF*)(a+8),(MtxF*)(b+8),(MtxF*)(a+24));
 brokenReference((MtxF*)(x+8),(MtxF*)(y+8),(MtxF*)(x+24));
 if(equal(a+24,x+24,64))failures[0]++;
 if(Soh3dsSkinMatrixMultAbi((MtxF*)(a+8),(MtxF*)(b+8),(MtxF*)(a+24)))failures[0]++;
 for(unsigned trial=0;trial<32768;trial++){
  for(unsigned i=0;i<48;i++)a[i]=x[i]=rnd(),b[i]=y[i]=rnd();
  // Half the cases are ordinary finite gameplay-like values. The rest cover
  // every float encoding class, including NaN payloads, infinities and zero.
  if(trial<16384)for(unsigned i=0;i<48;i++){
   union{float f;uint32_t u;}v;v.f=(float)((int32_t)(rnd()&0xffff)-32768)/256.0f;
   a[i]=x[i]=v.u;v.f=(float)((int32_t)(rnd()&0xffff)-32768)/512.0f;b[i]=y[i]=v.u;
  }
  MtxF* ad=(MtxF*)(a+8);MtxF* bd=(MtxF*)(b+8);
  MtxF* ax=(MtxF*)(x+8);MtxF* bx=(MtxF*)(y+8);
  // The game depends on mfA == dest; mfB == dest is excluded by the C API.
  MtxF* rd=(trial&1)?ad:(MtxF*)(a+24);
  MtxF* od=(trial&1)?ax:(MtxF*)(x+24);
  reference(ad,bd,rd);Soh3dsSkinMatrixMultArm11(ax,bx,od);
  unsigned first=(trial&1)?8:24;
  int guards=(first==8)?equal(a,x,8*4)&&equal(a+24,x+24,24*4):equal(a,x,24*4)&&equal(a+40,x+40,8*4);
  if(!guards||!matrixEqual(a,x,first)||!equal(b,y,sizeof(b))){
   failures[0]++;
   if(!failures[1])for(unsigned i=0;i<48;i++)if(a[i]!=x[i]){
    failures[1]=trial+1;failures[2]=i;failures[3]=a[i];failures[4]=x[i];break;
   }
  }
 }
}
void __aeabi_unwind_cpp_pr0(void){} void __aeabi_unwind_cpp_pr1(void){}
void _start(void){
 correctness();
 register uint32_t r0 __asm__("r0")=1;register void* r1 __asm__("r1")=failures;
 register uint32_t r2 __asm__("r2")=20;
 __asm__ volatile("mov r7,#4\nsvc #0":"+r"(r0):"r"(r1),"r"(r2):"r7","memory");
 r0=failures[0]!=0;__asm__ volatile("mov r7,#1\nsvc #0"::"r"(r0):"r7","memory");__builtin_unreachable();
}
'''
    with tempfile.TemporaryDirectory(prefix="soh-matrix-mult-") as directory:
        path = Path(directory)
        (path / "test.c").write_text(code)
        elf = path / "test"
        subprocess.run([
            str(CC), "-O2", "-march=armv6k", "-mtune=mpcore", "-mfpu=vfp",
            "-mfloat-abi=hard", "-fno-fast-math", "-ffp-contract=off",
            "-fno-builtin", "-nostdlib", "-Wl,-e,_start", "-Wl,-Ttext=0x10000",
            str(path / "test.c"), str(KERNEL), str(ROOT / "tests/fixtures/skin_matrix_mult_abi.S"), "-o", str(elf),
        ], check=True)
        disassembly = subprocess.check_output([str(OBJDUMP), "-d", str(elf)], text=True)
        run = subprocess.run(["qemu-arm", "-cpu", "arm11mpcore", str(elf)], stdout=subprocess.PIPE)
        assert run.returncode == 0 and run.stdout == bytes(20), (run.returncode, run.stdout.hex())
        kernel = disassembly.split("<Soh3dsSkinMatrixMultArm11>:", 1)[1].split("\n\n", 1)[0]
        reference_disassembly = disassembly.split("<reference>:", 1)[1].split("\n\n", 1)[0]
        assert "Soh3dsSkinMatrixMultArm11" not in reference_disassembly, reference_disassembly
        assert not re.search(r"\b(vfma|vld[1-4]|vst[1-4]|q[0-9]+)\b", kernel, re.I), kernel
        assert "vpush\t{d8-d15}" in kernel and "vpop\t{d8-d15}" in kernel, kernel
    print("PASS: 32768 exact ARM11 matrix products (NaN payload ignored); supported alias and guards")


if __name__ == "__main__":
    main()
