#!/usr/bin/env python3
"""Validate resident global filter setup and independent histories."""
from pathlib import Path
import subprocess
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
void mappedFilterSetupReference(int16_t* mem,unsigned bytes,const int16_t* coeff){memory=mem;aFilterImpl(2,bytes,(int16_t*)coeff);memcpy(mem+0x7400,rspa.filter,16);mem[0x7408]=rspa.filter_count/2;}
void mappedFilterProcessReference(int16_t* mem,unsigned flags,int16_t* state,unsigned address){memory=mem;rspa.filter_count=(uint16_t)mem[0x7408]*2;memcpy(rspa.filter,mem+0x7400,16);aFilterImpl(flags,address,state);memcpy(mem+0x7400,rspa.filter,16);}
'''
(out/'mapped_filter_reference.c').write_text(code)
flags=['-O2','-fsanitize=undefined','-fno-sanitize-recover=all']
subprocess.run(['cc','-std=c11',*flags,'-c',str(out/'mapped_filter_reference.c'),'-o',str(out/'mapped_filter_reference.o')],check=True)
subprocess.run(['c++','-std=c++17',*flags,'-I'+str(root/'research/dsp/teakra/include'),'-I'+str(root/'research/dsp/teakra/src'),str(here/'mapped_filter_probe.cpp'),str(out/'mapped_filter_reference.o'),str(out/'teakra-build/src/libteakra.a'),'-pthread','-o',str(out/'mapped-filter-probe')],check=True)
r=subprocess.run([str(out/'mapped-filter-probe')],capture_output=True,text=True)
print(r.stdout,end='');print(r.stderr,end='');r.check_returncode()
(out/'mapped-filter-result.txt').write_text(r.stdout)
