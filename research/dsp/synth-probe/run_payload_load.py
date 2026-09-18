#!/usr/bin/env python3
"""Verify ordered immutable-payload loads with complete resident memory guards."""
from pathlib import Path
import subprocess
root=Path(__file__).resolve().parents[3];here=Path(__file__).resolve().parent
out=root/'builds/dsp-synth-probe-20260909'
subprocess.run(['c++','-std=c++17','-O2','-fsanitize=undefined','-fno-sanitize-recover=all','-I'+str(root/'research/dsp/teakra/include'),'-I'+str(root/'research/dsp/teakra/src'),str(here/'payload_load_probe.cpp'),str(out/'teakra-build/src/libteakra.a'),'-pthread','-o',str(out/'payload-load-probe')],check=True)
r=subprocess.run([str(out/'payload-load-probe')],capture_output=True,text=True)
print(r.stdout,end='');print(r.stderr,end='');r.check_returncode()
(out/'payload-load-result.txt').write_text(r.stdout)
