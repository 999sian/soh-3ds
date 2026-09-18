#!/usr/bin/env python3
"""Verify the generated Old CIA CPU descriptor and its opt-in reset behavior."""
from pathlib import Path
import subprocess
import tempfile
import re

ROOT = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='soh-cpu-descriptor-') as directory:
    p = Path(directory)
    for name in ['main.cpp', 'src/setup/setup_prebuilt_3ds.cpp', 'src/compat3ds/compact_zip.cpp']:
        file = p / name
        file.parent.mkdir(parents=True, exist_ok=True)
        file.write_text('')
    rsf = p / 'platform/3ds/cia/app.rsf'
    rsf.parent.mkdir(parents=True)
    canonical = (ROOT / 'platform/3ds/cia/app.rsf').read_text()
    rsf.write_text(canonical)
    (p / 'CMakeLists.txt').write_text('cmake_minimum_required(VERSION 3.18)\n'
                                    'project(DescriptorFixture CXX)\n'
                                    'add_executable(soh_3ds main.cpp)\n'
                                    f'include("{ROOT / "cmake/Old3DS.cmake"}")\n')
    for flag, expected in [(None, 0x9e), ('ON', 0xd0), ('OFF', 0x9e)]:
        command = ['cmake', '-S', str(p), '-B', str(p / 'build')]
        if flag is not None:
            command.append('-DSOH3DS_OLD_CPU80:BOOL=' + flag)
        subprocess.run(command, check=True, stdout=subprocess.DEVNULL)
        generated = (p / 'build/old3ds.rsf').read_text()
        descriptor = int(re.search(r'^\s*MaxCpu\s*:\s*(0x[0-9a-fA-F]+)', generated, re.M)[1], 16)
        assert descriptor == expected, (flag, hex(descriptor), hex(expected))
        # Keep scheduler mode, memory layout and every unrelated permission.
        assert descriptor & 0x80
        restore_descriptor = re.sub(r'(MaxCpu\s*:\s*)0x[0-9a-fA-F]+', r'\g<1>0x9E', generated)
        assert restore_descriptor == canonical.replace('SystemModeExt                 : 124MB',
                                                       'SystemModeExt                 : 178MB')
    print('Old CIA descriptor: default 30, opt-in 80, reset 30; other permissions unchanged')
