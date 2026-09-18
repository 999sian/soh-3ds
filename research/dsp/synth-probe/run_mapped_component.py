#!/usr/bin/env python3
"""Validate reusable native component ownership and instantiate against the SDK."""
from pathlib import Path
import hashlib
import json
import os
import subprocess

ROOT = Path(__file__).resolve().parents[3]
HERE = Path(__file__).resolve().parent
OUT = ROOT / 'builds/dsp-synth-probe-20260909'
binary = OUT / 'mapped-component-probe'
subprocess.run([
    'c++', '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror',
    '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
    '-I' + str(HERE / 'component_test'), str(HERE / 'mapped_component_probe.cpp'),
    '-o', str(binary),
], check=True)
result = subprocess.run([str(binary)], capture_output=True, text=True)
print(result.stdout, end='')
print(result.stderr, end='')
result.check_returncode()
arm = OUT / 'mapped-component-arm11.cpp'
arm.write_text('''#include "mapped_component_3ds.h"
bool boot(ResidentDsp::MappedComponent3ds& component,const void* firmware,unsigned size,const int16_t* table){
    return component.start(firmware,size,table,[](){return false;});
}
bool stop(ResidentDsp::MappedComponent3ds& component){return component.stop();}
''')
dkp = Path(os.environ.get('DEVKITPRO', '/home/sian/dkp-root/opt/devkitpro'))
subprocess.run([
    str(dkp / 'devkitARM/bin/arm-none-eabi-g++'), '-std=gnu++17', '-O2',
    '-Wall', '-Wextra', '-Werror', '-march=armv6k', '-mtune=mpcore', '-mfpu=vfp',
    '-mfloat-abi=hard', '-mtp=soft', '-D__3DS__', '-DARM11',
    '-I' + str(dkp / 'libctru/include'), '-I' + str(HERE), '-c', str(arm),
    '-o', str(OUT / 'mapped-component-arm11.o'),
], check=True)
print('PASS: reusable native mapped component ARM11 compilation')
files = [HERE / name for name in (
    'mapped_component_3ds.h', 'mapped_component_probe.cpp', 'component_test/3ds.h',
    'transport_test/3ds.h', 'resident_transport_3ds.h', 'resident_session.h',
    'mapped_bootstrap.h', 'component_stop_guard.h', 'run_mapped_component.py',
)] + [arm, OUT / 'mapped-component-arm11.o']
(OUT / 'mapped-component-validation.json').write_text(json.dumps({
    'result': result.stdout,
    'scope': 'Host SDK fault injection with ASan/UBSan and real ARM11 SDK compilation. APT exclusion is a caller precondition; game integration and physical playback/performance remain unverified.',
    'sha256': {str(p.relative_to(ROOT)): hashlib.sha256(p.read_bytes()).hexdigest() for p in files},
}, indent=2) + '\n')
