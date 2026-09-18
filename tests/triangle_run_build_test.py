#!/usr/bin/env python3
"""Real fast/CMakeLists must set the run macro on the parent-created target."""
from pathlib import Path
import json
import subprocess
import tempfile
ROOT=Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='soh-triangle-run-cmake-') as directory:
    p=Path(directory);(p/'src/fast').mkdir(parents=True)
    (p/'CMakeLists.txt').write_text('cmake_minimum_required(VERSION 3.24)\nproject(RunFlags CXX)\nset(N3DS TRUE)\nset(CMAKE_EXPORT_COMPILE_COMMANDS ON)\nadd_subdirectory(src)\n')
    (p/'src/CMakeLists.txt').write_text('add_library(libultraship STATIC empty.cpp)\nadd_subdirectory(fast)\n')
    (p/'src/empty.cpp').write_text('int dummy;\n')
    (p/'src/fast/interpreter.cpp').write_text('int interpreter;\n')
    (p/'src/fast/CMakeLists.txt').write_text((ROOT/'third_party/libultraship/src/fast/CMakeLists.txt').read_text())
    for enabled in ('OFF','ON','OFF'):
        subprocess.run(['cmake','-S',str(p),'-B',str(p/'build'),'-DSOH3DS_TRIANGLE_RUN_REUSE='+enabled],check=True,stdout=subprocess.DEVNULL)
        commands=json.loads((p/'build/compile_commands.json').read_text())
        cmd=next(c['command'] for c in commands if c['file'].endswith('/interpreter.cpp'))
        assert ('-DSOH3DS_TRIANGLE_RUN_REUSE=1' in cmd)==(enabled=='ON'),(enabled,cmd)
    print('PASS: actual fast/CMakeLists sets parent-target interpreter flag OFF/ON/OFF')
