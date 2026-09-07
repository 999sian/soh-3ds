"""Exercise the binary trace's real buffering and file layout on the host."""
from pathlib import Path
import subprocess
import tempfile
import struct
import json

root = Path(__file__).resolve().parents[1]
header = root / 'platform/3ds/include/frame_trace_3ds.hpp'
assert header.exists(), 'Per-frame trace implementation is missing'
with tempfile.TemporaryDirectory() as tmp:
    source = Path(tmp) / 'test.cpp'
    source.write_text(r'''
#include "frame_trace_3ds.hpp"
#include <cassert>
#include <cstring>
int main() {
    Soh3dsFrameTrace disabled(nullptr);
    disabled.Record({});
    FILE* file = std::tmpfile();
    assert(file);
    Soh3dsFrameTrace trace(file);
    for (unsigned i = 1; i <= 59; ++i) {
        Soh3dsFrameTrace::Sample sample{};
        sample[0] = i;
        sample[1] = 1000ULL * i;
        trace.Record(sample);
    }
    assert(std::ftell(file) == 16); // header only until a complete batch
    Soh3dsFrameTrace::Sample last{};
    last[0] = 60;
    last[1] = 100000; // preserve a long gap, do not average it away
    trace.Record(last);
    assert(std::ftell(file) == 16 + 60 * 12 * 8);
    std::rewind(file);
    char magic[16];
    assert(std::fread(magic, 1, 16, file) == 16);
    assert(std::memcmp(magic, "SOH3DS-FRAME-v1", 16) == 0);
    for (unsigned i = 1; i <= 60; ++i) {
        Soh3dsFrameTrace::Sample sample{};
        assert(std::fread(sample.data(), sizeof(sample), 1, file) == 1);
        assert(sample[0] == i);
        assert(sample[1] == (i == 60 ? 100000 : 1000ULL * i));
    }
    assert(std::fgetc(file) == EOF);
    std::fclose(file);
}
''')
    exe = Path(tmp) / 'test'
    subprocess.run(['c++', '-std=c++17', '-Wall', '-Wextra', '-Werror',
                    '-I', str(header.parent), str(source), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
    trace_file = Path(tmp) / 'trace.bin'
    ticks = 268111856
    rows = [(1, ticks, 0, 0, 0, 0, 0, 0, 1, 30, 0, 0),
            (2, ticks + ticks // 30, 1000, 0, 0, 0, 0, 0, 1, 30, 2, 0),
            (3, ticks + ticks // 30 + ticks // 20, 2000, 0, 0, 0, 2, 1, 2, 30, 5, 500)]
    trace_file.write_bytes(b'SOH3DS-FRAME-v1\0' +
                           b''.join(struct.pack('<12Q', *row) for row in rows) + b'partial')
    report = json.loads(subprocess.check_output([
        'python3', str(root / 'scripts/analyze-frame-trace.py'), str(trace_file)]))
    assert report['records'] == 3 and report['partial_record_bytes'] == 7
    assert len(report['hitches']) == 1 and report['hitches'][0]['frame'] == 3
    assert report['hitches'][0]['uploads'] == 2 and report['hitches'][0]['dropped'] == 1
print('Frame trace buffering and binary layout passed')
