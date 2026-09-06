#!/usr/bin/env python3
"""Compare the linked fast command writer with pinned libctru command bytes.

Catches reordered words, wrong offsets, alias handling and writes past capacity.
The entry counter additionally verifies that single writes bypass the general
writer; GPUCMD_BASELINE=1 demonstrates the old path failing this criterion.
"""
from pathlib import Path
import os
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
reference = (root / 'tests/fixtures/libctru_gpu_reference.c').read_text()
reference = reference[reference.index('u32* gpuCmdBuf;'):reference.index('void GPUCMD_Split(')]
reference_without_counter = reference
reference = reference.replace('void GPUCMD_Add(u32 header, const u32* param, u32 paramlength)\n{',
                              'void GPUCMD_Add(u32 header, const u32* param, u32 paramlength)\n{\n++general_calls;')
prelude = '''#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <3ds/types.h>
#include <3ds/gpu/gpu.h>
#include <3ds/svc.h>
#define BIT(n) (1u << (n))
extern unsigned general_calls;
'''
main = r'''
#include <assert.h>
#include <setjmp.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <3ds/types.h>
#include <3ds/gpu/gpu.h>
#include <3ds/svc.h>
void __real_GPUCMD_Add(u32, const u32*, u32);
unsigned general_calls;
static jmp_buf panic;
void svcBreak(UserBreakType reason) { assert(reason == USERBREAK_PANIC); longjmp(panic, 1); }
static u32 data[1600], saved[1600], params[1024];
static unsigned cases, fast_singles, general_singles;
static void run_case(u32 header, u32 count, u32 offset, u32 capacity, int alias, int nullbuf, int nullparam) {
    u32 expected_offset = 0;
    int expected_panic = 0;
    for (int mode = 0; mode < 2; ++mode) {
        for (unsigned i = 0; i < 1600; ++i) data[i] = 0xA3F02000u + i;
        gpuCmdBuf = nullbuf ? NULL : data;
        gpuCmdBufOffset = offset;
        gpuCmdBufSize = capacity;
        const u32* source = nullparam ? NULL : alias >= 0 ? &data[alias] : params;
        general_calls = 0;
        int did_panic = setjmp(panic);
        if (!did_panic) {
            if (!mode) __real_GPUCMD_Add(header, source, count);
            else GPUCMD_Add(header, source, count);
        }
        if (!mode) {
            memcpy(saved, data, sizeof(data));
            expected_offset = gpuCmdBufOffset;
            expected_panic = did_panic;
        } else {
            assert(did_panic == expected_panic);
            assert(gpuCmdBufOffset == expected_offset);
            assert(memcmp(saved, data, sizeof(data)) == 0);
            if (count == 1 && !did_panic) { ++fast_singles; general_singles += general_calls; }
            if (count != 1) assert(general_calls == 1);
        }
    }
    ++cases;
}
static double bench(int fast) {
    const unsigned loops = 20000, commands = 512;
    clock_t start = clock();
    for (unsigned repeat = 0; repeat < loops; ++repeat) {
        gpuCmdBuf = data; gpuCmdBufSize = 1600; gpuCmdBufOffset = 0;
        for (u32 i = 0; i < commands; ++i) {
            if (fast) GPUCMD_Add(0x000F0041, &i, 1);
            else __real_GPUCMD_Add(0x000F0041, &i, 1);
        }
    }
    assert(gpuCmdBufOffset == commands * 2);
    return (double)(clock() - start) / CLOCKS_PER_SEC;
}
int main(void) {
    for (unsigned i = 0; i < 1024; ++i) params[i] = 0xDEADBEEFu ^ (i * 98761u);
    // Hand-checked wire format: value first, header second, unrelated words intact.
    memset(data, 0xCC, sizeof(data)); gpuCmdBuf=data; gpuCmdBufSize=1600; gpuCmdBufOffset=2;
    u32 value = 0x12345678; GPUCMD_Add(0x000F0041, &value, 1);
    assert(data[1] == 0xCCCCCCCC && data[2] == 0x12345678 && data[3] == 0x000F0041);
    assert(data[4] == 0xCCCCCCCC && gpuCmdBufOffset == 4);
    const u32 counts[] = {0, 1, 2, 3, 7, 16, 255, 256, 257, 511, 513};
    for (unsigned h = 0; h < 128; ++h) {
        u32 header = (h * 0x137AF19u) ^ 0x800F0300u;
        for (unsigned n = 0; n < sizeof(counts)/sizeof(counts[0]); ++n) {
            for (u32 off = 0; off < 4; ++off) run_case(header, counts[n], off, 1598, -1, 0, 0);
        }
        for (u32 off = 0; off < 8; ++off) {
            run_case(header, 1, off, off + 2, -1, 0, 0); // Exact fit.
            run_case(header, 1, off, off + 1, -1, 0, 0); // No room for header.
            run_case(header, 1, off, off, -1, 0, 0);
            run_case(header, 1, off, 100, -1, 1, 0);
            run_case(header, 1, off, 100, -1, 0, 1); // Null means zero.
            run_case(header, 1, off, 100, (int)off, 0, 0);
            run_case(header, 1, off, 100, (int)off + 1, 0, 0);
            run_case(header, 1, off, 100, (int)off + 2, 0, 0);
        }
    }
    printf("%u command cases match libctru; %u valid single writes, %u general-writer entries\n",
           cases, fast_singles, general_singles);
    fflush(stdout);
    assert(fast_singles > 0 && general_singles == 0);
    return 0;
}
'''.replace('#include <stdio.h>', '#include <stdio.h>\n#include <stdlib.h>')
with tempfile.TemporaryDirectory(prefix='soh-gpucmd-') as directory:
    p = Path(directory)
    (p / '3ds/gpu').mkdir(parents=True)
    (p / '3ds/types.h').write_text('#pragma once\n#include <stdint.h>\ntypedef uint32_t u32;\n')
    (p / '3ds/svc.h').write_text('#pragma once\ntypedef enum {USERBREAK_PANIC=0} UserBreakType;\nvoid svcBreak(UserBreakType);\n')
    (p / '3ds/gpu/gpu.h').write_text('#pragma once\n#include <3ds/types.h>\nextern u32 *gpuCmdBuf, gpuCmdBufSize, gpuCmdBufOffset;\nvoid GPUCMD_Add(u32,const u32*,u32);\n')
    (p / 'reference.c').write_text(prelude + reference)
    (p / 'main.c').write_text(main)
    candidate = root / 'platform/3ds/source/gpu_command_fast_3ds.c'
    if os.getenv('GPUCMD_BASELINE'):
        candidate = p / 'baseline.c'
        candidate.write_text('#include <3ds/types.h>\nvoid __real_GPUCMD_Add(u32,const u32*,u32);\nvoid __wrap_GPUCMD_Add(u32 h,const u32*p,u32 n){__real_GPUCMD_Add(h,p,n);}\n')
    flags = ['-std=c11', '-O3', '-g', '-fno-strict-aliasing', '-I' + str(p)]
    if os.getenv('SANITIZE'):
        flags += ['-fsanitize=address,undefined', '-fno-omit-frame-pointer', '-no-pie']
    subprocess.run([os.getenv('CC', 'gcc'), *flags, str(p/'main.c'), str(p/'reference.c'),
                    str(candidate), '-Wl,--wrap=GPUCMD_Add', '-o', str(p/'test')], check=True)
    subprocess.run([str(p/'test')], check=True)
    if os.getenv('BENCHMARK'):
        # Counting general-writer entries would penalize only the reference.
        # Benchmark a second binary with no entry instrumentation in either path.
        (p / 'reference.c').write_text(prelude + reference_without_counter)
        (p / 'main.c').write_text(main[:main.index('int main(void) {')] + r'''
int main(void) {
    bench(0); bench(1);
    for (int i = 0; i < 4; ++i) {
        double slow, fast;
        if (i & 1) { fast = bench(1); slow = bench(0); }
        else { slow = bench(0); fast = bench(1); }
        printf("uninstrumented host ratio fast/reference %.3f (%.4fs / %.4fs)\n",fast/slow,fast,slow);
    }
}
''')
        subprocess.run([os.getenv('CC', 'gcc'), *flags, str(p/'main.c'), str(p/'reference.c'),
                        str(candidate), '-Wl,--wrap=GPUCMD_Add', '-o', str(p/'bench')], check=True)
        subprocess.run([str(p/'bench')], check=True)
