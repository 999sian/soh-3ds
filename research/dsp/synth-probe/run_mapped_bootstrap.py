#!/usr/bin/env python3
from pathlib import Path
import subprocess,json,hashlib
root=Path(__file__).resolve().parents[3];here=Path(__file__).resolve().parent;out=root/'builds/dsp-synth-probe-20260909'
subprocess.run(['c++','-std=c++17','-O2','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-sanitize-recover=all',str(here/'mapped_bootstrap_probe.cpp'),'-o',str(out/'mapped-bootstrap-probe')],check=True)
r=subprocess.run([str(out/'mapped-bootstrap-probe')],capture_output=True,text=True);print(r.stdout,end='');print(r.stderr,end='');r.check_returncode()
(out/'mapped-bootstrap-validation.json').write_text(json.dumps({'result':r.stdout,'scope':'Bootstrap validation and mock cache publication; native service mapping/loading remains to verify.','sha256':{str(p.relative_to(root)):hashlib.sha256(p.read_bytes()).hexdigest() for p in [here/'mapped_bootstrap.h',here/'mapped_bootstrap_probe.cpp',here/'run_mapped_bootstrap.py',here/'resident_session.h']}},indent=2)+'\n')
