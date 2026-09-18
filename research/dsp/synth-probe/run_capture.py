#!/usr/bin/env python3
"""Check bounded command capture ownership and deferred host save dependencies."""
from pathlib import Path
import subprocess,json,hashlib
root=Path(__file__).resolve().parents[3];here=Path(__file__).resolve().parent
out=root/'builds/dsp-synth-probe-20260909'
subprocess.run(['c++','-std=c++17','-O2','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-sanitize-recover=all',str(here/'chunk_capture_probe.cpp'),'-o',str(out/'chunk-capture-probe')],check=True)
r=subprocess.run([str(out/'chunk-capture-probe')],capture_output=True,text=True)
print(r.stdout,end='');print(r.stderr,end='');r.check_returncode()
(out/'capture-validation.json').write_text(json.dumps({'result':r.stdout,'scope':'Host capture component; not DSP execution, CPU replay or game integration.','sha256':{str(p.relative_to(root)):hashlib.sha256(p.read_bytes()).hexdigest() for p in [here/'chunk_capture.h',here/'chunk_capture_probe.cpp',here/'mapped_parameters.h',here/'dmem_layout.h',here/'resident_session.h',here/'run_capture.py']}},indent=2)+'\n')
