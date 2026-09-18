#!/usr/bin/env python3
"""Build the standalone correctness-first 3DS matrix multiply benchmark."""
from pathlib import Path
import hashlib
import importlib.util
import json
import os
import subprocess

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("matrix_test", ROOT / "tests/arm11_matrix_mult_test.py")
test = importlib.util.module_from_spec(spec); spec.loader.exec_module(test)
game = (ROOT / "third_party/shipwright/soh/src/code/z_skin_matrix.c").read_text()
reference = test.function_body(game, "SkinMatrix_MtxFMtxFMult")
OUT = ROOT / "builds/arm11-matrix-mult-benchmark"
OUT.mkdir(parents=True, exist_ok=True)

code = r'''
#include <3ds.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdint.h>
typedef float f32;
typedef struct {float xx,yx,zx,wx,xy,yy,zy,wy,xz,yz,zz,wz,xw,yw,zw,ww;} MtxF;
void Soh3dsSkinMatrixMultArm11(MtxF*,MtxF*,MtxF*);
__attribute__((noinline,optimize("O2"))) void matrixReference(MtxF* mfA,MtxF* mfB,MtxF* dest)
''' + reference + r'''
static MtxF a,b,out;
static volatile uint32_t checksum;
static uint32_t state=0x91e10da5u;
static uint32_t rnd(void){state=state*1664525u+1013904223u;return state;}
static int same(const void* x,const void* y,unsigned n){
 const unsigned char* a=x,*b=y;for(unsigned i=0;i<n;i++)if(a[i]!=b[i])return 0;return 1;
}
typedef void (*Fn)(MtxF*,MtxF*,MtxF*);
static int correctness(void){
 for(unsigned trial=0;trial<8192;trial++){
  for(unsigned i=0;i<16;i++){
   ((float*)&a)[i]=(float)((int32_t)(rnd()&65535)-32768)/256.0f;
   ((float*)&b)[i]=(float)((int32_t)(rnd()&65535)-32768)/512.0f;
  }
  MtxF expected=a,actual=a;
  if(trial&1){matrixReference(&expected,&b,&expected);Soh3dsSkinMatrixMultArm11(&actual,&b,&actual);}
  else{matrixReference(&a,&b,&expected);Soh3dsSkinMatrixMultArm11(&a,&b,&actual);}
  if(!same(&expected,&actual,sizeof(expected)))return 1;
 }
 return 0;
}
static uint64_t timed(Fn volatile fn){
 uint64_t start=svcGetSystemTick();
 for(unsigned i=0;i<131072;i++)fn(&a,&b,&out);
 uint64_t elapsed=svcGetSystemTick()-start;
 checksum^=((uint32_t*)&out)[0];
 return elapsed;
}
int main(void){
 gfxInitDefault();consoleInit(GFX_TOP,NULL);bool newModel=false;APT_CheckNew3DS(&newModel);osSetSpeedupEnable(newModel);
 int failed=correctness();printf("Matrix multiply correctness failures: %d\n",failed);
 FILE* f=fopen("sdmc:/3ds/soh/arm11-matrix-mult-benchmark.csv","w");
 if(f)fprintf(f,"# model=%s correctness_failures=%d cases=8192 iterations=131072\nround,order,c_ticks,asm_ticks\n",newModel?"new":"old",failed);
 if(!failed){
  for(unsigned i=0;i<16;i++)((float*)&a)[i]=(float)((int)i-8)/8.0f,((float*)&b)[i]=(float)(15-i)/16.0f;
  if(((float*)&a)[0]!=-1.0f||((float*)&a)[8]!=0.0f||((float*)&a)[15]!=0.875f){
   printf("Timing input range check failed.\n");failed=1;goto finished;
  }
  timed(matrixReference);timed(Soh3dsSkinMatrixMultArm11);
  for(int round=0;round<7;round++){
   uint64_t c,s;if(round&1){s=timed(Soh3dsSkinMatrixMultArm11);c=timed(matrixReference);}
   else{c=timed(matrixReference);s=timed(Soh3dsSkinMatrixMultArm11);}
   printf("%d: C %llu ASM %llu\n",round,(unsigned long long)c,(unsigned long long)s);
   if(f)fprintf(f,"%d,%s,%llu,%llu\n",round,round&1?"asm-first":"c-first",(unsigned long long)c,(unsigned long long)s);
  }
 }
finished:
 if(f)fclose(f);printf("Done. START exits.\n");
 while(aptMainLoop()){hidScanInput();if(hidKeysDown()&KEY_START)break;gfxFlushBuffers();gfxSwapBuffers();gspWaitForVBlank();}
 gfxExit();return failed;
}
'''
(OUT / "benchmark.c").write_text(code)
dkp = Path(os.environ.get("DEVKITPRO", "/home/sian/dkp-root/opt/devkitpro"))
elf = OUT / "soh-arm11-matrix-mult-benchmark.elf"
subprocess.run([str(dkp / "devkitARM/bin/arm-none-eabi-gcc"), "-O2", "-march=armv6k", "-mtune=mpcore",
 "-mfpu=vfp", "-mfloat-abi=hard", "-mtp=soft", "-mword-relocations", "-fno-fast-math", "-ffp-contract=off",
 "-D__3DS__", "-DARM11", "-I"+str(dkp / "libctru/include"), "-specs="+str(dkp / "devkitARM/arm-none-eabi/lib/3dsx.specs"),
 str(OUT / "benchmark.c"), str(ROOT / "src/compat3ds/arm11/skin_matrix_mult.S"), "-L"+str(dkp / "libctru/lib"),
 "-lctru", "-lm", "-o", str(elf)], check=True)
three = OUT / "soh-arm11-matrix-mult-benchmark.3dsx"
subprocess.run([str(dkp / "tools/bin/3dsxtool"), str(elf), str(three)], check=True)
manifest = {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in (elf, three, OUT / "benchmark.c")}
(OUT / "manifest.json").write_text(json.dumps(manifest, indent=2)+"\n")
print(three)
