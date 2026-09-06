#!/usr/bin/env python3
"""Compare every payload, including duplicate occurrence order, independently of ZIP order."""
import argparse, collections, hashlib, zipfile
p = argparse.ArgumentParser()
p.add_argument('reference'); p.add_argument('actual'); p.add_argument('--rss-file'); p.add_argument('--max-rss-kib', type=int, default=120000)
a = p.parse_args()
def entries(path, stored=False):
    result = collections.defaultdict(list)
    with zipfile.ZipFile(path) as z:
        for i in z.infolist():
            if stored: assert i.compress_type == zipfile.ZIP_STORED, i.filename
            result[i.filename].append(hashlib.sha256(z.read(i)).hexdigest())
    return dict(result)
r, s = entries(a.reference), entries(a.actual, True)
assert r == s, {'missing': sorted(r.keys()-s.keys())[:20], 'extra': sorted(s.keys()-r.keys())[:20], 'changed': [k for k in r.keys()&s.keys() if r[k]!=s[k]][:30]}
if a.rss_file:
    rss = int(open(a.rss_file).read().strip())
    assert rss <= a.max_rss_kib, (rss, a.max_rss_kib)
print(f'PASS: {sum(map(len,s.values()))} stored entries; identical names, duplicate occurrences and payload hashes')
