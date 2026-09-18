#!/usr/bin/env python3
"""Validate firmware and build a standalone 3DS DSP transfer benchmark."""
from pathlib import Path
import argparse,hashlib,json,os,shutil,struct,subprocess
ROOT=Path(__file__).resolve().parents[3]
HERE=Path(__file__).resolve().parent
OUT=ROOT/'builds/dsp-synth-probe-20260909'
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--typed',action='store_true',help='Build v5 resident pipeline probe in separate output directory')
parser.add_argument('--extended',action='store_true',help='Build v6 envelope/filter chain probe in separate output directory')
parser.add_argument('--multiblock',action='store_true',help='Build v7 multiblock firmware plus quiet CSND output proof')
parser.add_argument('--events',action='store_true',help='Build v8 native completion-event transport and output proof')
args=parser.parse_args()
if args.events:args.multiblock=True
if args.multiblock:args.extended=True
if args.extended:args.typed=True
if args.typed:
    subprocess.run(['python3',str(HERE/'run_typed.py')],check=True)
    validation=OUT
    OUT=OUT/('events-v8' if args.events else 'multiblock-v7' if args.multiblock else 'extended-v6' if args.extended else 'typed-v5');OUT.mkdir(exist_ok=True)
    firmware='completion-firmware-words.bin' if args.events else 'multiblock-firmware-words.bin' if args.multiblock else 'extended-firmware-words.bin' if args.extended else 'typed-firmware-words.bin'
    fixture='chain_fixture.h' if args.extended else 'pipeline_fixture.h'
    for source,target in [(firmware,'firmware-words.bin'),(fixture,fixture),('typed-validation.json','typed-validation.json')]:
        shutil.copyfile(validation/source,OUT/target)
else:
    subprocess.run(['python3',str(HERE/'run_probe.py')],check=True)
    subprocess.run(['c++','-std=c++17','-O2','-fsanitize=undefined','-fno-sanitize-recover=all',
        '-I'+str(ROOT/'research/dsp/teakra/include'),'-I'+str(ROOT/'research/dsp/teakra/src'),'-I'+str(OUT),
        str(HERE/'firmware_probe.cpp'),str(OUT/'teakra-build/src/libteakra.a'),'-pthread','-o',str(OUT/'firmware-probe')],check=True)
    r=subprocess.run([str(OUT/'firmware-probe'),str(OUT)],check=True,capture_output=True,text=True)
    print(r.stdout,end='');(OUT/'firmware-result.txt').write_text(r.stdout)
# DSP1 layout matches Teakra's makedsp1. Unsigned research firmware: loader may reject it.
program=(OUT/'firmware-words.bin').read_bytes()
segments=[(0,0,program),(2,0,bytes(0x8200 if args.typed else 0x3000))]
component=bytearray(0x300)
for i,(kind,target,data) in enumerate(segments):
    struct.pack_into('<IIII',component,0x120+i*0x30,len(component),target,len(data),kind<<24)
    component[0x130+i*0x30:0x150+i*0x30]=hashlib.sha256(data).digest()
    component.extend(data)
struct.pack_into('<4sIII',component,0x100,b'DSP1',len(component),0xffff,len(segments)<<16)
(OUT/'synth-probe.cdc').write_bytes(component)
(OUT/'firmware_component.h').write_text('alignas(32) static const unsigned char firmware_component[] = {\n'+
    ','.join(str(x) for x in component)+'\n};\n')
dkp=Path(os.environ.get('DEVKITPRO','/home/sian/dkp-root/opt/devkitpro'))
flags=['-O2','-march=armv6k','-mtune=mpcore','-mfpu=vfp','-mfloat-abi=hard','-mtp=soft','-mword-relocations',
    '-D__3DS__','-DARM11','-I'+str(dkp/'libctru/include')]
if args.extended:flags.append('-DSOH_DSP_EXTENDED_PROBE')
if args.multiblock:flags.append('-DSOH_DSP_MULTIBLOCK_PROBE')
if args.events:flags.append('-DSOH_DSP_EVENT_PROBE')
subprocess.run([str(dkp/'devkitARM/bin/arm-none-eabi-gcc'),*flags,'-c',str(ROOT/'src/compat3ds/arm11/audio_mix.S'),'-o',str(OUT/'audio_mix.o')],check=True)
subprocess.run([str(dkp/'devkitARM/bin/arm-none-eabi-g++'),*flags,'-std=gnu++17','-Wall','-Wextra','-Werror',
    '-I'+str(OUT),'-specs='+str(dkp/'devkitARM/arm-none-eabi/lib/3dsx.specs'),str(HERE/('typed_hardware_probe.cpp' if args.typed else 'hardware_probe.cpp')),str(OUT/'audio_mix.o'),
    '-L'+str(dkp/'libctru/lib'),'-lctru','-lm','-o',str(OUT/'soh-dsp-synth-probe.elf')],check=True)
subprocess.run([str(dkp/'tools/bin/3dsxtool'),str(OUT/'soh-dsp-synth-probe.elf'),str(OUT/'soh-dsp-synth-probe.3dsx')],check=True)
# CIA/CXI declares DSP memory mapping explicitly; 3DSX emulator defaults omit it.
rsf=(ROOT/'platform/3ds/cia/app.rsf').read_text()
rsf=rsf.replace('Ship of Harkinian','DSP Synth Probe').replace('CTR-P-SOH3','CTR-P-DSPT').replace('0x53480','0x5348d')
rsf=rsf.replace('SystemMode                    : 80MB','SystemMode                    : 64MB').replace('SystemModeExt                 : 124MB','SystemModeExt                 : Legacy').replace('SpecialMemoryArrange          : true','SpecialMemoryArrange          : false')
rsf=rsf.replace('  SystemCallAccess: ', '  SystemCallAccess: \n    InvalidateProcessDataCache: 82\n    FlushProcessDataCache: 84')
assert 'InvalidateProcessDataCache: 82' in rsf and 'FlushProcessDataCache: 84' in rsf
(OUT/'probe.rsf').write_text(rsf)
(OUT/'empty-romfs').mkdir(exist_ok=True)
subprocess.run([str(dkp/'tools/bin/smdhtool'),'--create','DSP Synth Probe','Silent DSP CPU-offload benchmark','SoH 3DS',str(dkp/'libctru/default_icon.png'),str(OUT/'probe.smdh')],check=True)
for fmt,ext in [('ncch','cxi'),('cia','cia')]:
    subprocess.run([str(dkp/'tools/bin/makerom'),'-f',fmt,'-o',str(OUT/('soh-dsp-synth-probe.'+ext)),
        '-elf',str(OUT/'soh-dsp-synth-probe.elf'),'-rsf',str(OUT/'probe.rsf'),'-icon',str(OUT/'probe.smdh'),'-banner',str(ROOT/'platform/3ds/cia/banner.bnr'),
        '-DROMFS_ROOT='+str(OUT/'empty-romfs'),'-target','t','-exefslogo'],check=True)
cia=(OUT/'soh-dsp-synth-probe.cia').read_bytes()
header,_,_,cert,ticket,tmd,_,size=struct.unpack_from('<IHHIIIIQ',cia)
align=lambda n:(n+63)&~63
content_offset=align(header)+align(cert)+align(ticket)+align(tmd)
content=cia[content_offset:content_offset+size]
assert content==(OUT/'soh-dsp-synth-probe.cxi').read_bytes()
assert content[0x100:0x104]==b'NCCH' and content[0x118:0x120][::-1].hex()=='0004000005348d00'
caps=struct.unpack_from('<28I',content,0x570)
allowed=set()
for cap in caps:
    if cap & 0xf8000000 == 0xf0000000:
        allowed.update(((cap>>24)&7)*24+i for i in range(24) if cap & (1<<i))
assert {82,84} <= allowed, 'Missing process cache SVC permissions in packaged exheader'
(OUT/'cia-verification.json').write_text(json.dumps({'content_offset':content_offset,'content_sha256':hashlib.sha256(content).hexdigest(),'title_id':'0004000005348d00','matches_cxi':True},indent=2)+'\n')
manifest={str(p.relative_to(ROOT)):hashlib.sha256(p.read_bytes()).hexdigest() for p in
    [*HERE.glob('*.cpp'),*HERE.glob('*.h'),*HERE.glob('*.py'),HERE/'transport_test/3ds.h',OUT/'synth-probe.cdc',OUT/'soh-dsp-synth-probe.elf',OUT/'soh-dsp-synth-probe.3dsx',OUT/'soh-dsp-synth-probe.cxi',OUT/'soh-dsp-synth-probe.cia',OUT/'probe.rsf']}
(OUT/'hardware-manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
print(OUT/'soh-dsp-synth-probe.cia')
