#!/usr/bin/env python3
"""Validate addressable gain/envelope kernels against extracted production C."""
from pathlib import Path
import subprocess,json,hashlib
root=Path(__file__).resolve().parents[3];here=Path(__file__).resolve().parent
out=root/'builds/dsp-synth-probe-20260909'
src=(root/'third_party/shipwright/soh/soh/mixer.c').read_text()
a=src.index('static void aMixImplRef(');gain=src[a:src.index('\nvoid aMixImpl(',a)]
a=src.index('void aInterleaveImpl(');interleave=src[a:src.index('\nvoid aDMEMMoveImpl(',a)]
a=src.index('void aEnvMixerImpl(');envelope=src[a:src.index('\nstatic void aMixImplRef(',a)]
a=src.index('void aAddMixerImpl(');add=src[a:src.index('\nvoid aDuplicateImpl(',a)]
a=src.index('void aInterlImpl(');interl=src[a:src.index('\nvoid aFilterImpl(',a)]
a=src.index('void aResampleZohImpl(');zoh=src[a:src.index('\nvoid aInterlImpl(',a)]
code='''#include <stdint.h>
#include <stdbool.h>
typedef uint32_t u32;
static int16_t* memory;
static const uint16_t* bases;
static struct {uint16_t in,out,nbytes;uint16_t vol[2],rate[2],vol_wet,rate_wet;} rspa;
#define ROUND_UP_64(v) (((v)+63)&~63)
#define ROUND_UP_8(v) (((v)+7)&~7)
#define ROUND_UP_32(v) (((v)+31)&~31)
#define ROUND_UP_16(v) (((v)+15)&~15)
#define ROUND_DOWN_16(v) ((v)&~15)
#define BUF_S16(a) (memory+(a)/2)
static int16_t clamp16(int32_t x){return x < -32768 ? -32768 : x > 32767 ? 32767 : (int16_t)x;}
'''+gain+interleave+add+interl+zoh+'''
void mappedZohReference(int16_t* mem,unsigned bytes,unsigned input,unsigned output,uint16_t pitch,uint16_t phase){memory=mem;rspa.in=input;rspa.out=output;rspa.nbytes=bytes;aResampleZohImpl(pitch,phase);}
void mappedAddReference(int16_t* mem,unsigned count,unsigned input,unsigned output){memory=mem;aAddMixerImpl(count,input,output);}
void mappedInterlReference(int16_t* mem,unsigned count,unsigned input,unsigned output){memory=mem;aInterlImpl(input,output,count);}
void mappedInterleaveReference(int16_t* mem,unsigned bytes,unsigned left,unsigned right,unsigned dest){memory=mem;aInterleaveImpl(dest,left,right,bytes);}
void mappedGainReference(int16_t* mem,unsigned count,int16_t gain,unsigned input,unsigned output){memory=mem;aMixImplRef(count,gain,input,output);}
#undef BUF_S16
#define BUF_S16(a) (memory+bases[(a)/0x100-1])
'''+envelope+'''
void mappedEnvelopeReference(int16_t* mem,unsigned count,unsigned flags,const uint16_t* vols,const uint16_t* rates,const uint16_t* addresses){
memory=mem;bases=addresses;rspa.vol[0]=vols[0];rspa.vol[1]=vols[1];rspa.vol_wet=vols[2];rspa.rate[0]=rates[0];rspa.rate[1]=rates[1];rspa.rate_wet=rates[2];
aEnvMixerImpl(0x100,count,flags&1,flags&2,flags&4,flags&8,flags&16,0x20304050,0);
}
'''
(out/'mapped_arithmetic_reference.c').write_text(code)
flags=['-O2','-fsanitize=undefined','-fno-sanitize-recover=all']
subprocess.run(['cc','-std=c11',*flags,'-c',str(out/'mapped_arithmetic_reference.c'),'-o',str(out/'mapped_arithmetic_reference.o')],check=True)
subprocess.run(['c++','-std=c++17',*flags,'-I'+str(root/'research/dsp/teakra/include'),'-I'+str(root/'research/dsp/teakra/src'),str(here/'mapped_arithmetic_probe.cpp'),str(out/'mapped_arithmetic_reference.o'),str(out/'teakra-build/src/libteakra.a'),'-pthread','-o',str(out/'mapped-arithmetic-probe')],check=True)
r=subprocess.run([str(out/'mapped-arithmetic-probe')],capture_output=True,text=True);print(r.stdout,end='');print(r.stderr,end='');r.check_returncode()
(out/'mapped-arithmetic-result.txt').write_text(r.stdout)
(out/'mapped-arithmetic-validation.json').write_text(json.dumps({'result':r.stdout,'gain_sha256':hashlib.sha256(gain.encode()).hexdigest(),'interleave_sha256':hashlib.sha256(interleave.encode()).hexdigest(),'envelope_sha256':hashlib.sha256(envelope.encode()).hexdigest(),'add_sha256':hashlib.sha256(add.encode()).hexdigest(),'interl_sha256':hashlib.sha256(interl.encode()).hexdigest(),'zoh_sha256':hashlib.sha256(zoh.encode()).hexdigest(),'oracle_ubsan':True,'sha256':{str(p.relative_to(root)):hashlib.sha256(p.read_bytes()).hexdigest() for p in [*here.glob('*.h'),here/'mapped_arithmetic_probe.cpp',here/'run_mapped_arithmetic.py']}},indent=2)+'\n')
