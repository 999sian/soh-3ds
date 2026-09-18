#!/usr/bin/env python3
"""Validate exact snapshot duplication against extracted production C."""
from pathlib import Path
import subprocess
root=Path(__file__).resolve().parents[3];here=Path(__file__).resolve().parent
out=root/'builds/dsp-synth-probe-20260909'
src=(root/'third_party/shipwright/soh/soh/mixer.c').read_text()
a=src.index('void aDuplicateImpl(');body=src[a:src.index('\nvoid aResampleZohImpl(',a)]
code='''#include <stdint.h>
#include <string.h>
static int16_t* memory;
#define BUF_U8(a) ((uint8_t*)memory+(a))
'''+body+'''
void duplicateReference(int16_t* mem,unsigned count,unsigned input,unsigned output){memory=mem;aDuplicateImpl(count,input,output);}
'''
(out/'duplicate_reference.c').write_text(code)
flags=['-O2','-fsanitize=undefined','-fno-sanitize-recover=all']
subprocess.run(['cc','-std=c11',*flags,'-c',str(out/'duplicate_reference.c'),'-o',str(out/'duplicate_reference.o')],check=True)
subprocess.run(['c++','-std=c++17',*flags,'-I'+str(root/'research/dsp/teakra/include'),'-I'+str(root/'research/dsp/teakra/src'),str(here/'mapped_duplicate_probe.cpp'),str(out/'duplicate_reference.o'),str(out/'teakra-build/src/libteakra.a'),'-pthread','-o',str(out/'mapped-duplicate-probe')],check=True)
r=subprocess.run([str(out/'mapped-duplicate-probe')],capture_output=True,text=True)
print(r.stdout,end='');print(r.stderr,end='');r.check_returncode()
(out/'mapped-duplicate-result.txt').write_text(r.stdout)
