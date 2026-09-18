#!/usr/bin/env python3
"""Verify firmware's actual dependencies and embed its verified component bytes."""
from pathlib import Path
import argparse
import hashlib
import json
import subprocess
ROOT=Path(__file__).resolve().parents[3]
HERE=Path(__file__).resolve().parent
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--output',required=True)
parser.add_argument('--mailbox-only',action='store_true')
args=parser.parse_args()
PACKAGE=ROOT/('builds/dsp-synth-probe-20260909/mailbox-firmware-v14' if args.mailbox_only else 'builds/dsp-synth-probe-20260909/mapped-v9')
def current():
    try:
        manifest=json.loads((PACKAGE/'firmware-package.json').read_text())
        return manifest['signature']=='4458' and all(hashlib.sha256((ROOT/p).read_bytes()).hexdigest()==h for p,h in manifest['sha256'].items())
    except (OSError,KeyError,ValueError):return False
if not current():subprocess.run(['python3',str(HERE/'build_mapped_firmware.py'),*(['--mailbox-only'] if args.mailbox_only else [])],check=True)
if not current():raise RuntimeError('Mapped firmware dependencies are not verified')
data=(PACKAGE/'synth-mapped.cdc').read_bytes()
text=('#define SOH3DS_DSP_MAILBOX_COMPLETION 1\n' if args.mailbox_only else '')+'static constexpr bool mapped_firmware_mailbox_completion = '+str(args.mailbox_only).lower()+';\n'+'alignas(32) static const unsigned char mapped_firmware_component[] = {\n'+','.join(str(x) for x in data)+'\n};\n'
output=Path(args.output);output.parent.mkdir(parents=True,exist_ok=True)
if not output.exists() or output.read_text()!=text:output.write_text(text)
print('Verified mapped component SHA256',hashlib.sha256(data).hexdigest())
