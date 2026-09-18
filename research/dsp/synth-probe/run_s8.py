#!/usr/bin/env python3
"""Validate mapped S8 synthesis against extracted production C."""
from pathlib import Path
import subprocess
root=Path(__file__).resolve().parents[3];here=Path(__file__).resolve().parent
out=root/'builds/dsp-synth-probe-20260909'
src=(root/'third_party/shipwright/soh/soh/mixer.c').read_text()
a=src.index('void aS8DecImpl(');body=src[a:src.index('\nvoid aAddMixerImpl(',a)]
code='''#include <stdint.h>
#include <string.h>
typedef int16_t ADPCM_STATE[16];
static int16_t* memory;
static struct {unsigned in,out,nbytes;int16_t* adpcm_loop_state;} rspa;
#define BUF_U8(a) ((uint8_t*)memory+(a))
#define BUF_S16(a) (memory+(a)/2)
#define ROUND_UP_32(v) (((v)+31)&~31)
#define SOH3DS_AUDIO_GUARD(a,b,c) 1
#define A_INIT 1
#define A_LOOP 2
'''+body+'''
void s8Reference(int16_t* mem,unsigned bytes,unsigned flags,unsigned input,unsigned output,int16_t* state,int16_t* loop){memory=mem;rspa.in=input;rspa.out=output;rspa.nbytes=bytes;rspa.adpcm_loop_state=loop;aS8DecImpl(flags,state);}
'''
(out/'s8_reference.c').write_text(code)
flags=['-O2','-fsanitize=undefined','-fno-sanitize-recover=all']
subprocess.run(['cc','-std=c11',*flags,'-c',str(out/'s8_reference.c'),'-o',str(out/'s8_reference.o')],check=True)
subprocess.run(['c++','-std=c++17',*flags,'-I'+str(root/'research/dsp/teakra/include'),'-I'+str(root/'research/dsp/teakra/src'),str(here/'mapped_s8_probe.cpp'),str(out/'s8_reference.o'),str(out/'teakra-build/src/libteakra.a'),'-pthread','-o',str(out/'mapped-s8-probe')],check=True)
r=subprocess.run([str(out/'mapped-s8-probe')],capture_output=True,text=True)
print(r.stdout,end='');print(r.stderr,end='');r.check_returncode()
(out/'mapped-s8-result.txt').write_text(r.stdout)
