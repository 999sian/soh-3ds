#!/usr/bin/env python3
"""Build/run the isolated Teak DSP mixer probe against today's production reference."""
from pathlib import Path
import subprocess
import hashlib
import json

ROOT = Path(__file__).resolve().parents[3]
OUT = ROOT / 'builds/dsp-synth-probe-20260909'
OUT.mkdir(parents=True, exist_ok=True)
source = ROOT / 'third_party/shipwright/soh/soh/mixer.c'
text = source.read_text()
start = text.index('static void aMixImplRef(')
end = text.index('\nvoid aMixImpl(', start)
reference = text[start:end]
header = '''#include <cstdint>
static int16_t oracleMemory[4096];
#define BUF_S16(a) (oracleMemory + (a)/2)
#define ROUND_UP_32(v) (((v) + 31) & ~31)
#define ROUND_DOWN_16(v) ((v) & ~0xf)
static int16_t clamp16(int32_t v) { return v < -32768 ? -32768 : v > 32767 ? 32767 : int16_t(v); }
''' + reference
(OUT / 'production_mix_reference.h').write_text(header)
subprocess.run(['cmake', '-S', str(ROOT/'research/dsp/teakra'), '-B', str(OUT/'teakra-build'),
                '-DTEAKRA_BUILD_UNIT_TESTS=OFF','-DTEAKRA_BUILD_TOOLS=OFF',
                '-DTEAKRA_WARNINGS_AS_ERRORS=OFF','-DCMAKE_BUILD_TYPE=Release'], check=True)
subprocess.run(['cmake','--build',str(OUT/'teakra-build'),'-j8'],check=True)
subprocess.run(['c++','-std=c++17','-O2','-fsanitize=undefined','-fno-sanitize-recover=all',
                '-I'+str(ROOT/'research/dsp/teakra/include'),'-I'+str(ROOT/'research/dsp/teakra/src'),
                '-I'+str(OUT),str(Path(__file__).with_name('mix_probe.cpp')),
                str(OUT/'teakra-build/src/libteakra.a'),'-pthread','-o',str(OUT/'mix-probe')],check=True)
result = subprocess.run([str(OUT/'mix-probe'),str(OUT)],check=True,text=True,capture_output=True)
print(result.stdout,end='')
(OUT/'result.txt').write_text(result.stdout)
(OUT/'reference.json').write_text(json.dumps({'source':str(source.relative_to(ROOT)),
    'sha256':hashlib.sha256(source.read_bytes()).hexdigest(),
    'reference_sha256':hashlib.sha256(reference.encode()).hexdigest(),
    'teakra_revision':subprocess.check_output(['git','-C',str(ROOT/'research/dsp/teakra'),'rev-parse','HEAD'],text=True).strip()},indent=2)+'\n')
