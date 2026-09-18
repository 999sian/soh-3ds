#!/usr/bin/env python3
"""Build distinct v9 native mapped correctness probe with production mixer C."""
from pathlib import Path
import argparse,hashlib,json,os,shlex,shutil,struct,subprocess
ROOT=Path(__file__).resolve().parents[3]
HERE=Path(__file__).resolve().parent
parser=argparse.ArgumentParser(description=__doc__);group=parser.add_mutually_exclusive_group();group.add_argument('--stream',action='store_true');group.add_argument('--lifecycle',action='store_true');group.add_argument('--stream-trace',action='store_true');group.add_argument('--stream-io-trace',action='store_true');group.add_argument('--stream-mailbox',action='store_true');args=parser.parse_args();args.stream_io_trace=args.stream_io_trace or args.stream_mailbox;args.stream_trace=args.stream_trace or args.stream_io_trace;args.stream=args.stream or args.stream_trace
OUT=ROOT/('builds/dsp-synth-probe-20260909/mailbox-firmware-v14' if args.stream_mailbox else 'builds/dsp-synth-probe-20260909/mapped-v9')
# Verify the previously generated component and its true dependencies.
package=json.loads((OUT/'firmware-package.json').read_text())
for name,digest in package['sha256'].items():
    assert hashlib.sha256((ROOT/name).read_bytes()).hexdigest()==digest,name
assert package['signature']=='4458'
if args.stream or args.lifecycle:
    original=OUT;OUT=OUT.parent/('lifecycle-v11' if args.lifecycle else 'stream-v14' if args.stream_mailbox else 'stream-v13' if args.stream_io_trace else 'stream-v12' if args.stream_trace else 'stream-v10');OUT.mkdir(exist_ok=True)
    for name in ('mapped_firmware_component.h','synth-mapped.cdc'):shutil.copyfile(original/name,OUT/name)
binary='soh-dsp-lifecycle-probe' if args.lifecycle else 'soh-dsp-stream-probe' if args.stream else 'soh-dsp-mapped-probe'
title='DSP Lifecycle v11' if args.lifecycle else 'DSP Mailbox v14' if args.stream_mailbox else 'DSP Stream IO v13' if args.stream_io_trace else 'DSP Stream Trace v12' if args.stream_trace else 'DSP Stream v10' if args.stream else 'DSP Mapped v9'
title_id='0004000005349000' if args.lifecycle else '0004000005349300' if args.stream_mailbox else '0004000005349200' if args.stream_io_trace else '0004000005349100' if args.stream_trace else '0004000005348f00' if args.stream else '0004000005348e00'
source='lifecycle_hardware_probe.cpp' if args.lifecycle else 'stream_hardware_probe.cpp' if args.stream else 'mapped_hardware_probe.cpp'

dkp=Path(os.environ.get('DEVKITPRO','/home/sian/dkp-root/opt/devkitpro'))
lines=(ROOT/'build-3ds-mk/CMakeFiles/soh_enhancement.dir/flags.make').read_text().splitlines()
flags=[]
for key in ('C_DEFINES','C_INCLUDES','C_FLAGS'):
    flags+=shlex.split(next(line.split(' = ',1)[1] for line in lines if line.startswith(key+' = ')))
subprocess.run([str(dkp/'devkitARM/bin/arm-none-eabi-gcc'),*flags,'-O2','-DSOH3DS_DISABLE_ARM11_ASM','-DSOH3DS_DSP_CAPTURE','-I'+str(HERE),'-MMD','-MF',str(OUT/'mixer-native.d'),'-c',str(ROOT/'third_party/shipwright/soh/soh/mixer.c'),'-o',str(OUT/'mixer-native.o')],check=True)
includes=[]
for key in ('C_INCLUDES',):
    includes+=shlex.split(next(line.split(' = ',1)[1] for line in lines if line.startswith(key+' = ')))
flags=['-O2','-march=armv6k','-mtune=mpcore','-mfpu=vfp','-mfloat-abi=hard','-mtp=soft','-mword-relocations','-ffunction-sections','-fdata-sections','-D__3DS__','-DARM11','-I'+str(dkp/'libctru/include')]
subprocess.run([str(dkp/'devkitARM/bin/arm-none-eabi-g++'),*flags,*includes,'-std=gnu++17','-Wall','-Wextra','-Werror','-MMD','-MF',str(OUT/'mapped-probe-cpu.d'),'-c',str(HERE/'mapped_probe_cpu.cpp'),'-o',str(OUT/'mapped-probe-cpu.o')],check=True)
extra_objects=[]
if args.lifecycle:
    from prepare_sdk_lifecycle import prepare
    extra_objects=[str(prepare(dkp,OUT/'sdk-dsp.o')),str(OUT/'sdk-lifecycle-bridge.o')]
    subprocess.run([str(dkp/'devkitARM/bin/arm-none-eabi-gcc'),*flags,'-std=gnu11','-Wall','-Wextra','-Werror','-MMD','-MF',str(OUT/'sdk-lifecycle-bridge.d'),'-c',str(HERE/'sdk_lifecycle_bridge.c'),'-o',extra_objects[1]],check=True)
    extra_objects.append(str(OUT/'sdk-ownership.o'))
    subprocess.run([str(dkp/'devkitARM/bin/arm-none-eabi-gcc'),*flags,'-std=gnu11','-Wall','-Wextra','-Werror','-MMD','-MF',str(OUT/'sdk-ownership.d'),'-c',str(HERE/'sdk_ownership_3ds.c'),'-o',extra_objects[2]],check=True)
subprocess.run([str(dkp/'devkitARM/bin/arm-none-eabi-g++'),*flags,*(['-DSOH3DS_DSP_MAILBOX_DIAGNOSTIC'] if args.stream_mailbox else []),*(['-DSOH3DS_DSP_STREAM_TRACE'] if args.stream_trace else []),*(['-DSOH3DS_DSP_STREAM_IO_TRACE'] if args.stream_io_trace else []),*includes,'-std=gnu++17','-Wall','-Wextra','-Werror','-I'+str(OUT),'-specs='+str(dkp/'devkitARM/arm-none-eabi/lib/3dsx.specs'),'-MMD','-MF',str(OUT/'mapped-hardware.d'),str(HERE/source),str(OUT/'mixer-native.o'),str(OUT/'mapped-probe-cpu.o'),*extra_objects,'-Wl,--gc-sections','-L'+str(dkp/'libctru/lib'),'-lctru','-lm','-o',str(OUT/(binary+'.elf'))],check=True)
subprocess.run([str(dkp/'tools/bin/3dsxtool'),str(OUT/(binary+'.elf')),str(OUT/(binary+'.3dsx'))],check=True)
# CIA/CXI declares DSP memory mapping explicitly; 3DSX emulator defaults omit it.
rsf=(ROOT/'platform/3ds/cia/app.rsf').read_text()
rsf=rsf.replace('Ship of Harkinian',title).replace('CTR-P-SOH3','CTR-P-DSLC' if args.lifecycle else 'CTR-P-DSMB' if args.stream_mailbox else 'CTR-P-DSIO' if args.stream_io_trace else 'CTR-P-DSTC' if args.stream_trace else 'CTR-P-DSSM' if args.stream else 'CTR-P-DSMP').replace('0x53480','0x53490' if args.lifecycle else '0x53493' if args.stream_mailbox else '0x53492' if args.stream_io_trace else '0x53491' if args.stream_trace else '0x5348f' if args.stream else '0x5348e')
rsf=rsf.replace('SystemMode                    : 80MB','SystemMode                    : 64MB').replace('SystemModeExt                 : 124MB','SystemModeExt                 : Legacy').replace('SpecialMemoryArrange          : true','SpecialMemoryArrange          : false')
# Canonical permissions now include the cache SVCs used by the game runtime.
assert 'InvalidateProcessDataCache: 82' in rsf and 'FlushProcessDataCache: 84' in rsf
(OUT/'probe.rsf').write_text(rsf)
(OUT/'empty-romfs').mkdir(exist_ok=True)
subprocess.run([str(dkp/'tools/bin/smdhtool'),'--create',title,'DSP streaming correctness test' if args.stream else 'Silent DSP correctness and recovery test','SoH 3DS',str(dkp/'libctru/default_icon.png'),str(OUT/'probe.smdh')],check=True)
for fmt,ext in [('ncch','cxi'),('cia','cia')]:
    subprocess.run([str(dkp/'tools/bin/makerom'),'-f',fmt,'-o',str(OUT/(binary+'.'+ext)),
        '-elf',str(OUT/(binary+'.elf')),'-rsf',str(OUT/'probe.rsf'),'-icon',str(OUT/'probe.smdh'),'-banner',str(ROOT/'platform/3ds/cia/banner.bnr'),
        '-DROMFS_ROOT='+str(OUT/'empty-romfs'),'-target','t','-exefslogo'],check=True)
cia=(OUT/(binary+'.cia')).read_bytes()
header,_,_,cert,ticket,tmd,_,size=struct.unpack_from('<IHHIIIIQ',cia)
align=lambda n:(n+63)&~63
content_offset=align(header)+align(cert)+align(ticket)+align(tmd)
content=cia[content_offset:content_offset+size]
assert content==(OUT/(binary+'.cxi')).read_bytes()
assert content[0x100:0x104]==b'NCCH' and content[0x118:0x120][::-1].hex()==title_id
caps=struct.unpack_from('<28I',content,0x570)
# Adjacent static mapping descriptors: writable DSP IO pages1ff00..1ff80.
assert any(caps[i:i+2]==(0xff81ff00,0xff81ff80) for i in range(len(caps)-1)), 'Missing packaged DSP IO mapping'
allowed=set()
for cap in caps:
    if cap & 0xf8000000 == 0xf0000000:
        allowed.update(((cap>>24)&7)*24+i for i in range(24) if cap & (1<<i))
assert {82,84} <= allowed, 'Missing process cache SVC permissions in packaged exheader'
(OUT/'cia-verification.json').write_text(json.dumps({'content_offset':content_offset,'content_sha256':hashlib.sha256(content).hexdigest(),'title_id':title_id,'matches_cxi':True,'dsp_io_mapping':'1ff00000-1ff7ffff','cache_svcs':[82,84]},indent=2)+'\n')
dependencies=set()
for name in ('mixer-native.d','mapped-probe-cpu.d','mapped-hardware.d') + (('sdk-lifecycle-bridge.d','sdk-ownership.d') if args.lifecycle else ()):
    dependency_text=(OUT/name).read_text().replace('\\\n',' ')
    for path in shlex.split(dependency_text.split(':',1)[1]):
        dependency=Path(path).resolve()
        if dependency.is_relative_to(ROOT):dependencies.add(dependency)
if args.lifecycle:
    dependencies.update([HERE/'prepare_sdk_lifecycle.py',OUT/'sdk-dsp.o',OUT/'sdk-dsp.json'])
manifest={str(p.relative_to(ROOT)):hashlib.sha256(p.read_bytes()).hexdigest() for p in
    [*dependencies,ROOT/'build-3ds-mk/CMakeFiles/soh_enhancement.dir/flags.make',HERE/'build_mapped_hardware.py',ROOT/'platform/3ds/cia/app.rsf',ROOT/'platform/3ds/cia/banner.bnr',OUT/'synth-mapped.cdc',OUT/(binary+'.elf'),OUT/(binary+'.3dsx'),OUT/(binary+'.cxi'),OUT/(binary+'.cia'),OUT/'probe.rsf']}
(OUT/'hardware-manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
print(OUT/(binary+'.cia'))
