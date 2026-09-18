#!/usr/bin/env python3
"""Compare DSP stateful filter with extracted production scalar implementation."""
from pathlib import Path
import subprocess,json,hashlib
root=Path(__file__).resolve().parents[3];here=Path(__file__).resolve().parent
out=root/'builds/dsp-synth-probe-20260909'
src=(root/'third_party/shipwright/soh/soh/mixer.c').read_text()
a=src.index('void aFilterImpl(');body=src[a:src.index('\nvoid aHiLoGainImpl(',a)]
code='''#include <stdint.h>
#include <string.h>
static int16_t* memory;
static struct {uint16_t filter_count;int16_t filter[8];} rspa;
#define BUF_S16(a) (memory+(a)/2)
#define ROUND_UP_16(v) (((v)+15)&~15)
#define A_INIT 1
#define SOH3DS_AUDIO_GUARD(a,b,c) 1
static int16_t clamp16(int32_t x){return x < -32768 ? -32768 : x > 32767 ? 32767 : (int16_t)x;}
'''+body+'''
void filterReferenceAt(int16_t* mem,unsigned bytes,unsigned flags,int16_t* state,int16_t* coeff,uint16_t address){
memory=mem;aFilterImpl(2,bytes,coeff);aFilterImpl(flags,address,state);memcpy(coeff,rspa.filter,sizeof(rspa.filter));
}
void filterReference(int16_t* mem,unsigned bytes,unsigned flags,int16_t* state,int16_t* coeff){filterReferenceAt(mem,bytes,flags,state,coeff,0x800);}
'''
(out/'filter_reference.c').write_text(code)
subprocess.run(['cc','-std=c11','-O2','-fsanitize=undefined','-fno-sanitize-recover=all','-c',str(out/'filter_reference.c'),'-o',str(out/'filter_reference.o')],check=True)
subprocess.run(['c++','-std=c++17','-O2','-fsanitize=undefined','-fno-sanitize-recover=all','-I'+str(root/'research/dsp/teakra/include'),'-I'+str(root/'research/dsp/teakra/src'),str(here/'filter_probe.cpp'),str(out/'filter_reference.o'),str(out/'teakra-build/src/libteakra.a'),'-pthread','-o',str(out/'filter-probe')],check=True)
r=subprocess.run([str(out/'filter-probe')],capture_output=True,text=True);print(r.stdout,end='');print(r.stderr,end='');r.check_returncode()
(out/'filter-result.txt').write_text(r.stdout)
(out/'filter-reference.json').write_text(json.dumps({'body_sha256':hashlib.sha256(body.encode()).hexdigest(),'oracle_ubsan':True},indent=2)+'\n')
