#!/usr/bin/env python3
"""Both compilation trees must enable and reset the compact-stream experiment."""
from pathlib import Path
import json
import re
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
backend = re.search(r'    if\(SOH3DS_COMPACT_VERTEX_STREAM\).*?    endif\(\)',
                    (root / 'CMakeLists.txt').read_text(), re.S).group(0)
with tempfile.TemporaryDirectory(prefix='soh-compact-cmake-') as directory:
    p = Path(directory)
    (p / 'src/fast').mkdir(parents=True)
    (p / 'CMakeLists.txt').write_text(
        'cmake_minimum_required(VERSION 3.24)\nproject(CompactFlags CXX)\n'
        'set(N3DS TRUE)\nset(CMAKE_EXPORT_COMPILE_COMMANDS ON)\n'
        'add_subdirectory(src)\nadd_library(mk_gfx_citro3d STATIC backend.cpp)\n' + backend + '\n')
    (p / 'backend.cpp').write_text('int backend;\n')
    (p / 'src/CMakeLists.txt').write_text('add_library(libultraship STATIC empty.cpp)\nadd_subdirectory(fast)\n')
    (p / 'src/empty.cpp').write_text('int empty;\n')
    (p / 'src/fast/interpreter.cpp').write_text('int interpreter;\n')
    (p / 'src/fast/CMakeLists.txt').write_text((root / 'third_party/libultraship/src/fast/CMakeLists.txt').read_text())
    for enabled in ('OFF', 'ON', 'OFF'):
        subprocess.run(['cmake', '-S', str(p), '-B', str(p / 'build'),
                        '-DSOH3DS_COMPACT_VERTEX_STREAM=' + enabled], check=True, stdout=subprocess.DEVNULL)
        commands = json.loads((p / 'build/compile_commands.json').read_text())
        for file in ('/interpreter.cpp', '/backend.cpp'):
            command = next(c['command'] for c in commands if c['file'].endswith(file))
            assert ('-DSOH3DS_COMPACT_VERTEX_STREAM=1' in command) == (enabled == 'ON'), command
    script = (root / 'scripts/build-3ds.sh').read_text()
    assert script.count('-DSOH3DS_COMPACT_VERTEX_STREAM:BOOL="${SOH3DS_COMPACT_VERTEX_STREAM:-OFF}"') == 2
print('Compact stream option: both targets OFF/ON/OFF; canonical script resets both trees')
