#!/usr/bin/env python3
"""Build a standalone 3DS benchmark: correctness first, two timer reads per batch."""
from pathlib import Path
import importlib.util,subprocess,json,hashlib,os
root=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('kernels_test',root/'tests/arm11_kernels_test.py')
module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
out=root/'builds/arm11-benchmark';out.mkdir(parents=True,exist_ok=True)
code='#include <3ds.h>\n#include <stdio.h>\n#include <inttypes.h>\n'+module.harness()
code=code.replace('__attribute__((noinline)) void Soh3dsMatrixCopyReference','__attribute__((noinline,optimize("Os"))) void Soh3dsMatrixCopyReference')
code+=r'''
typedef int16_t* (*Resampler)(int16_t*,int16_t*,uint32_t*,uint32_t,uint32_t,const int16_t[64][4]);
typedef void (*MatrixCopy)(MtxF*,MtxF*);
static volatile unsigned checksum;
static uint64_t timeResample(Resampler volatile fn,unsigned count,unsigned offset){
 uint64_t start=svcGetSystemTick();
 for(unsigned i=0;i<2048;i++){
  uint32_t phase=i&65535;
  fn(input+offset,actual,&phase,0xd231,count,resample_table);
 }
 uint64_t elapsed=svcGetSystemTick()-start;
 checksum^=(unsigned)(uint16_t)actual[0];return elapsed;
}
static uint64_t timeMatrix(MatrixCopy volatile fn){
 uint64_t start=svcGetSystemTick();
 for(unsigned i=0;i<131072;i++)fn((MtxF*)(matrixB+16),(MtxF*)(matrixA+16));
 uint64_t elapsed=svcGetSystemTick()-start;
 checksum^=matrixB[16];return elapsed;
}
static void result(FILE* f,const char* kernel,unsigned count,unsigned offset,int round,
                   uint64_t c,uint64_t a,unsigned iterations){
 printf("%s %u: C %llu / ASM %llu ticks\n",kernel,count,(unsigned long long)c,(unsigned long long)a);
 if(f)fprintf(f,"%s,%u,%u,%d,%u,%llu,%llu\n",kernel,count,offset,round,iterations,
              (unsigned long long)c,(unsigned long long)a);
}
int main(void){
 gfxInitDefault();consoleInit(GFX_TOP,NULL);
 bool newModel=false;APT_CheckNew3DS(&newModel);
 // Use the normal clock for the detected model. Old hardware stays at 268 MHz.
 osSetSpeedupEnable(newModel);
 printf("ARM11 handwritten kernel benchmark\nModel: %s\nChecking correctness first...\n",newModel?"New 3DS":"Old 3DS");
 gfxFlushBuffers();gfxSwapBuffers();gspWaitForVBlank();
 correctness();
 printf("Correctness failures: %d\n",failures);
 FILE* f=fopen("sdmc:/3ds/soh/arm11-benchmark.csv","w");
 if(f){fprintf(f,"# model=%s correctness_failures=%d\n",newModel?"new":"old",failures);
 fprintf(f,"kernel,samples,input_halfword_offset,round,iterations,c_ticks,asm_ticks\n");}
 if(!failures){
  // Warm both instruction paths. Report five alternating-order rounds; SD
  // writes and screen output happen only outside the measured intervals.
  timeResample(resampleReference,544,0);timeResample(Soh3dsResampleArm11,544,0);
  timeMatrix(Soh3dsMatrixCopyReference);timeMatrix(Matrix_MtxFCopy);
  for(int round=0;round<5;round++){
   for(unsigned offset=0;offset<2;offset++)for(unsigned mode=0;mode<2;mode++){
    unsigned count=mode?544:512;uint64_t c,a;
    if(round&1){a=timeResample(Soh3dsResampleArm11,count,offset);c=timeResample(resampleReference,count,offset);}
    else{c=timeResample(resampleReference,count,offset);a=timeResample(Soh3dsResampleArm11,count,offset);}
    result(f,"resample",count,offset,round,c,a,2048);
   }
   uint64_t c,a;
   if(round&1){a=timeMatrix(Matrix_MtxFCopy);c=timeMatrix(Soh3dsMatrixCopyReference);}
   else{c=timeMatrix(Soh3dsMatrixCopyReference);a=timeMatrix(Matrix_MtxFCopy);}
   result(f,"matrix_copy",16,0,round,c,a,131072);
  }
 }
 if(f)fclose(f);
 printf("\nDone. Results: /3ds/soh/arm11-benchmark.csv\nSTART to exit.\n");
 while(aptMainLoop()){
  hidScanInput();if(hidKeysDown()&KEY_START)break;
  gfxFlushBuffers();gfxSwapBuffers();gspWaitForVBlank();
 }
 gfxExit();return failures?1:0;
}
'''
(out/'benchmark.c').write_text(code)
dkp=Path(os.environ.get('DEVKITPRO','/home/sian/dkp-root/opt/devkitpro'))
subprocess.run([str(dkp/'devkitARM/bin/arm-none-eabi-gcc'),'-O2','-march=armv6k','-mtune=mpcore','-mfpu=vfp','-mfloat-abi=hard','-mtp=soft','-mword-relocations','-fno-fast-math','-ffp-contract=off','-D__3DS__','-DARM11','-I'+str(root/'src'),'-I'+str(dkp/'libctru/include'),'-specs='+str(dkp/'devkitARM/arm-none-eabi/lib/3dsx.specs'),str(out/'benchmark.c'),str(root/'src/compat3ds/arm11/resample.S'),str(root/'src/compat3ds/arm11/matrix_copy.S'),'-L'+str(dkp/'libctru/lib'),'-lctru','-lm','-o',str(out/'soh-arm11-benchmark.elf')],check=True)
subprocess.run([str(dkp/'tools/bin/3dsxtool'),str(out/'soh-arm11-benchmark.elf'),str(out/'soh-arm11-benchmark.3dsx')],check=True)
manifest={p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in out.iterdir() if p.suffix in ('.elf','.3dsx','.c')}
(out/'manifest.json').write_text(json.dumps(manifest,indent=2))
print(out/'soh-arm11-benchmark.3dsx')
