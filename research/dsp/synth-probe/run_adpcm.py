#!/usr/bin/env python3
from pathlib import Path
import subprocess,json,hashlib
root=Path(__file__).resolve().parents[3];here=Path(__file__).resolve().parent;out=root/'builds/dsp-synth-probe-20260909'
src=(root/'third_party/shipwright/soh/soh/mixer.c').read_text();a=src.index('void aADPCMdecImpl(');body=src[a:src.index('\nvoid aResampleImpl(',a)]
code='''#include <stdint.h>
#include <string.h>
typedef int16_t ADPCM_STATE[16];
static int16_t* memory;
static struct {uint16_t in,out,nbytes;int16_t adpcm_table[8][2][8];ADPCM_STATE* adpcm_loop_state;} rspa;
#define BUF_U8(a) ((uint8_t*)memory+(a))
#define BUF_S16(a) (memory+(a)/2)
#define ROUND_UP_32(a) (((a)+31)&~31)
#define A_INIT 1
#define A_LOOP 2
#define SOH3DS_AUDIO_GUARD(a,b,c) 1
static int16_t clamp16(int32_t v){return v < -32768 ? -32768 : v > 32767 ? 32767 : (int16_t)v;}
'''+body+'''
void adpcmReferenceAt(int16_t* mem,unsigned bytes,unsigned flags,uint16_t input,uint16_t output,int16_t* state,int16_t* loop,const int16_t* book){
 memory=mem;rspa.in=input;rspa.out=output;rspa.nbytes=bytes;rspa.adpcm_loop_state=(ADPCM_STATE*)loop;memcpy(rspa.adpcm_table,book,sizeof(rspa.adpcm_table));aADPCMdecImpl(flags,state);
}
void adpcmReference(int16_t* mem,unsigned bytes,unsigned flags,int16_t* state,int16_t* loop,const int16_t* book){
 memory=mem;rspa.in=0x1000;rspa.out=0x6000;rspa.nbytes=bytes;rspa.adpcm_loop_state=(ADPCM_STATE*)loop;memcpy(rspa.adpcm_table,book,sizeof(rspa.adpcm_table));aADPCMdecImpl(flags,state);
}
'''
(out/'adpcm_reference.c').write_text(code)
# Production decoder uses signed left shifts. Preserve established ARM modulo32
# behavior with fwrapv; shift sanitization cannot be used for this unchanged oracle.
subprocess.run(['cc','-std=c11','-O2','-fwrapv','-c',str(out/'adpcm_reference.c'),'-o',str(out/'adpcm_reference.o')],check=True)
for stem in ('adpcm_probe','adpcm_block_probe','adpcm_state_probe','dynamic_adpcm_probe'):
 subprocess.run(['c++','-std=c++17','-O2','-fsanitize=undefined','-fno-sanitize-recover=all','-I'+str(root/'research/dsp/teakra/include'),'-I'+str(root/'research/dsp/teakra/src'),str(here/(stem+'.cpp')),str(out/'adpcm_reference.o'),str(out/'teakra-build/src/libteakra.a'),'-pthread','-o',str(out/stem)],check=True)
 r=subprocess.run([str(out/stem)],check=False,capture_output=True,text=True);print(r.stdout,end='',flush=True);print(r.stderr,end='',flush=True);r.check_returncode();(out/(stem+'-result.txt')).write_text(r.stdout)
(out/'adpcm-reference.json').write_text(json.dumps({'body_sha256':hashlib.sha256(body.encode()).hexdigest(),'oracle_cflags':['-O2','-fwrapv'],'production_signed_shifts_unsanitized':True},indent=2)+'\n')
