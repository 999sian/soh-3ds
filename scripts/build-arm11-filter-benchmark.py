#!/usr/bin/env python3
"""Compare production C/ARM11 filter wrappers, correctness before timing."""
from pathlib import Path
import hashlib
import importlib.util
import json
import os
import subprocess

root = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('filter_test', root/'tests/audio_filter_arm11_test.py')
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)
out = root/'builds/arm11-filter-benchmark'
out.mkdir(parents=True, exist_ok=True)
code = '#include <3ds.h>\n#include <stdio.h>\n' + module.harness() + r'''
typedef void (*FilterFn)(uint8_t,uint16_t,int16_t*);
static volatile uint32_t checksum;
static uint64_t measure(FilterFn volatile fn,unsigned samples,unsigned offset,unsigned iterations){
 // Identical initial PCM/state/coefficients for both paths, outside timing.
 randomState=456;
 memset(&refRspa,0,sizeof(refRspa));
 for(unsigned i=0;i<DMEM_BUF_SIZE/2;i++)refRspa.buf.as_s16[i]=(int16_t)rnd();
 memcpy(&asmRspa,&refRspa,sizeof(refRspa));
 for(unsigned i=0;i<20;i++)stateC[i]=stateAsm[i]=(int16_t)rnd();
 for(unsigned i=0;i<12;i++)coeffs[i]=(int16_t)(rnd()>>3);
 int16_t* state=(fn==filterC?stateC:stateAsm)+offset;
 fn(2,samples*2,coeffs+offset);
 uint64_t begin=svcGetSystemTick();
 for(unsigned i=0;i<iterations;i++)fn(0,0x500+offset*2,state);
 uint64_t elapsed=svcGetSystemTick()-begin;
 checksum^=(uint16_t)state[0];return elapsed;
}
int main(void){
 gfxInitDefault();consoleInit(GFX_TOP,NULL);
 bool newModel=false;APT_CheckNew3DS(&newModel);osSetSpeedupEnable(newModel);
 printf("ARM11 eight-tap filter benchmark\nModel: %s\nChecking PCM and saved state...\n",newModel?"New 3DS":"Old 3DS");
 gfxFlushBuffers();gfxSwapBuffers();gspWaitForVBlank();
 correctness();
 printf("%lu checks, %lu failures\n",(unsigned long)comparisons,(unsigned long)failures);
 FILE* file=fopen("sdmc:/3ds/soh/arm11-filter-benchmark.csv","w");
 if(file){
  fprintf(file,"# model=%s core=%ld correctness_failures=%lu comparisons=%lu pointer_guards=valid_fixture\n",
          newModel?"new":"old",(long)svcGetProcessorID(),(unsigned long)failures,(unsigned long)comparisons);
  fprintf(file,"kernel,samples,halfword_offset,round,iterations,c_ticks,asm_ticks\n");
 }
 if(!failures){
  for(unsigned round=0;round<5;round++)for(unsigned mode=0;mode<4;mode++)for(unsigned offset=0;offset<2;offset++){
   unsigned samples=(unsigned[]){8,64,128,544}[mode],iterations=samples>=512?512:2048;
   // Warm both implementations; alternate order to reduce systematic bias.
   measure(filterC,samples,offset,32);measure(filterAsm,samples,offset,32);
   uint64_t c,a;
   if(round&1){a=measure(filterAsm,samples,offset,iterations);c=measure(filterC,samples,offset,iterations);}
   else{c=measure(filterC,samples,offset,iterations);a=measure(filterAsm,samples,offset,iterations);}
   printf("%lu/%lu %lu: C %llu ASM %llu\n",(unsigned long)samples,(unsigned long)offset,
          (unsigned long)round,(unsigned long long)c,(unsigned long long)a);
   if(file)fprintf(file,"filter,%u,%u,%u,%u,%llu,%llu\n",samples,offset,round,iterations,
                   (unsigned long long)c,(unsigned long long)a);
   gfxFlushBuffers();gfxSwapBuffers();gspWaitForVBlank();
  }
 }
 if(file)fclose(file);
 printf("\nDone. %lu failures.\n%s\nSTART to exit.\n",(unsigned long)failures,
        file?"Saved arm11-filter-benchmark.csv":"Could not write CSV to /3ds/soh");
 while(aptMainLoop()){
  hidScanInput();if(hidKeysDown()&KEY_START)break;
  gfxFlushBuffers();gfxSwapBuffers();gspWaitForVBlank();
 }
 gfxExit();return failures?1:0;
}
'''
(out/'benchmark.c').write_text(code)
dkp = Path(os.environ.get('DEVKITPRO', '/home/sian/dkp-root/opt/devkitpro'))
subprocess.run([str(dkp/'devkitARM/bin/arm-none-eabi-gcc'), '-O2', '-march=armv6k', '-mtune=mpcore',
                '-mfpu=vfp', '-mfloat-abi=hard', '-mtp=soft', '-mword-relocations', '-fno-fast-math',
                '-ffp-contract=off', '-D__3DS__', '-DARM11', '-I'+str(root/'src'),
                '-I'+str(dkp/'libctru/include'), '-specs='+str(dkp/'devkitARM/arm-none-eabi/lib/3dsx.specs'),
                str(out/'benchmark.c'), str(root/'src/compat3ds/arm11/audio_filter.S'),
                str(root/'tests/fixtures/filter_arm11_abi.S'),
                '-L'+str(dkp/'libctru/lib'), '-lctru', '-lm', '-o', str(out/'soh-arm11-filter-benchmark.elf')], check=True)
subprocess.run([str(dkp/'tools/bin/3dsxtool'), str(out/'soh-arm11-filter-benchmark.elf'),
                str(out/'soh-arm11-filter-benchmark.3dsx')], check=True)
(out/'manifest.json').write_text(json.dumps({p.name:hashlib.sha256(p.read_bytes()).hexdigest()
    for p in out.iterdir() if p.suffix in ['.c','.elf','.3dsx']},indent=2)+'\n')
print(out/'soh-arm11-filter-benchmark.3dsx')
