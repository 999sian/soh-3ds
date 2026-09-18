#!/usr/bin/env python3
"""Compare DSP envelope arithmetic with the production scalar implementation."""
from pathlib import Path
import subprocess,json,hashlib
root=Path(__file__).resolve().parents[3];here=Path(__file__).resolve().parent
out=root/'builds/dsp-synth-probe-20260909'
src=(root/'third_party/shipwright/soh/soh/mixer.c').read_text()
a=src.index('void aEnvMixerImpl(');body=src[a:src.index('\nstatic void aMixImplRef(',a)]
code='''#include <stdint.h>
#include <stdbool.h>
typedef uint32_t u32;
static int16_t* memory;
static const uint16_t* bases;
static struct {uint16_t vol[2],rate[2],vol_wet,rate_wet;} rspa;
// Remap encoded bases; test cases also intentionally alias these buffers.
#define BUF_S16(a) (memory + bases[(a)/0x100-1])
#define ROUND_UP_16(v) (((v)+15)&~15)
static int16_t clamp16(int32_t x){return x < -32768 ? -32768 : x > 32767 ? 32767 : (int16_t)x;}
'''+body+'''
void envelopeReference(int16_t* mem,unsigned count,unsigned flags,const uint16_t* vols,const uint16_t* rates,const uint16_t* addresses){
memory=mem;bases=addresses;rspa.vol[0]=vols[0];rspa.vol[1]=vols[1];rspa.vol_wet=vols[2];
rspa.rate[0]=rates[0];rspa.rate[1]=rates[1];rspa.rate_wet=rates[2];
aEnvMixerImpl(0x100,count,flags&1,flags&2,flags&4,flags&8,flags&16,0x20304050,0);
}
'''
(out/'envelope_reference.c').write_text(code)
subprocess.run(['cc','-std=c11','-O2','-fsanitize=undefined','-fno-sanitize-recover=all','-c',str(out/'envelope_reference.c'),'-o',str(out/'envelope_reference.o')],check=True)
subprocess.run(['c++','-std=c++17','-O2','-fsanitize=undefined','-fno-sanitize-recover=all','-I'+str(root/'research/dsp/teakra/include'),'-I'+str(root/'research/dsp/teakra/src'),str(here/'envelope_probe.cpp'),str(out/'envelope_reference.o'),str(out/'teakra-build/src/libteakra.a'),'-pthread','-o',str(out/'envelope-probe')],check=True)
r=subprocess.run([str(out/'envelope-probe')],capture_output=True,text=True);print(r.stdout,end='');print(r.stderr,end='');r.check_returncode()
(out/'envelope-result.txt').write_text(r.stdout)
(out/'envelope-reference.json').write_text(json.dumps({'body_sha256':hashlib.sha256(body.encode()).hexdigest(),'oracle_ubsan':True},indent=2)+'\n')
