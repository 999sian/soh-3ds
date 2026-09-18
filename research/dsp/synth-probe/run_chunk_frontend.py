#!/usr/bin/env python3
"""Compile original mixer calls into DSP execution, or replay them on CPU."""
from pathlib import Path
import subprocess,json,hashlib,argparse,os
root=Path(__file__).resolve().parents[3];here=Path(__file__).resolve().parent;out=root/'builds/dsp-synth-probe-20260909'
parser=argparse.ArgumentParser();parser.add_argument('--reuse-cpu',action='store_true',help='Umbrella only: CPU fixture just refreshed');args=parser.parse_args()
if not args.reuse_cpu:subprocess.run(['python3',str(here/'run_cpu_replay.py')],check=True)
fixture=out/'cpu-replay-fixture'
subprocess.run(['c++','-std=c++17','-O2','-fsanitize=address,undefined','-fno-sanitize-recover=all','-I'+str(fixture),'-I'+str(root/'research/dsp/teakra/include'),'-I'+str(root/'research/dsp/teakra/src'),str(here/'chunk_frontend_probe.cpp'),str(fixture/'mixer_cpu.o'),str(out/'teakra-build/src/libteakra.a'),'-pthread','-o',str(out/'chunk-frontend-probe')],check=True)
r=subprocess.run([str(out/'chunk-frontend-probe')],capture_output=True,text=True);print(r.stdout,end='');print(r.stderr,end='');r.check_returncode()
dkp=Path(os.environ.get('DEVKITPRO','/home/sian/dkp-root/opt/devkitpro'));arm=out/'chunk-frontend-arm11.cpp'
arm.write_text('#include "resident_transport_3ds.h"\n#include "chunk_frontend.h"\nstruct Cpu {bool restore(const void*,unsigned);void execute(const ResidentDsp::CpuCall&,const void*);bool canApply(const DspMixerImage&);void apply(const DspMixerImage&);};\ntemplate class ResidentDsp::ChunkFrontend<ResidentDsp::CtrTransport>;\nusing F=ResidentDsp::ChunkFrontend<ResidentDsp::CtrTransport>;\ntemplate bool F::record(const ResidentDsp::CpuCall&,Cpu&);\ntemplate bool F::seal(Cpu&);\ntemplate F::Phase F::step(uint64_t,Cpu&);\ntemplate bool F::recoverAfterStop(Cpu&);\nstatic_assert(sizeof(F)<12*1024);\n')
subprocess.run([str(dkp/'devkitARM/bin/arm-none-eabi-g++'),'-std=gnu++17','-O2','-Wall','-Wextra','-Werror','-march=armv6k','-mtune=mpcore','-mfpu=vfp','-mfloat-abi=hard','-mtp=soft','-D__3DS__','-DARM11','-I'+str(dkp/'libctru/include'),'-I'+str(here),'-c',str(arm),'-o',str(out/'chunk-frontend-arm11.o')],check=True)
(out/'chunk-frontend-validation.json').write_text(json.dumps({'result':r.stdout,'scope':'Original-call frontend in Teakra. Actual producer interception, native output and NDSP recovery are not wired.','oracle_limitation':'Production scalar mixer has shift sanitizer disabled for original ADPCM arithmetic.','sha256':{str(p.relative_to(root)):hashlib.sha256(p.read_bytes()).hexdigest() for p in [*here.glob('*.h'),here/'chunk_frontend_probe.cpp',here/'run_chunk_frontend.py',fixture/'mixer_cpu.c']}},indent=2)+'\n')
