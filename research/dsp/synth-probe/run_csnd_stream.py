#!/usr/bin/env python3
"""Check experimental CSND streaming ownership; no physical playback claim."""
from pathlib import Path
import subprocess,json,hashlib,os
root=Path(__file__).resolve().parents[3];here=Path(__file__).resolve().parent
out=root/'builds/dsp-synth-probe-20260909';results=[]
for name in ('stream_timeline','csnd_stream'):
    exe=out/(name+'-probe')
    subprocess.run(['c++','-std=c++17','-O2','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-sanitize-recover=all','-I'+str(here/'csnd_test'),str(here/(name+'_probe.cpp')),'-o',str(exe)],check=True)
    r=subprocess.run([str(exe)],capture_output=True,text=True);print(r.stdout,end='');print(r.stderr,end='');r.check_returncode();results.append(r.stdout)
arm=out/'csnd-stream-arm11.cpp'
arm.write_text('#include "csnd_stream_3ds.h"\nusing namespace ResidentDsp;\nbool openStream(CsndStream& s,const int16_t* p){return s.open(32000) && s.start(p,4096);}\nCsndStream::Push pushStream(CsndStream& s,const int16_t* p){return s.push(p,512);}\nbool serviceStream(CsndStream& s){return s.service();}\nbool stopStream(CsndStream& s){return s.stop();}\n')
dkp=Path(os.environ.get('DEVKITPRO','/home/sian/dkp-root/opt/devkitpro'))
subprocess.run([str(dkp/'devkitARM/bin/arm-none-eabi-g++'),'-std=gnu++17','-O2','-Wall','-Wextra','-Werror','-march=armv6k','-mtune=mpcore','-mfpu=vfp','-mfloat-abi=hard','-mtp=soft','-D__3DS__','-DARM11','-I'+str(dkp/'libctru/include'),'-I'+str(here),'-c',str(arm),'-o',str(out/'csnd-stream-arm11.o')],check=True)
print('PASS: experimental CSND streaming adapter ARM11 compilation')
files=[here/'stream_timeline.h',here/'stream_timeline_probe.cpp',here/'csnd_stream_3ds.h',here/'csnd_stream_probe.cpp',here/'csnd_test/3ds.h',here/'run_csnd_stream.py',arm,out/'csnd-stream-arm11.o']
(out/'csnd-stream-validation.json').write_text(json.dumps({'results':results,'scope':'ASan/UBSan mocked native CSND and timeline arithmetic plus SDK ARM11 compilation. No physical clock/playback/prefetch/scheduling validation and no game output installation.','sha256':{str(p.relative_to(root)):hashlib.sha256(p.read_bytes()).hexdigest() for p in files}},indent=2)+'\n')
