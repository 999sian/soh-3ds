#!/usr/bin/env python3
from pathlib import Path
import subprocess,json,hashlib,os
root=Path(__file__).resolve().parents[3];here=Path(__file__).resolve().parent;out=root/'builds/dsp-synth-probe-20260909'
subprocess.run(['c++','-std=c++17','-O2','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-sanitize-recover=all','-I'+str(here/'transport_test'),str(here/'resident_transport_probe.cpp'),'-o',str(out/'resident-transport-probe')],check=True)
r=subprocess.run([str(out/'resident-transport-probe')],capture_output=True,text=True);print(r.stdout,end='');print(r.stderr,end='');r.check_returncode()
mailbox=out/'mailbox-transport-probe'
subprocess.run(['c++','-std=c++17','-O2','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-sanitize-recover=all','-I'+str(here/'transport_test'),str(here/'mailbox_transport_probe.cpp'),'-o',str(mailbox)],check=True)
mailbox_result=subprocess.run([str(mailbox)],capture_output=True,text=True);print(mailbox_result.stdout,end='');print(mailbox_result.stderr,end='');mailbox_result.check_returncode()
arm=out/'mapped-transport-arm11.cpp';arm.write_text('#include "resident_transport_3ds.h"\nusing namespace ResidentDsp;\nReceive waitMapped(CtrTransport& io,s64 ns){return io.waitForEvent(ns);}\nReceive pollMapped(CtrTransport& io,uint16_t& seq){return io.tryReceive(seq);}\nbool closeMapped(CtrTransport& io){return io.closeAfterStop();}\nbool openMapped(CtrTransport& io,volatile uint16_t* p){return io.open(p,MappedSharedWords);}\ntemplate bool ResidentDsp::initializeMappedTables(ResidentDsp::CtrTransport&,ResidentDsp::Session<ResidentDsp::CtrTransport>&,const int16_t*);\n')
dkp=Path(os.environ.get('DEVKITPRO','/home/sian/dkp-root/opt/devkitpro'))
for mailbox_mode in (False,True):
    subprocess.run([str(dkp/'devkitARM/bin/arm-none-eabi-g++'),*(['-DSOH3DS_DSP_MAILBOX_COMPLETION'] if mailbox_mode else []),'-std=gnu++17','-O2','-Wall','-Wextra','-Werror','-march=armv6k','-mtune=mpcore','-mfpu=vfp','-mfloat-abi=hard','-mtp=soft','-D__3DS__','-DARM11','-I'+str(dkp/'libctru/include'),'-I'+str(here),'-c',str(arm),'-o',str(out/('mailbox-transport-arm11.o' if mailbox_mode else 'mapped-transport-arm11.o'))],check=True)
print('PASS: DR0 and mailbox native transport/table initialization ARM11 compilation')
(out/'native-transport-validation.json').write_text(json.dumps({'result':r.stdout+mailbox_result.stdout,'scope':'Mocked native SVC/IPC behavior and ARM11 compilation; actual mapped loader/service acceptance still pending.','sha256':{str(p.relative_to(root)):hashlib.sha256(p.read_bytes()).hexdigest() for p in [here/'resident_transport_3ds.h',here/'resident_transport_probe.cpp',here/'mailbox_transport_probe.cpp',here/'mapped_bootstrap.h',here/'run_native_transport.py',here/'transport_test/3ds.h']}},indent=2)+'\n')
