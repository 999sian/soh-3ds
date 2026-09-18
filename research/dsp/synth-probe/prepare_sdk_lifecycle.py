#!/usr/bin/env python3
"""Extract the installed SDK DSP object and rename only APT lifecycle entry points."""
from pathlib import Path
import argparse
import hashlib
import json
import subprocess
import tempfile

def prepare(dkp, output):
    dkp, output = Path(dkp).resolve(), Path(output).resolve()
    archive = dkp / 'libctru/lib/libctru.a'
    prefix = dkp / 'devkitARM/bin/arm-none-eabi-'
    ar, nm, objcopy = (str(prefix) + name for name in ('ar', 'nm', 'objcopy'))
    members = subprocess.check_output([ar, 't', str(archive)], text=True).splitlines()
    if members.count('dsp.o') != 1:
        raise RuntimeError('Expected exactly one SDK dsp.o; inspect SDK layout before adapting')
    original = subprocess.check_output([ar, 'p', str(archive), 'dsp.o'])
    renames = {'aptDspSleep': 'SohSdkDspSleep', 'aptDspWakeup': 'SohSdkDspWakeup',
               'aptDspCancel': 'SohSdkDspCancel', 'svcSendSyncRequest': 'SohDspSvcSendSyncRequest'}
    output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(dir=output.parent) as temporary:
        source, adapted = Path(temporary) / 'dsp.o', Path(temporary) / 'adapted.o'
        source.write_bytes(original)
        def symbols(path):
            rows = subprocess.check_output([nm, '-g', '--defined-only', str(path)], text=True)
            return {row.split()[2]: row.split()[:2] for row in rows.splitlines() if row.strip()}
        before = symbols(source)
        if not all(before.get(name, [None, None])[1] == 'T' for name in renames if name!='svcSendSyncRequest'):
            raise RuntimeError('Missing strong SDK lifecycle definitions')
        undefined=subprocess.check_output([nm,'-u',str(source)],text=True).splitlines()
        if not any(row.split()[-1]=='svcSendSyncRequest' for row in undefined):
            raise RuntimeError('SDK DSP object has no observable IPC boundary')
        subprocess.run([objcopy, *['--redefine-sym=' + a + '=' + b for a, b in renames.items()],
                        str(source), str(adapted)], check=True)
        after = symbols(adapted)
        expected = {renames.get(name, name): value for name, value in before.items()}
        if after != expected:
            raise RuntimeError('Unexpected SDK symbol change')
        undefined=subprocess.check_output([nm,'-u',str(adapted)],text=True).splitlines()
        if not any(row.split()[-1]=='SohDspSvcSendSyncRequest' for row in undefined) or any(row.split()[-1]=='svcSendSyncRequest' for row in undefined):
            raise RuntimeError('SDK IPC boundary was not redirected exactly')
        # Round trip must reconstruct the original object byte-for-byte. This
        # also checks code/data/relocations were not accidentally discarded.
        restored = Path(temporary) / 'restored.o'
        subprocess.run([objcopy, *['--redefine-sym=' + b + '=' + a for a, b in renames.items()],
                        str(adapted), str(restored)], check=True)
        if restored.read_bytes() != original:
            raise RuntimeError('SDK symbol rename is not byte-reversible')
        output.write_bytes(adapted.read_bytes())
    evidence = {'sdk_archive': str(archive), 'sdk_archive_sha256': hashlib.sha256(archive.read_bytes()).hexdigest(),
                'original_member_sha256': hashlib.sha256(original).hexdigest(),
                'adapted_sha256': hashlib.sha256(output.read_bytes()).hexdigest(),
                'renames': renames, 'other_global_symbols_preserved': True, 'byte_reversible': True}
    output.with_suffix('.json').write_text(json.dumps(evidence, indent=2) + '\n')
    return output

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--devkitpro', required=True)
    parser.add_argument('--output', required=True)
    args = parser.parse_args()
    print(prepare(args.devkitpro, args.output))
