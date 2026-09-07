"""Run the production heartbeat gate and log initialization with real flag files."""
from pathlib import Path
import re
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / 'platform/3ds/source/gfx_citro3d.cpp').read_text()
start = source.index('static FILE* Soh3dsOpenPerformanceLog()')
helpers = source[start:source.index('// Frame pacing state', start)]
gate = re.search(r'    if \(\+\+sFrames % 60 == 0[^\n]*', source).group(0)
code = r'''
#include <cassert>
#include <cstdio>
#include <cstdlib>
static bool sRenderProfileEnabled = false;
#include "ship/utils/logging_3ds.h"
unsigned testLoggingFlags = 0;
extern "C" unsigned Soh3dsLoggingFlags() { return testLoggingFlags; }
''' + helpers + r'''
int main(int argc, char** argv) {
    assert(argc == 3);
    testLoggingFlags = static_cast<unsigned>(std::atoi(argv[2]));
    unsigned sFrames = 0, diagnosticWork = 0;
    for (unsigned i = 0; i < 120; ++i) {
        FILE* performanceLog = Soh3dsPerformanceLog();
''' + gate + r'''
            ++diagnosticWork;
        }
    }
    assert(sFrames == 120);
    assert(diagnosticWork == static_cast<unsigned>(std::atoi(argv[1])));
}
'''
with tempfile.TemporaryDirectory() as tmp:
    tmp = Path(tmp)
    cpp = tmp / 'test.cpp'
    cpp.write_text(code)
    exe = tmp / 'test'
    subprocess.run(['c++', '-std=c++17', '-I'+str(root/'third_party/libultraship/include'), str(cpp), '-o', str(exe)], check=True)
    for name, flags, expected, fail_open, modes in (
        ('normal', [], 0, False, 0),
        ('old-flags', ['perfprofile.flag', 'perftrace.flag'], 0, False, 0),
        ('capture', [], 2, False, 1),
        ('capture-failed', [], 0, True, 1),
    ):
        cwd = tmp / name
        cwd.mkdir()
        for flag in flags:
            (cwd / flag).touch()
        if fail_open:
            (cwd / 'perf.csv').mkdir()
        subprocess.run([str(exe), str(expected), str(modes)], cwd=cwd, check=True)
print('Normal play skips heartbeat work; explicit successful capture retains the 60-frame cadence')
