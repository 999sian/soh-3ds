#!/usr/bin/env python3
"""Validate original-call replay using the actual scalar production mixer bodies."""
from pathlib import Path
import subprocess,json,hashlib,os
root=Path(__file__).resolve().parents[3];here=Path(__file__).resolve().parent
out=root/'builds/dsp-synth-probe-20260909';fixture=out/'cpu-replay-fixture';fixture.mkdir(exist_ok=True)
production=root/'third_party/shipwright/soh/soh'
header=(production/'mixer.h').read_text().replace('#include "libultraship/libultra/abi.h"','typedef uint32_t u32;\ntypedef int16_t ADPCM_STATE[16];\ntypedef int16_t RESAMPLE_STATE[16];\n#define A_INIT 1\n#define A_LOOP 2')
(fixture/'mixer.h').write_text(header)
src=(production/'mixer.c').read_text()
start=src.index('#include <opusfile.h>');end=src.index('void aSaveBufferImpl(',start)
src=src[:start]+src[end:]
src=src[:src.index('// From here on there are SIMD implementations')]
src+='\nunsigned cpuSnapshotSize(void){return sizeof(rspa);}\nvoid cpuSnapshot(void* p){memcpy(p,&rspa,sizeof(rspa));}\nint cpuRestore(const void* p,unsigned n){if(n!=sizeof(rspa))return 0;memcpy(&rspa,p,n);return 1;}\n'
src+='\nconst int16_t* cpuResampleTable(void){return &resample_table[0][0];}\n'
src+='\n#include <stddef.h>\n#include "mixer_image.h"\n_Static_assert(sizeof(rspa)==sizeof(DspMixerImage),"mixer image size");\n'
for field in ('in','out','nbytes','vol','rate','vol_wet','rate_wet','adpcm_loop_state','adpcm_table','filter_count','filter','buf'):
    src+=f'_Static_assert(offsetof(__typeof__(rspa),{field})==offsetof(DspMixerImage,{field}),"mixer image offset {field}");\n'
(fixture/'mixer_cpu.c').write_text(src)
# The production ADPCM arithmetic has signed shifts; preserve it as the reference
# and disable only that UBSan category. No DSP arithmetic is being validated here.
subprocess.run(['cc','-std=c11','-O2','-U__SSE2__','-fsanitize=address,undefined','-fno-sanitize=shift','-fno-sanitize-recover=all','-I'+str(fixture),'-I'+str(here),'-c',str(fixture/'mixer_cpu.c'),'-o',str(fixture/'mixer_cpu.o')],check=True)
subprocess.run(['c++','-std=c++17','-O2','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-sanitize-recover=all','-I'+str(fixture),str(here/'cpu_replay_probe.cpp'),str(fixture/'mixer_cpu.o'),'-o',str(out/'cpu-replay-probe')],check=True)
r=subprocess.run([str(out/'cpu-replay-probe')],capture_output=True,text=True);print(r.stdout,end='');print(r.stderr,end='');r.check_returncode()
dkp=Path(os.environ.get('DEVKITPRO','/home/sian/dkp-root/opt/devkitpro'))
arm=fixture/'arm11.cpp';arm.write_text('#include "cpu_replay_dispatch.h"\nstruct Cpu {bool restore(const void*,unsigned);void execute(const ResidentDsp::CpuCall& c,const void* p){ResidentDsp::executeCpuCall(c,p);}};\ntemplate bool ResidentDsp::CpuReplay::replay(Cpu&);\nstatic_assert(sizeof(ResidentDsp::CpuReplay)<=32*1024);\n')
subprocess.run([str(dkp/'devkitARM/bin/arm-none-eabi-g++'),'-std=gnu++17','-O2','-Wall','-Wextra','-Werror','-march=armv6k','-mtune=mpcore','-mfpu=vfp','-mfloat-abi=hard','-mtp=soft','-I'+str(fixture),'-I'+str(here),'-c',str(arm),'-o',str(fixture/'arm11.o')],check=True)
(out/'cpu-replay-validation.json').write_text(json.dumps({'result':r.stdout,'scope':'Original-call CPU journal and dispatch, not production interception or native DSP/NDSP recovery.','oracle_limitation':'Production ADPCM signed shifts preserved with shift sanitizer disabled for mixer oracle.','sha256':{str(p.relative_to(root)):hashlib.sha256(p.read_bytes()).hexdigest() for p in [here/'cpu_replay.h',here/'cpu_replay_dispatch.h',here/'cpu_replay_probe.cpp',here/'run_cpu_replay.py',production/'mixer.c',production/'mixer.h',here/'mixer_image.h',fixture/'mixer_cpu.c',fixture/'mixer.h']}},indent=2)+'\n')
