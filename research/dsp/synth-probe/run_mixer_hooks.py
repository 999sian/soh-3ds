#!/usr/bin/env python3
"""Exercise opt-in production mixer hooks and compile real ARM11 producer sources."""
from pathlib import Path
import subprocess,json,hashlib,os,shlex,argparse
root=Path(__file__).resolve().parents[3];here=Path(__file__).resolve().parent;out=root/'builds/dsp-synth-probe-20260909';fixture=out/'cpu-replay-fixture'
parser=argparse.ArgumentParser();parser.add_argument('--reuse-cpu',action='store_true');args=parser.parse_args()
if not args.reuse_cpu:subprocess.run(['python3',str(here/'run_cpu_replay.py')],check=True)
production=root/'third_party/shipwright/soh/soh/mixer.c';s=production.read_text();s=s[:s.index('// From here on there are SIMD implementations')]
s=s.replace('#define SOH3DS_AUDIO_GUARD(op, p, n) 1','extern int cpuTestGuard(const void*,uint32_t);\n#define SOH3DS_AUDIO_GUARD(op, p, n) cpuTestGuard(p,n)')
(fixture/'mixer_hooked.c').write_text(s)
(fixture/'opusfile.h').write_text('#include <stdint.h>\n#include <stddef.h>\ntypedef struct OggOpusFile OggOpusFile;\nOggOpusFile* op_open_memory(const unsigned char*,size_t,int*);\nint op_pcm_seek(OggOpusFile*,int64_t);\nint op_read(OggOpusFile*,int16_t*,int,int*);\nvoid op_free(OggOpusFile*);\n')
subprocess.run(['cc','-std=c11','-O2','-U__SSE2__','-DSOH3DS_DSP_CAPTURE','-fsanitize=address,undefined','-fno-sanitize=shift','-fno-sanitize-recover=all','-I'+str(fixture),'-I'+str(here),'-c',str(fixture/'mixer_hooked.c'),'-o',str(fixture/'mixer_hooked.o')],check=True)
subprocess.run(['c++','-std=c++17','-O2','-fsanitize=address,undefined','-fno-sanitize-recover=all','-I'+str(fixture),'-I'+str(root/'research/dsp/teakra/include'),'-I'+str(root/'research/dsp/teakra/src'),str(here/'mixer_hooks_probe.cpp'),str(fixture/'mixer_hooked.o'),str(out/'teakra-build/src/libteakra.a'),'-pthread','-o',str(out/'mixer-hooks-probe')],check=True)
r=subprocess.run([str(out/'mixer-hooks-probe')],capture_output=True,text=True);print(r.stdout,end='');print(r.stderr,end='');r.check_returncode()
# Reuse the configured production flags, but write isolated objects in evidence.
# This neither replaces game build objects nor changes its CMake cache.
dkp=Path(os.environ.get('DEVKITPRO','/home/sian/dkp-root/opt/devkitpro'));native=[]
for target,src in [('soh_enhancement',production),('soh_decomp',root/'third_party/shipwright/soh/src/code/audio_synthesis.c')]:
    flags_path=root/f'build-3ds-mk/CMakeFiles/{target}.dir/flags.make';lines=flags_path.read_text().splitlines();flags=[]
    for key in ('C_DEFINES','C_INCLUDES','C_FLAGS'):
        flags+=shlex.split(next(line.split(' = ',1)[1] for line in lines if line.startswith(key+' = ')))
    for line in lines:
        if src.name+'.obj_DEFINES = ' in line:flags+=['-D'+x for x in line.split(' = ',1)[1].split(';')]
    for enabled in (False,True):
        obj=out/(src.stem+('-hooks-on.o' if enabled else '-hooks-off.o'))
        subprocess.run([str(dkp/'devkitARM/bin/arm-none-eabi-gcc'),*flags,'-O2',*(['-DSOH3DS_DSP_CAPTURE','-I'+str(here)] if enabled else []),'-c',str(src),'-o',str(obj)],check=True)
        nm=subprocess.check_output([str(dkp/'devkitARM/bin/arm-none-eabi-nm'),str(obj)],text=True)
        if enabled and 'SohDspMixer' not in nm:raise RuntimeError('hook symbols missing')
        if not enabled and 'SohDspMixer' in nm:raise RuntimeError('disabled hooks left symbols')
        native.append({'source':str(src.relative_to(root)),'enabled':enabled,'object_sha256':hashlib.sha256(obj.read_bytes()).hexdigest()})
print('PASS: actual mixer.c and audio_synthesis.c ARM11 objects compile with hooks ON/OFF; disabled objects contain no hook symbols')
(out/'mixer-hooks-validation.json').write_text(json.dumps({'result':r.stdout,'native':native,'scope':'Real mixer entry hooks with Teakra callbacks. Native callback installation/output/NDSP recovery still absent. OPUS is an ordering stub, not codec validation.','oracle_limitation':'Original ADPCM shifts retained with shift sanitizer disabled; host pointer guard simulates rejected ranges.','sha256':{str(p.relative_to(root)):hashlib.sha256(p.read_bytes()).hexdigest() for p in [*here.glob('*.h'),here/'mixer_hooks_probe.cpp',here/'run_mixer_hooks.py',production,root/'third_party/shipwright/soh/src/code/audio_synthesis.c',root/'CMakeLists.txt',root/'scripts/build-3ds.sh']}},indent=2)+'\n')
