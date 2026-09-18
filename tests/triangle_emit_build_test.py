#!/usr/bin/env python3
"""Check real parent-target CMake wiring and canonical script reset behaviour."""
from pathlib import Path
import json
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='soh-triangle-emit-cmake-') as directory:
    p = Path(directory)
    (p/'src/fast').mkdir(parents=True)
    (p/'CMakeLists.txt').write_text('cmake_minimum_required(VERSION 3.24)\nproject(EmitFlags CXX ASM)\nset(N3DS TRUE)\nset(CMAKE_EXPORT_COMPILE_COMMANDS ON)\nadd_subdirectory(src)\n')
    (p/'src/CMakeLists.txt').write_text('add_library(libultraship STATIC empty.cpp)\nadd_subdirectory(fast)\n')
    (p/'src/empty.cpp').write_text('int dummy;\n')
    (p/'src/fast/interpreter.cpp').write_text('int interpreter;\n')
    (p/'src/fast/triangle_emit_arm11.S').write_text('')
    (p/'src/fast/CMakeLists.txt').write_text((ROOT/'third_party/libultraship/src/fast/CMakeLists.txt').read_text())
    for enabled in ('OFF', 'ON', 'OFF'):
        subprocess.run(['cmake', '-S', str(p), '-B', str(p/'build'), '-DSOH3DS_ARM11_TRIANGLE_EMIT='+enabled],
                       check=True, stdout=subprocess.DEVNULL)
        commands = json.loads((p/'build/compile_commands.json').read_text())
        cmd = next(c['command'] for c in commands if c['file'].endswith('/interpreter.cpp'))
        assert ('-DSOH3DS_ARM11_TRIANGLE_EMIT=1' in cmd) == (enabled == 'ON'), (enabled, cmd)
        assert any(c['file'].endswith('/triangle_emit_arm11.S') for c in commands) == (enabled == 'ON')
    script = (ROOT/'scripts/build-3ds.sh').read_text()
    assert '-DSOH3DS_ARM11_TRIANGLE_EMIT:BOOL="${SOH3DS_ARM11_TRIANGLE_EMIT:-OFF}"' in script
print('PASS: emitter parent-target macro and ASM source OFF/ON/OFF; canonical build defaults OFF')
