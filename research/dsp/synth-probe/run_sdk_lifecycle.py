#!/usr/bin/env python3
"""Check the SDK adapter's exact symbol rewrite and lifecycle dispatch."""
from pathlib import Path
import hashlib
import json
import os
import subprocess
from prepare_sdk_lifecycle import prepare

ROOT=Path(__file__).resolve().parents[3]
HERE=Path(__file__).resolve().parent
OUT=ROOT/'builds/dsp-synth-probe-20260909/sdk-lifecycle'
OUT.mkdir(parents=True,exist_ok=True)
options=['-O2','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-sanitize-recover=all']
subprocess.run(['cc','-std=c11',*options,'-c',str(HERE/'sdk_lifecycle_bridge.c'),'-o',str(OUT/'bridge-host.o')],check=True)
subprocess.run(['c++','-std=c++17',*options,str(HERE/'sdk_lifecycle_probe.cpp'),str(OUT/'bridge-host.o'),'-pthread','-o',str(OUT/'probe')],check=True)
r=subprocess.run([str(OUT/'probe')],capture_output=True,text=True)
print(r.stdout,end='');print(r.stderr,end='');r.check_returncode()
subprocess.run(['cc','-std=c11',*options,'-I'+str(HERE/'ownership_test'),str(HERE/'sdk_ownership_probe.c'),str(HERE/'sdk_ownership_3ds.c'),'-o',str(OUT/'ownership-probe')],check=True)
ownership=subprocess.run([str(OUT/'ownership-probe')],capture_output=True,text=True)
print(ownership.stdout,end='');print(ownership.stderr,end='');ownership.check_returncode()
dkp=Path(os.environ.get('DEVKITPRO','/home/sian/dkp-root/opt/devkitpro'))
prepare(dkp,OUT/'dsp.o')
subprocess.run([str(dkp/'devkitARM/bin/arm-none-eabi-gcc'),'-std=gnu11','-O2','-Wall','-Wextra','-Werror','-march=armv6k','-mtune=mpcore','-mfpu=vfp','-mfloat-abi=hard','-mtp=soft','-c',str(HERE/'sdk_lifecycle_bridge.c'),'-o',str(OUT/'bridge-arm11.o')],check=True)
# Exercise the same CMake attachment used by the opt-in game target. This is
# an SDK link smoke target, not a game build or a physical lifecycle test.
cmake_source=OUT/'cmake-source';cmake_source.mkdir(exist_ok=True)
(cmake_source/'main.c').write_text('#include <3ds.h>\nint main(void){return dspIsComponentLoaded();}\n')
(cmake_source/'CMakeLists.txt').write_text('''cmake_minimum_required(VERSION 3.20)
project(dsp_sdk_link C CXX)
include("${SOH_ROOT}/cmake/DspLifecycle3DS.cmake")
add_executable(dsp_sdk_link main.c)
soh3ds_attach_dsp_lifecycle(dsp_sdk_link)
target_link_libraries(dsp_sdk_link PRIVATE ctru m)
''')
env=os.environ.copy();env['DEVKITPRO']=str(dkp)
subprocess.run(['cmake','-S',str(cmake_source),'-B',str(OUT/'cmake-build'),'-DCMAKE_TOOLCHAIN_FILE='+str(ROOT/'cmake/3DS.cmake'),'-DSOH_ROOT='+str(ROOT)],check=True,env=env,stdout=subprocess.DEVNULL)
subprocess.run(['cmake','--build',str(OUT/'cmake-build'),'-j2'],check=True,env=env)
elf=OUT/'cmake-build/dsp_sdk_link'
nm=subprocess.check_output([str(dkp/'devkitARM/bin/arm-none-eabi-nm'),'-g','--defined-only',str(elf)],text=True)
for symbol in ('aptDspSleep','aptDspWakeup','aptDspCancel','SohSdkDspSleep','SohSdkDspWakeup','SohSdkDspCancel'):
    assert sum(line.endswith(' T '+symbol) for line in nm.splitlines())==1,symbol
(OUT/'linked-symbols.txt').write_text(nm)
files=[HERE/n for n in ('sdk_lifecycle_bridge.c','sdk_lifecycle_bridge.h','sdk_lifecycle_probe.cpp','prepare_sdk_lifecycle.py','run_sdk_lifecycle.py','producer_gate.h','producer_gate_3ds.cpp','sdk_ownership.h','sdk_ownership_3ds.c','sdk_ownership_probe.c','ownership_test/3ds.h')]+[OUT/'dsp.o',OUT/'dsp.json',OUT/'bridge-arm11.o',ROOT/'cmake/DspLifecycle3DS.cmake',elf,OUT/'linked-symbols.txt']
(OUT/'validation.json').write_text(json.dumps({'result':r.stdout+ownership.stdout,'scope':'Host dispatch/concurrent publication, actual ARM11 compilation/CMake link and byte-reversible installed SDK symbol adaptation. Does not establish game producer exclusion, NDSP transitions or physical sleep.','sha256':{str(p.relative_to(ROOT)):hashlib.sha256(p.read_bytes()).hexdigest() for p in files}},indent=2)+'\n')
print('PASS: byte-reversible SDK lifecycle adaptation and ARM11 bridge compilation')
