#!/usr/bin/env python3
"""Run genuine setup extraction; compare every payload and enforce a host RSS bound.

Requires an existing host Torch build and the user's own ROM/reference archive.
No copyrighted data is copied into the repository.
"""
import argparse, json, pathlib, resource, subprocess, time, tempfile
p = argparse.ArgumentParser()
p.add_argument('--exe', default='/var/tmp/setup-torch-extract')
p.add_argument('--rom', required=True); p.add_argument('--metadata', required=True)
p.add_argument('--reference', required=True); p.add_argument('--output', required=True)
p.add_argument('--version', default='9.2.3'); p.add_argument('--max-rss-kib', type=int, default=100000)
a = p.parse_args(); out = pathlib.Path(a.output); out.mkdir(parents=True,exist_ok=True)
command = [a.exe, a.rom, a.metadata, str(out), a.version]
start = time.monotonic()
with (out/'extract.log').open('w') as log:
    proc = subprocess.Popen(command, stdout=log, stderr=subprocess.STDOUT)
    _, status, usage = __import__('os').wait4(proc.pid,0)
    proc.returncode = __import__('os').waitstatus_to_exitcode(status)
    assert proc.returncode == 0, (out/'extract.log').read_text()
result = {'peak_rss_kib': usage.ru_maxrss, 'elapsed_seconds': time.monotonic()-start}
(out/'measurement.json').write_text(json.dumps(result,indent=2)+'\n')
(out/'rss-kib.txt').write_text(str(usage.ru_maxrss))
root=pathlib.Path(__file__).resolve().parent
archive = 'oot-mq.o2r' if (out/'oot-mq.o2r').exists() else 'oot.o2r'
subprocess.run(['python3',str(root/'setup_torch_compare.py'),a.reference,str(out/archive),
                '--rss-file',str(out/'rss-kib.txt'),'--max-rss-kib',str(a.max_rss_kib)],check=True)
# Exercise cancelled export, missing metadata, and successful retry in one process.
with (out/'retry.log').open('w') as log:
    subprocess.run(command+['cancel-retry'],stdout=log,stderr=subprocess.STDOUT,check=True)
subprocess.run(['python3',str(root/'setup_torch_compare.py'),a.reference,str(out/archive)],check=True)
with tempfile.TemporaryDirectory(prefix='setup-torch-oversized-') as temp:
    oversized = pathlib.Path(temp)/'oversized.z64'
    with oversized.open('wb') as f: f.truncate(64*1024*1024)
    refused = subprocess.run([a.exe,str(oversized),a.metadata,str(out),a.version],capture_output=True,text=True)
    assert refused.returncode != 0 and 'up to 32 MiB' in refused.stderr, refused.stderr
print(json.dumps(result))
