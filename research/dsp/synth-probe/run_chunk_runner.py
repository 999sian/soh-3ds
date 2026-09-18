#!/usr/bin/env python3
"""Execute captured chunks through real Teakra firmware and injected transport faults."""
from pathlib import Path
import subprocess,json,hashlib,argparse,os
root=Path(__file__).resolve().parents[3];here=Path(__file__).resolve().parent
out=root/'builds/dsp-synth-probe-20260909'
parser=argparse.ArgumentParser()
parser.add_argument('--reuse-scalar',action='store_true',help='Umbrella only: scalar oracle was refreshed earlier in this same run')
args=parser.parse_args()
# Standalone runs always refresh the extracted production CPU comparison oracle.
if not args.reuse_scalar:
    subprocess.run(['python3',str(here/'run_scalar.py')],check=True)
# Instantiate every runner method against the real native adapter and ARM11 ABI.
dkp=Path(os.environ.get('DEVKITPRO','/home/sian/dkp-root/opt/devkitpro'))
arm_source=out/'chunk-runner-arm11.cpp'
arm_source.write_text('#include "resident_transport_3ds.h"\n#include "chunk_runner.h"\ntemplate class ResidentDsp::ChunkRunner<ResidentDsp::CtrTransport>;\nstatic_assert(sizeof(ResidentDsp::ChunkRunner<ResidentDsp::CtrTransport>)<8192);\n')
subprocess.run([str(dkp/'devkitARM/bin/arm-none-eabi-g++'),'-std=gnu++17','-O2','-Wall','-Wextra','-Werror','-march=armv6k','-mtune=mpcore','-mfpu=vfp','-mfloat-abi=hard','-mtp=soft','-D__3DS__','-DARM11','-I'+str(dkp/'libctru/include'),'-I'+str(here),'-c',str(arm_source),'-o',str(out/'chunk-runner-arm11.o')],check=True)
subprocess.run(['c++','-std=c++17','-O2','-fsanitize=address,undefined','-fno-sanitize-recover=all','-I'+str(root/'research/dsp/teakra/include'),'-I'+str(root/'research/dsp/teakra/src'),str(here/'chunk_runner_probe.cpp'),str(out/'scalar_reference.o'),str(out/'teakra-build/src/libteakra.a'),'-pthread','-o',str(out/'chunk-runner-probe')],check=True)
r=subprocess.run([str(out/'chunk-runner-probe')],capture_output=True,text=True)
print(r.stdout,end='');print(r.stderr,end='');r.check_returncode()
(out/'chunk-runner-validation.json').write_text(json.dumps({'result':r.stdout,'scope':'Captured DSP execution; not native transport, persistent-state transaction, CPU fallback or production integration.','sha256':{str(p.relative_to(root)):hashlib.sha256(p.read_bytes()).hexdigest() for p in [*here.glob('*.h'),here/'chunk_runner_probe.cpp',here/'run_chunk_runner.py',out/'scalar_reference.c']}},indent=2)+'\n')
