#!/usr/bin/env python3
"""Verify resident byte-addressed DMEM against production clear/memmove."""
from pathlib import Path
import subprocess,hashlib,json
root=Path(__file__).resolve().parents[3];here=Path(__file__).resolve().parent
out=root/'builds/dsp-synth-probe-20260909'
src=(root/'third_party/shipwright/soh/soh/mixer.c').read_text()
bodies=[]
for name,end in [('aClearBufferImpl','aLoadBufferImpl'),('aDMEMMoveImpl','aSetLoopImpl')]:
 start=src.index('void '+name+'(');bodies.append(src[start:src.index('\nvoid '+end+'(',start)])
code='''#include <stdint.h>
#include <string.h>
static uint8_t* memory;
#define ROUND_UP_16(v) (((v)+15)&~15)
#define BUF_U8(a) (memory+((a)-0x3c0))
'''+''.join(bodies)+'''
void dmemReference(uint8_t* mem,unsigned op,unsigned src,unsigned dst,unsigned bytes){
 memory=mem;if(op)aClearBufferImpl(dst+0x3c0,bytes);else aDMEMMoveImpl(src+0x3c0,dst+0x3c0,bytes);
}
'''
(out/'dmem_reference.c').write_text(code)
flags=['-O2','-fsanitize=undefined','-fno-sanitize-recover=all']
subprocess.run(['cc','-std=c11',*flags,'-c',str(out/'dmem_reference.c'),'-o',str(out/'dmem_reference.o')],check=True)
subprocess.run(['c++','-std=c++17',*flags,'-I'+str(root/'research/dsp/teakra/include'),'-I'+str(root/'research/dsp/teakra/src'),str(here/'dmem_transfer_probe.cpp'),str(out/'dmem_reference.o'),str(out/'teakra-build/src/libteakra.a'),'-pthread','-o',str(out/'dmem-transfer-probe')],check=True)
r=subprocess.run([str(out/'dmem-transfer-probe')],capture_output=True,text=True);print(r.stdout,end='');print(r.stderr,end='');r.check_returncode()
(out/'dmem-result.txt').write_text(r.stdout)
(out/'dmem-reference.json').write_text(json.dumps({'body_sha256':hashlib.sha256(''.join(bodies).encode()).hexdigest(),'oracle_ubsan':True},indent=2)+'\n')

subprocess.run(['c++','-std=c++17',*flags,'-I'+str(root/'research/dsp/teakra/include'),'-I'+str(root/'research/dsp/teakra/src'),str(here/'mapped_queue_probe.cpp'),str(out/'teakra-build/src/libteakra.a'),'-pthread','-o',str(out/'mapped-queue-probe')],check=True)
r=subprocess.run([str(out/'mapped-queue-probe')],capture_output=True,text=True);print(r.stdout,end='');print(r.stderr,end='');r.check_returncode()
(out/'mapped-queue-result.txt').write_text(r.stdout)
(out/'mapped-validation.json').write_text(json.dumps({
    'results': [(out/'dmem-result.txt').read_text(), (out/'mapped-queue-result.txt').read_text()],
    'scope': 'Teakra only; native mapped allocation and production backend are not integrated.',
    'sha256': {str(p.relative_to(root)):hashlib.sha256(p.read_bytes()).hexdigest() for p in [*here.glob('*.h'),here/'dmem_transfer_probe.cpp',here/'mapped_queue_probe.cpp',here/'run_dmem.py',out/'dmem_reference.c']}
},indent=2)+'\n')
