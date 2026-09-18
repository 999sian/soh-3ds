#!/usr/bin/env python3
"""Package opt-in mapped firmware separately from the uploaded v7 probe."""
from pathlib import Path
import subprocess,struct,hashlib,json,shlex,argparse
parser=argparse.ArgumentParser();parser.add_argument("--mailbox-only",action="store_true");args=parser.parse_args()
root=Path(__file__).resolve().parents[3];here=Path(__file__).resolve().parent
validation=root/'builds/dsp-synth-probe-20260909';out=validation/('mailbox-firmware-v14' if args.mailbox_only else 'mapped-v9');out.mkdir(exist_ok=True)
subprocess.run(['c++',*(['-DSOH3DS_DSP_MAILBOX_DIAGNOSTIC'] if args.mailbox_only else []),'-std=c++17','-O2','-fsanitize=undefined','-fno-sanitize-recover=all','-MMD','-MF',str(out/'firmware-export.d'),'-I'+str(root/'research/dsp/teakra/include'),'-I'+str(root/'research/dsp/teakra/src'),str(here/'mapped_firmware_export.cpp'),str(validation/'teakra-build/src/libteakra.a'),'-pthread','-o',str(out/'export-firmware')],check=True)
subprocess.run([str(out/'export-firmware'),str(out/'firmware-words.bin')],check=True)
program=(out/'firmware-words.bin').read_bytes();words=0x7420
if len(program)%2 or len(program)//2>0x20000:raise RuntimeError('program exceeds documented DSP program word space')
# Same DSP1 container layout as the prior native probes; this data segment now
# covers every mapped state/filter word, including aligned endpoint7420.
component=bytearray(0x300)
for i,(kind,data) in enumerate(((0,program),(2,bytes(words*2)))):
    struct.pack_into('<IIII',component,0x120+i*0x30,len(component),0,len(data),kind<<24)
    component[0x130+i*0x30:0x150+i*0x30]=hashlib.sha256(data).digest();component.extend(data)
struct.pack_into('<4sIII',component,0x100,b'DSP1',len(component),0xffff,2<<16)
(out/'synth-mapped.cdc').write_bytes(component)
(out/'mapped_firmware_component.h').write_text('alignas(32) static const unsigned char mapped_firmware_component[] = {\n'+','.join(str(x) for x in component)+'\n};\n')
# Independently reparse the produced file, including segment hashes and overlap.
b=(out/'synth-mapped.cdc').read_bytes();magic,size,_,flags=struct.unpack_from('<4sIII',b,0x100)
assert magic==b'DSP1' and size==len(b) and flags>>16==2
end=0x300
for i,expected in enumerate((program,bytes(words*2))):
    offset,target,length,kind=struct.unpack_from('<IIII',b,0x120+i*0x30)
    assert offset==end and target==0 and kind==(0 if i==0 else 2<<24)
    assert b[offset:offset+length]==expected and hashlib.sha256(expected).digest()==b[0x130+i*0x30:0x150+i*0x30]
    end=offset+length
assert end==len(b)
dependencies=[Path(name).resolve() for name in shlex.split((out/'firmware-export.d').read_text().replace('\\\n',''))[1:]]
dependencies=sorted({p for p in dependencies if p.is_relative_to(root)})
(out/'firmware-package.json').write_text(json.dumps({'signature':'4458','program_words':len(program)//2,'data_words':words,'bytes':len(b),'scope':'Validated DSP1 packaging; native loader acceptance is unverified. No new installer or FTP upload.','sha256':{str(p.relative_to(root)):hashlib.sha256(p.read_bytes()).hexdigest() for p in [*dependencies,here/'build_mapped_firmware.py',validation/'teakra-build/src/libteakra.a',out/'firmware-words.bin',out/'synth-mapped.cdc']}},indent=2)+'\n')
print(f'PASS: mapped DSP1 segment bounds/hashes; {len(program)//2} program words, {words} data words, {len(b)} component bytes')
