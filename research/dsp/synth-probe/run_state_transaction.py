#!/usr/bin/env python3
"""Validate persistent-state commit against production filtering and real firmware."""
from pathlib import Path
import subprocess,json,hashlib,argparse,os
root=Path(__file__).resolve().parents[3];here=Path(__file__).resolve().parent
out=root/'builds/dsp-synth-probe-20260909'
parser=argparse.ArgumentParser()
parser.add_argument('--reuse-oracles',action='store_true',help='Umbrella only: oracle already refreshed in this run')
args=parser.parse_args()
if not args.reuse_oracles:
    for script in ('run_mapped_filter.py','run_resample.py','run_adpcm.py'):
        subprocess.run(['python3',str(here/script)],check=True)
dkp=Path(os.environ.get('DEVKITPRO','/home/sian/dkp-root/opt/devkitpro'))
arm=out/'state-transaction-arm11.cpp'
arm.write_text('#include "resident_transport_3ds.h"\n#include "state_transaction.h"\ntemplate bool ResidentDsp::StateTransaction::stage(ResidentDsp::CtrTransport&,const ResidentDsp::Session<ResidentDsp::CtrTransport>&);\ntemplate bool ResidentDsp::StateTransaction::collect(ResidentDsp::CtrTransport&,const ResidentDsp::Session<ResidentDsp::CtrTransport>&,const ResidentDsp::ChunkCapture&);\n')
subprocess.run([str(dkp/'devkitARM/bin/arm-none-eabi-g++'),'-std=gnu++17','-O2','-Wall','-Wextra','-Werror','-march=armv6k','-mtune=mpcore','-mfpu=vfp','-mfloat-abi=hard','-mtp=soft','-D__3DS__','-DARM11','-I'+str(dkp/'libctru/include'),'-I'+str(here),'-c',str(arm),'-o',str(out/'state-transaction-arm11.o')],check=True)
subprocess.run(['c++','-std=c++17','-O2','-fsanitize=address,undefined','-fno-sanitize-recover=all','-I'+str(root/'research/dsp/teakra/include'),'-I'+str(root/'research/dsp/teakra/src'),str(here/'state_transaction_probe.cpp'),str(out/'mapped_filter_reference.o'),str(out/'resample_reference.o'),str(out/'adpcm_reference.o'),str(out/'teakra-build/src/libteakra.a'),'-pthread','-o',str(out/'state-transaction-probe')],check=True)
r=subprocess.run([str(out/'state-transaction-probe')],capture_output=True,text=True)
print(r.stdout,end='');print(r.stderr,end='');r.check_returncode()
(out/'state-transaction-validation.json').write_text(json.dumps({'result':r.stdout,'oracle_limitation':'Extracted ADPCM oracle retains signed-shift portability limitation; see adpcm-reference.json.', 'scope':'Chunk-local state mapping and transactional publication. No producer interception, CPU replay, native output or FPS evidence.','sha256':{str(p.relative_to(root)):hashlib.sha256(p.read_bytes()).hexdigest() for p in [*here.glob('*.h'),here/'state_transaction_probe.cpp',here/'run_state_transaction.py',out/'mapped_filter_reference.c',out/'resample_reference.c',out/'adpcm_reference.c']}},indent=2)+'\n')
