#!/usr/bin/env python3
"""Check producer owner with real C hooks, Teakra and lifecycle failures."""
from pathlib import Path
import subprocess,json,hashlib,os,argparse
root=Path(__file__).resolve().parents[3];here=Path(__file__).resolve().parent;out=root/'builds/dsp-synth-probe-20260909';fixture=out/'cpu-replay-fixture'
parser=argparse.ArgumentParser();parser.add_argument('--reuse-hooks',action='store_true');parser.add_argument('--mailbox-only',action='store_true');args=parser.parse_args()
package_dir=out/('mailbox-firmware-v14' if args.mailbox_only else 'mapped-v9')
suffix='-mailbox' if args.mailbox_only else ''
if not args.reuse_hooks:subprocess.run(['python3',str(here/'run_mixer_hooks.py')],check=True)
def current_package():
    try:
        package=json.loads((package_dir/'firmware-package.json').read_text())
        return all(hashlib.sha256((root/path).read_bytes()).hexdigest()==digest for path,digest in package['sha256'].items())
    except (OSError,KeyError,ValueError):return False
if not current_package():subprocess.run(['python3',str(here/'build_mapped_firmware.py'),*(['--mailbox-only'] if args.mailbox_only else [])],check=True)
assert current_package(), 'Mapped firmware package verification failed'
stop_exe=out/'component-stop-probe'
subprocess.run(['c++',*(['-DSOH3DS_DSP_MAILBOX_COMPLETION'] if args.mailbox_only else []),'-std=c++17','-O2','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-sanitize-recover=all',str(here/'component_stop_probe.cpp'),'-o',str(stop_exe)],check=True)
subprocess.run([str(stop_exe)],check=True)
exe=out/('mixer-owner'+suffix+'-probe')
subprocess.run(['c++',*(['-DSOH3DS_DSP_MAILBOX_COMPLETION'] if args.mailbox_only else []),'-std=c++17','-O2','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-sanitize-recover=all','-I'+str(fixture),'-I'+str(root/'research/dsp/teakra/include'),'-I'+str(root/'research/dsp/teakra/src'),str(here/'mixer_owner_probe.cpp'),str(fixture/'mixer_hooked.o'),str(out/'teakra-build/src/libteakra.a'),'-pthread','-o',str(exe)],check=True)
r=subprocess.run([str(exe),str(package_dir/'firmware-words.bin')],capture_output=True,text=True);print(r.stdout,end='');print(r.stderr,end='');r.check_returncode()
arm=out/'mixer-owner-arm11.cpp'
arm.write_text('#include "resident_transport_3ds.h"\n#include "mixer_owner.h"\nstruct Cpu {bool read(DspMixerImage&);bool restore(const void*,unsigned);void execute(const ResidentDsp::CpuCall&,const void*);bool canApply(const DspMixerImage&);void apply(const DspMixerImage&);};\nstruct Life {void lock();void unlock();bool startDsp();bool stopDsp();bool startOutput();bool stopOutput();bool startNdsp();bool stopNdsp();bool cancelled();bool outputHealthy();uint64_t now();bool wait(uint64_t);[[noreturn]] void fatal(const char*);};\ntemplate class ResidentDsp::MixerOwner<ResidentDsp::CtrTransport,Cpu,Life>;\n')
dkp=Path(os.environ.get('DEVKITPRO','/home/sian/dkp-root/opt/devkitpro'))
subprocess.run([str(dkp/'devkitARM/bin/arm-none-eabi-g++'),'-std=gnu++17','-O2','-Wall','-Wextra','-Werror','-march=armv6k','-mtune=mpcore','-mfpu=vfp','-mfloat-abi=hard','-mtp=soft','-D__3DS__','-DARM11','-I'+str(dkp/'libctru/include'),'-I'+str(here),'-c',str(arm),'-o',str(out/'mixer-owner-arm11.o')],check=True)
print('PASS: producer lifecycle owner ARM11 compilation')
files=[package_dir/'firmware-words.bin',package_dir/'firmware-package.json',*here.glob('*.h'),here/'mixer_owner_probe.cpp',here/'component_stop_probe.cpp',here/'build_mapped_firmware.py',here/'run_mixer_owner.py',fixture/'mixer_hooked.c',fixture/'mixer_hooked.o',arm,out/'mixer-owner-arm11.o']
(out/('mixer-owner'+suffix+'-validation.json')).write_text(json.dumps({'result':r.stdout,'scope':'Real production C entry hooks and Teakra DSP with mocked lifecycle/output resources and real host mutex exclusion. Native output-worker/lifecycle service implementation and game installation remain pending. Fatal paths checked in child processes, never continued.','oracle_limitation':'Production C retains original signed ADPCM shifts with shift sanitizer excluded in its reference object. OPUS stubs are unreachable in this test.','sha256':{str(p.relative_to(root)):hashlib.sha256(p.read_bytes()).hexdigest() for p in files}},indent=2)+'\n')
