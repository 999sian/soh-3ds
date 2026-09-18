#!/usr/bin/env python3
"""Exercise the dedicated output worker using real host threads and SDK compile."""
from pathlib import Path
import subprocess,json,hashlib,os
root=Path(__file__).resolve().parents[3];here=Path(__file__).resolve().parent;out=root/'builds/dsp-synth-probe-20260909'
exe=out/'output-worker-probe'
subprocess.run(['c++','-std=c++17','-O2','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-sanitize-recover=all','-I'+str(here/'output_worker_test'),str(here/'output_worker_probe.cpp'),'-pthread','-o',str(exe)],check=True)
r=subprocess.run([str(exe)],capture_output=True,text=True);print(r.stdout,end='');print(r.stderr,end='');r.check_returncode()
arm=out/'output-worker-arm11.cpp';arm.write_text('#include "csnd_stream_3ds.h"\n#include "output_worker_3ds.h"\ntemplate class ResidentDsp::OutputWorker3ds<ResidentDsp::CsndStream>;\n')
dkp=Path(os.environ.get('DEVKITPRO','/home/sian/dkp-root/opt/devkitpro'))
subprocess.run([str(dkp/'devkitARM/bin/arm-none-eabi-g++'),'-std=gnu++17','-O2','-Wall','-Wextra','-Werror','-march=armv6k','-mtune=mpcore','-mfpu=vfp','-mfloat-abi=hard','-mtp=soft','-D__3DS__','-DARM11','-I'+str(dkp/'libctru/include'),'-I'+str(here),'-c',str(arm),'-o',str(out/'output-worker-arm11.o')],check=True)
print('PASS: dedicated CSND output worker ARM11 compilation')
files=[here/'output_worker_3ds.h',here/'pcm_queue.h',here/'csnd_stream_3ds.h',here/'stream_timeline.h',here/'output_worker_probe.cpp',here/'output_worker_test/3ds.h',here/'run_output_worker.py',arm,out/'output-worker-arm11.o']
(out/'output-worker-validation.json').write_text(json.dumps({'result':r.stdout,'scope':'Real host threads with mocked libctru APIs and output, ASan/UBSan; real SDK ARM11 compilation. Not native scheduling, physical streaming, game integration or performance validation.','sha256':{str(p.relative_to(root)):hashlib.sha256(p.read_bytes()).hexdigest() for p in files}},indent=2)+'\n')
