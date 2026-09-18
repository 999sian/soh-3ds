#!/usr/bin/env python3
"""Validate remaining scalar gain/table commands against production C."""
from pathlib import Path
import subprocess
root=Path(__file__).resolve().parents[3];here=Path(__file__).resolve().parent
out=root/'builds/dsp-synth-probe-20260909'
src=(root/'third_party/shipwright/soh/soh/mixer.c').read_text()
a=src.index('void aHiLoGainImpl(');body=src[a:src.index('// From here on',a)]
code='''#include <stdint.h>
#include <string.h>
static int16_t* memory;
#define BUF_S16(a) (memory+(a)/2)
#define ROUND_UP_32(v) (((v)+31)&~31)
#define ROUND_UP_64(v) (((v)+63)&~63)
static int16_t clamp16(int32_t x){return x < -32768 ? -32768 : x > 32767 ? 32767 : (int16_t)x;}
'''+body+'''
void hiLoReference(int16_t* mem,unsigned gain,unsigned bytes,unsigned input){memory=mem;aHiLoGainImpl(gain,bytes,input);}
void tableReference(int16_t* mem,unsigned offset,unsigned bytes,unsigned output,unsigned input){memory=mem;aUnkCmd19Impl(offset,bytes,output,input);}
'''
(out/'scalar_reference.c').write_text(code)
flags=['-O2','-fsanitize=undefined','-fno-sanitize-recover=all']
subprocess.run(['cc','-std=c11',*flags,'-c',str(out/'scalar_reference.c'),'-o',str(out/'scalar_reference.o')],check=True)
subprocess.run(['c++','-std=c++17',*flags,'-I'+str(root/'research/dsp/teakra/include'),'-I'+str(root/'research/dsp/teakra/src'),str(here/'mapped_scalar_probe.cpp'),str(out/'scalar_reference.o'),str(out/'teakra-build/src/libteakra.a'),'-pthread','-o',str(out/'mapped-scalar-probe')],check=True)
r=subprocess.run([str(out/'mapped-scalar-probe')],capture_output=True,text=True)
print(r.stdout,end='');print(r.stderr,end='');r.check_returncode()
(out/'mapped-scalar-result.txt').write_text(r.stdout)
