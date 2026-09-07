#!/usr/bin/env python3
"""Decode opt-in 3DS submission timing; this does not measure LCD scanout."""
import argparse
import json
from pathlib import Path
import statistics
import struct

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('trace', type=Path)
args = parser.parse_args()
data = args.trace.read_bytes()
if data[:16] != b'SOH3DS-FRAME-v1\0':
    parser.error('Not a SOH3DS frame trace v1 file')
record = struct.Struct('<12Q')
payload = data[16:]
trailing = len(payload) % record.size
samples = list(record.iter_unpack(payload[:len(payload) - trailing]))
tick_ms = 1000 / 268111856
intervals = []
for prev, cur in zip(samples, samples[1:]):
    if cur[0] != prev[0] + 1 or cur[1] < prev[1]:
        parser.error('Nonmonotonic or discontinuous frame records')
    gap = (cur[1] - prev[1]) * tick_ms
    intervals.append({
        'frame': cur[0], 'target': cur[9], 'target_changed': cur[9] != prev[9],
        'gap_ms': gap, 'render_ms': cur[2] * tick_ms,
        'gpu_queue_wait_ms': cur[3] * tick_ms, 'pacing_swap_wait_ms': cur[4] * tick_ms,
        'loop_ms': cur[5] * tick_ms, 'uploads': cur[6] - prev[6],
        'dropped': (cur[7] - prev[7]) & 0xffffffff,
        'game_ticks': (cur[8] - prev[8]) & 0xffffffff,
        'vblanks': (cur[10] - prev[10]) & 0xffffffff,
        'previous_trace_write_ms': cur[11] * tick_ms,
        # Includes preceding diagnostics, uninstrumented setup and clock overhead.
        'unattributed_ms': gap - sum(cur[2:6]) * tick_ms,
    })
gameplay = [r for r in intervals if r['target'] == 30 and not r['target_changed']]
hitches = [r for r in gameplay if r['gap_ms'] > 1000 / 30 * 1.25]
report = {
    'source': str(args.trace), 'records': len(samples), 'partial_record_bytes': trailing,
    'target30_intervals': len(gameplay),
    'median_gap_ms': statistics.median(r['gap_ms'] for r in gameplay) if gameplay else None,
    'max_gap_ms': max((r['gap_ms'] for r in gameplay), default=None),
    'gap_threshold_ms': 1000 / 30 * 1.25, 'hitches': hitches,
    'limitations': [
        'Submission timestamps are not physical LCD presentation timestamps.',
        'Target 30 includes menus and transitions; movement and scene are not marked here.',
        'Trace writes can perturb timing; previous_trace_write_ms identifies their cost.',
        'Up to 59 final records remain in RAM and are not saved on exit.',
        'Texture uploads do not measure all resource loading or actor work.',
    ],
}
print(json.dumps(report, indent=2))
