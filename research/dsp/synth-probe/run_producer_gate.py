#!/usr/bin/env python3
"""Exercise the native producer gate and compile its actual game call sites."""
from pathlib import Path
import hashlib
import json
import os
import shlex
import subprocess

ROOT=Path(__file__).resolve().parents[3]
HERE=Path(__file__).resolve().parent
OUT=ROOT/'builds/dsp-synth-probe-20260909/producer-gate'
OUT.mkdir(parents=True,exist_ok=True)
subprocess.run(['c++','-std=c++17','-O2','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-sanitize-recover=all','-I'+str(HERE/'gate_test'),str(HERE/'producer_gate_probe.cpp'),str(HERE/'producer_gate_3ds.cpp'),'-pthread','-o',str(OUT/'probe')],check=True)
r=subprocess.run([str(OUT/'probe')],capture_output=True,text=True)
print(r.stdout,end='',flush=True);print(r.stderr,end='',flush=True);r.check_returncode()
subprocess.run(['cc','-std=c11','-O2','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-sanitize-recover=all','-c',str(HERE/'sdk_lifecycle_bridge.c'),'-o',str(OUT/'bridge-host.o')],check=True)
subprocess.run(['c++','-std=c++17','-O2','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-sanitize-recover=all','-I'+str(HERE/'gate_test'),str(HERE/'sdk_lifecycle_owner_probe.cpp'),str(HERE/'producer_gate_3ds.cpp'),str(OUT/'bridge-host.o'),'-pthread','-o',str(OUT/'lifecycle-owner-probe')],check=True)
lifecycle=subprocess.run([str(OUT/'lifecycle-owner-probe')],capture_output=True,text=True)
print(lifecycle.stdout,end='',flush=True);print(lifecycle.stderr,end='',flush=True);lifecycle.check_returncode()
dkp=Path(os.environ.get('DEVKITPRO','/home/sian/dkp-root/opt/devkitpro'))
compiler=str(dkp/'devkitARM/bin/arm-none-eabi-g++')
subprocess.run([compiler,'-std=gnu++17','-O2','-Wall','-Wextra','-Werror','-march=armv6k','-mtune=mpcore','-mfpu=vfp','-mfloat-abi=hard','-mtp=soft','-D__3DS__','-DARM11','-I'+str(dkp/'libctru/include'),'-c',str(HERE/'producer_gate_3ds.cpp'),'-o',str(OUT/'gate-arm11.o')],check=True)
arm=OUT/'sdk-owner-arm11.cpp'
arm.write_text('''#include "resident_transport_3ds.h"
#include "mixer_owner.h"
#include "sdk_lifecycle_owner.h"
struct Cpu {bool read(DspMixerImage&);bool restore(const void*,unsigned);void execute(const ResidentDsp::CpuCall&,const void*);bool canApply(const DspMixerImage&);void apply(const DspMixerImage&);};
struct Life {void lock();void unlock();bool startDsp();bool stopDsp();bool startOutput();bool stopOutput();bool startNdsp();bool stopNdsp();bool cancelled();bool outputHealthy();uint64_t now();bool wait(uint64_t);[[noreturn]] void fatal(const char*);};
struct Mutex {LightLock mutex;Mutex(){LightLock_Init(&mutex);}void lock(){LightLock_Lock(&mutex);}void unlock(){LightLock_Unlock(&mutex);}};
using NativeOwner=ResidentDsp::MixerOwner<ResidentDsp::CtrTransport,Cpu,Life>;
template class ResidentDsp::SdkLifecycleOwner<NativeOwner,Mutex>;
''')
subprocess.run([compiler,'-std=gnu++17','-O2','-Wall','-Wextra','-Werror','-march=armv6k','-mtune=mpcore','-mfpu=vfp','-mfloat-abi=hard','-mtp=soft','-D__3DS__','-DARM11','-I'+str(dkp/'libctru/include'),'-I'+str(HERE),'-c',str(arm),'-o',str(OUT/'sdk-owner-arm11.o')],check=True)
flags_file=ROOT/'build-3ds-mk/CMakeFiles/soh_enhancement.dir/flags.make'
lines=flags_file.read_text().splitlines();flags=[]
for key in ('CXX_DEFINES','CXX_INCLUDES','CXX_FLAGS'):
    flags+=shlex.split(next(line.split(' = ',1)[1] for line in lines if line.startswith(key+' = ')))
game=ROOT/'third_party/shipwright/soh/soh/OTRGlobals.cpp'
subprocess.run([compiler,*flags,'-DSOH3DS_DSP_CAPTURE','-I'+str(HERE),'-fsyntax-only',str(game)],check=True)
files=[HERE/n for n in ('producer_gate.h','producer_gate_3ds.cpp','producer_gate_probe.cpp','gate_test/3ds.h','run_producer_gate.py','sdk_lifecycle_owner.h','sdk_lifecycle_owner_probe.cpp','sdk_lifecycle_bridge.c','sdk_lifecycle_bridge.h','mixer_owner.h')]+[game,ROOT/'CMakeLists.txt',ROOT/'cmake/DspLifecycle3DS.cmake',flags_file,OUT/'gate-arm11.o',arm,OUT/'sdk-owner-arm11.o']
(OUT/'validation.json').write_text(json.dumps({'result':r.stdout+lifecycle.stdout,'scope':'Real host threads against native gate and lifecycle coordinator with mocked backend modes, ASan/UBSan; ARM11 gate and actual MixerOwner/coordinator template compilation, plus actual OTRGlobals opt-in call-site syntax. No complete game link, physical sleep or runtime handler installation proven.','sha256':{str(p.relative_to(ROOT)):hashlib.sha256(p.read_bytes()).hexdigest() for p in files}},indent=2)+'\n')
print('PASS: ARM11 producer gate and actual opt-in OTRGlobals call sites',flush=True)
