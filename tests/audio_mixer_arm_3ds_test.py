#!/usr/bin/env python3
"""Compare production resampling, envelope and mixing PCM on ARM11 at -Os/-O2."""
from pathlib import Path
import subprocess,tempfile
root=Path(__file__).resolve().parents[1]
s=(root/'third_party/shipwright/soh/soh/mixer.c').read_text()
reference_clamp='static inline int16_t clamp16(int32_t v) { return v < -32768 ? -32768 : (v > 32767 ? 32767 : (int16_t)v); }\n'
def function(text,name):
 a=text.index(name); a=text.rfind('\n',0,a)+1;b=text.index('{',a);depth=1;i=b+1
 while depth:
  depth+=(text[i]=='{')-(text[i]=='}');i+=1
 return text[a:i]+'\n'
prefix=r'''
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <arm_acle.h>
#include "compat3ds/arm11/kernels.h"
void __aeabi_unwind_cpp_pr0(void){}
void __aeabi_unwind_cpp_pr1(void){}
typedef int16_t ADPCM_STATE[16], RESAMPLE_STATE[16];
typedef uint32_t u32;
#define A_INIT 1
#define A_LOOP 2
#define SOH3DS_AUDIO_GUARD(a,b,c) 1
void* memcpy(void* d,const void* s,size_t n) { for(size_t i=0;i<n;i++) ((char*)d)[i]=((const char*)s)[i]; return d; }
void* memset(void* d,int c,size_t n) { for(size_t i=0;i<n;i++) ((char*)d)[i]=c; return d; }
'''
prefix+=s[s.index('#define ROUND_UP_64'):s.index('#ifdef __3DS__',s.index('#define ROUND_UP_64'))]
prefix+=s[s.index('static struct {'):s.index('static void aMixImplSSE2')]
body=''.join(function(s,x) for x in ['void aADPCMdecImpl(', 'void aResampleImpl(', 'void aEnvSetup1Impl(', 'void aEnvSetup2Impl(', 'void aEnvMixerImpl(', 'static void aMixImplRef(', 'void aMixImpl('])
run=r'''
uint32_t randomState=123;
uint32_t rnd(void) { randomState=randomState*1664525u+1013904223u;return randomState; }
void _start(void) {
 uint32_t hash=2166136261u;
 for(int trial=0;trial<400;trial++) {
  for(unsigned i=0;i<sizeof(rspa.buf.as_s16)/2;i++) rspa.buf.as_s16[i]=(int)(rnd()&16383)-8192;
  rspa.in=0x500;rspa.out=0xa00;rspa.nbytes=128;
  ADPCM_STATE adpcm={0};
  for(unsigned i=0;i<sizeof(rspa.adpcm_table)/2;i++) ((int16_t*)rspa.adpcm_table)[i]=(int16_t)rnd();
  // Legal table selectors, both ADPCM widths and all supported shifts.
  for(int block=0;block<4;block++) *BUF_U8(0x500 + block*(trial&1?5:9))=((trial&7)<<4)|(block&7);
  aADPCMdecImpl(A_INIT | (trial&1?4:0),adpcm);
  aADPCMdecImpl(trial&1?4:0,adpcm);
  RESAMPLE_STATE state={0};
  aResampleImpl(A_INIT,(uint16_t)rnd(),state);
  aResampleImpl(0,(uint16_t)rnd(),state);
  aEnvSetup1Impl(rnd(),rnd(),rnd(),rnd());aEnvSetup2Impl(rnd(),rnd());
  aEnvMixerImpl(0x500,128,trial&1,trial&2,trial&4,trial&8,trial&16,0x7090b0d0,0);
  aMixImpl(16,trial&1 ? -32768 : (int16_t)rnd(),0x500,0x700);
  for(unsigned i=0;i<sizeof(rspa.buf);i++) hash=(hash^rspa.buf.as_u8[i])*16777619u;
  for(unsigned i=0;i<16;i++) hash=(hash^(uint16_t)adpcm[i])*16777619u;
  for(unsigned i=0;i<16;i++) hash=(hash^(uint16_t)state[i])*16777619u;
 }
 register unsigned r0 __asm__("r0")=1;
 register void* r1 __asm__("r1")=&hash;
 register unsigned r2 __asm__("r2")=sizeof(hash);
 __asm__ volatile("mov r7,#4\nsvc #0":"+r"(r0):"r"(r1),"r"(r2):"r7","memory");
 r0=0;__asm__ volatile("mov r7,#1\nsvc #0"::"r"(r0):"r7","memory");__builtin_unreachable();
}
'''
sdk=Path('/home/sian/dkp-root/opt/devkitpro/devkitARM/bin');outputs=[]
with tempfile.TemporaryDirectory() as t:
 p=Path(t)
 for label,clamp,opt in [('reference',reference_clamp,'-Os'),('candidate',function(s,'static inline int16_t clamp16('),'-O2'),('profile',function(s,'static inline int16_t clamp16('),'-O2')]:
  profile = '''
#define SOH3DS_AUDIO_PROFILE
#include "ship/utils/audio_profile_3ds.h"
bool gSoh3dsAudioProfileActive = true;
uint32_t gSoh3dsAudioProfileOps[SOH3DS_AUDIO_OP_COUNT] = {0};
''' if label=='profile' else ''
  (p/(label+'.c')).write_text(prefix+profile+clamp+body+run)
  asm_args=['-DSOH3DS_DISABLE_ARM11_ASM'] if label=='reference' else [str(root/'src/compat3ds/arm11/resample.S'),str(root/'src/compat3ds/arm11/audio_mix.S')]
  subprocess.run([str(sdk/'arm-none-eabi-gcc'),opt,'-I'+str(root/'src'),'-I'+str(root/'third_party/libultraship/include'),*asm_args,'-D__3DS__','-march=armv6k','-mfloat-abi=soft','-fno-builtin','-nostdlib','-Wl,-e,_start','-Wl,-Ttext=0x10000',str(p/(label+'.c')),'-o',str(p/label)],check=True)
  outputs.append(subprocess.check_output(['qemu-arm','-cpu','arm11mpcore',str(p/label)]))
 assert len(outputs[0])==4 and all(output==outputs[0] for output in outputs[1:]),outputs
 print('ARM11 PCM/state match for 400 randomized sequences, including active profile counters:',outputs[0].hex())
